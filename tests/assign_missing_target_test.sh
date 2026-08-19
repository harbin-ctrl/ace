#!/bin/sh
set -eu

# Confirms `Assign FOO: NOSUCH:` -- assigning a name to a target that does
# not exist -- fails and does not create the assign. TODO.md once flagged
# this as reporting success (exit 0) with the note that it "has not been
# investigated" and might just be session scoping rather than a real defect.
# It could not be reproduced under any invocation style tried (an isolated
# session, no ACE_SESSION at all, the real default broker, or a live shell
# session): every one already matches real AROS's workbench/c/Assign.c,
# which calls Lock() on the target before AssignLock() and prints "Can't
# find %s" plus RETURN_FAIL when that Lock() fails. This test exists so a
# regression is caught rather than rediscovered.

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-assign-missing-target.XXXXXX")
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
    printf 'assign missing target test: %s\n' "$1" >&2
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

run_command()
{
    command_name=$1
    shift
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=assign-missing-target \
        "$repo_dir/build/$command_name" "$@"
}

output=$(run_command Assign FOO: NOSUCH: 2>&1) && rc=0 || rc=$?
[ "$rc" -ne 0 ] || fail "Assign FOO: NOSUCH: exited 0, expected failure: $output"
case "$output" in
    *"Can't find"*) ;;
    *) fail "Assign did not report why it failed: $output" ;;
esac

assigns=$(run_command ace-brokerctl assigns)
case "$assigns" in
    *FOO*) fail "FOO: was created despite its target not existing: $assigns" ;;
esac

printf 'Assign missing target test passed\n'
