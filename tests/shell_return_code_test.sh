#!/bin/sh
set -eu

# A command process publishes its result through the broker. A command that
# was not found never starts, though, so Shell.c's local cli_Result2 must be
# promoted when the shell returns. Check both paths in one-shot shells, which
# are the process whose exit status becomes System()/ADDRESS COMMAND's RC.
repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-shell-return-code.XXXXXX")
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
    printf 'shell return code test: %s\n' "$1" >&2
    printf 'shell output was:\n%s\n' "${output-}" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$runtime_dir"
cp "$repo_dir/build/Echo" "$repo_dir/build/Delete" "$sys_dir/C/"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

run_case()
{
    name=$1
    expected=$2
    command=$3
    script_path="$test_dir/$name.script"
    printf '%s\n' "$command" > "$script_path"
    amiga_script="rootfs:${script_path#/}"

    set +e
    output=$(env ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
        ACE_BROKER_SOCKET="$socket_path" ACE_SESSION="shell-rc-$name" \
        ACE_STARTUP_SCRIPT="$amiga_script" \
        "$repo_dir/build/ace-user-shell" </dev/null 2>&1)
    rc=$?
    set -e
    [ "$rc" -eq "$expected" ] ||
        fail "$name returned $rc, expected $expected"
}

run_case success 0 'Echo ok'
run_case command-failure 5 'Delete SYS:file-that-does-not-exist'
run_case command-not-found 10 'AceDefinitelyMissingCommand'
printf '%s\n' "$output" | grep -q 'object not found' ||
    fail 'command-not-found did not report the missing object'

run_case recovery 0 "$(printf 'AceDefinitelyMissingCommand\nEcho recovered')"

printf 'shell return code test: ok\n'
