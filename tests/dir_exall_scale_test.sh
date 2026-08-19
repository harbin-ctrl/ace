#!/bin/sh
set -eu

# Confirms two ExNext()/ExAll() bugs that only showed up on a directory large
# enough to matter: a directory with thousands of entries but only 10-20
# printed.
#
# 1. src/native_dos.c's ExNext() used to return DOSFALSE for any entry it
#    could not spell (component_needs_mapping() giving up on a too-long or
#    illegal name), and AROS's real rom/dos/exall.c treats any ExNext()
#    failure other than ERROR_NO_MORE_ENTRIES as a hard error, discarding
#    everything already collected; workbench/c/Dir.c's ALL recursion then
#    propagates that failure up through every parent directory. One
#    unrepresentable name anywhere in a tree used to blank out the whole
#    listing. Fixed by skipping just that one entry instead of aborting the
#    scan (see the comment on the ERROR_INVALID_COMPONENT_NAME /
#    ERROR_LINE_TOO_LONG check in ExNext()).
#
# 2. Separately, and independent of any unmappable name: AROS's real
#    rom/dos/exall.c batches entries into a fixed buffer and, when a batch
#    fills up mid-entry, rolls fib_DiskKey back by one and calls ExNext()
#    again expecting that same entry back, so it can retry it in the next
#    batch. ACE's directory scan is a forward-only POSIX readdir() stream,
#    which has no way to "give back" an entry -- so without a fix, one entry
#    was silently lost at every batch boundary (roughly one in every ~65
#    entries for plain filenames, reproducing exactly the "10-20 instead of
#    thousands" style loss on directories big enough to need more than one
#    batch). Fixed with a telldir()/seekdir() replay: ExNext() detects the
#    rollback (fib_DiskKey == scan_key - 1) and seeks back to replay the
#    entry instead of reading past it.

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-dir-exall-scale.XXXXXX")
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
    printf 'dir exall scale test: %s\n' "$1" >&2
    exit 1
}

work="$sys_dir/pi"
mkdir -p "$work" "$runtime_dir"

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
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=dir-exall-scale \
        "$repo_dir/build/$command_name" "$@"
}

file_count=500
i=1
while [ "$i" -le "$file_count" ]; do
    : > "$work/file$i.txt"
    i=$((i + 1))
done

# One name too long to spell, so component_needs_mapping() gives up on it
# (see broker.c) and ExNext() must skip rather than abort over it.
python3 -c "
name = 'x' * 200
open('$work/' + name, 'w').close()
"

listing=$(run_command Dir SYS:pi ALL FILES)

missing=0
i=1
while [ "$i" -le "$file_count" ]; do
    case "$listing" in
        *"file$i.txt"*) ;;
        *) missing=$((missing + 1)) ;;
    esac
    i=$((i + 1))
done

[ "$missing" -eq 0 ] ||
    fail "$missing of $file_count ordinary files were missing from the listing"

printf 'Dir ExAll scale test passed\n'
