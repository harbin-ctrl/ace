#!/bin/sh
set -eu

# Confirms `ace-brokerctl assign NAME PATH` -- the form README.md documents
# ("ace-brokerctl assign WORK: /tmp") -- actually works. It used to fail
# silently: broker.c's ASSIGN handler always ran the target through
# resolve_path() with host_path=false, which parses it as an AmigaDOS path
# (colon-relative, or relative to the session's Amiga current directory).
# A raw Linux path like "/tmp" has no colon, so it was looked up as a
# subdirectory named "tmp" underneath the current Amiga directory -- which
# does not exist -- and the request failed with ENOENT. brokerctl also never
# printed anything on failure, so the command just exited 1.
#
# Fixed by giving ace-brokerctl's CLI-level assign the AMIGA_BROKER_PATH_HOST
# flag (host_path=true), which broker.c's ASSIGN handler now honours, and by
# making brokerctl report the errno on failure. This does not change the
# real Assign command / assign_compat.c, which already hands broker.c an
# AmigaDOS path resolved through NameFromLock() and must keep host_path=false.

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-brokerctl-assign.XXXXXX")
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
broker_pid=""

cleanup()
{
    if [ -n "$broker_pid" ] && kill -0 "$broker_pid" 2>/dev/null; then
        kill -TERM "$broker_pid" 2>/dev/null || true
        for _ in $(seq 1 100); do
            kill -0 "$broker_pid" 2>/dev/null || break
            sleep 0.01
        done
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'brokerctl assign test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$runtime_dir"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

run_ctl()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=brokerctl-assign-test \
        "$repo_dir/build/ace-brokerctl" "$@"
}

target_dir="$test_dir/work"
mkdir -p "$target_dir"

run_ctl assign WORK: "$target_dir" ||
    fail "assign WORK: $target_dir exited nonzero"

assigns=$(run_ctl assigns)
case "$assigns" in
    *"WORK"*"$target_dir"*) ;;
    *) fail "assigns did not list WORK: -> $target_dir: $assigns" ;;
esac

resolved=$(run_ctl resolve WORK:)
[ "$resolved" = "$target_dir" ] ||
    fail "resolve WORK: gave '$resolved', expected '$target_dir'"

# A target that does not exist must still fail (and say why), not just the
# broken "always fails" case above.
if err=$(run_ctl assign BAD: "$test_dir/no-such-directory" 2>&1); then
    fail "assign to a missing directory unexpectedly succeeded"
fi
case "$err" in
    *"No such file or directory"*) ;;
    *) fail "assign failure printed nothing useful: $err" ;;
esac

printf 'ace-brokerctl assign test passed\n'
