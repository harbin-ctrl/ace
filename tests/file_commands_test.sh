#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-file-commands.XXXXXX")
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
    printf 'file commands test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" "$runtime_dir"
printf 'ACE file-command test\n' > "$sys_dir/C/source.txt"

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
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=file-commands \
        "$repo_dir/build/$command_name" "$@"
}

run_command Copy SYS:C/source.txt SYS:C/copied.txt QUIET
cmp "$sys_dir/C/source.txt" "$sys_dir/C/copied.txt" || fail 'Copy made the wrong contents'

run_command Touch SYS:C/touched.txt
[ -f "$sys_dir/C/touched.txt" ] || fail 'Touch did not create a missing file'

run_command MakeLink SYS:C/soft-link SYS:C/source.txt
[ -L "$sys_dir/C/soft-link" ] || fail 'MakeLink did not create a soft link'
[ "$(readlink "$sys_dir/C/soft-link")" = "$sys_dir/C/source.txt" ] || \
    fail 'MakeLink soft link points at the wrong target'

run_command MakeLink SYS:C/dangling-link SYS:C/missing-target
[ -L "$sys_dir/C/dangling-link" ] || \
    fail 'MakeLink did not create a dangling soft link'

run_command MakeLink SYS:C/hard-link SYS:C/source.txt HARD
[ -f "$sys_dir/C/hard-link" ] || fail 'MakeLink did not create a hard link'
[ "$(stat -c '%i' "$sys_dir/C/hard-link")" = \
  "$(stat -c '%i' "$sys_dir/C/source.txt")" ] || \
    fail 'MakeLink hard link does not share the source inode'

printf 'second line\n' > "$sys_dir/C/second.txt"
run_command Join SYS:C/source.txt SYS:C/second.txt AS SYS:C/joined.txt
printf 'ACE file-command test\nsecond line\n' > "$sys_dir/C/expected-joined.txt"
cmp "$sys_dir/C/expected-joined.txt" "$sys_dir/C/joined.txt" || \
    fail 'Join produced the wrong contents'

list_output=$(run_command List SYS:C NOHEAD FILES)
printf '%s\n' "$list_output" | grep -q 'source.txt' || fail 'List omitted source.txt'
printf '%s\n' "$list_output" | grep -q 'copied.txt' || fail 'List omitted copied.txt'
printf '%s\n' "$list_output" | grep -q 'touched.txt' || fail 'List omitted touched.txt'

printf '%s\n' 'Copy, List, Touch, MakeLink, and Join file-command test passed'
