#!/bin/sh
# Exercise the Amiga/AROS LhA binary through ACE's AmigaDOS and POSIX seams.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-lha-test.XXXXXX")
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
archive_name="lha-seam-test.lha"
archive_path="$sys_dir/C/$archive_name"
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
trap cleanup EXIT INT TERM

fail()
{
    printf 'LhA integration test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" "$runtime_dir"
for command in "$repo_dir"/build/*; do
    [ -f "$command" ] && [ -x "$command" ] || continue
    ln -s "$command" "$sys_dir/C/$(basename "$command")"
done

ACE_SYS_DIR="$sys_dir" "$repo_dir/build/ace-broker" "$socket_path" \
    >/dev/null 2>&1 &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail "broker did not start"

output=$(printf '%s\n' \
    "C:LhA a SYS:C/$archive_name README.md SYS:C/LhA" \
    "C:LhA l SYS:C/$archive_name" \
    'EndCLI' | \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=lha-test \
    "$repo_dir/build/ace-user-shell" 2>&1)

[ -s "$archive_path" ] || fail "LhA did not create the archive"
case "$output" in
    *"README.md"*) ;;
    *) fail "archive listing did not contain the relative input" ;;
esac
case "$output" in
    *"SYS:C/LhA"*) ;;
    *) fail "archive listing did not contain the Amiga-volume input" ;;
esac

printf '%s\n' 'LhA Amiga/AROS integration test passed'
