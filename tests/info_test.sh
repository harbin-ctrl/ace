#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ace-info-test.XXXXXX")
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
    printf 'Info test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir" "$runtime_dir"
ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

dos_list=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=info-test \
    "$repo_dir/build/ace-brokerctl" doslist)
volume=$(printf '%s\n' "$dos_list" | awk -F '\t' 'NF >= 1 { print $1; exit }')
volume_label=$(printf '%s\n' "$dos_list" |
    awk -F '\t' -v device="$volume" '$1 == device { print $4; exit }')
[ -n "$volume" ] || fail 'broker did not publish a DOS volume'
[ -n "$volume_label" ] || fail 'selected DOS volume did not have a label'

run_info()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=info-test \
        "$repo_dir/build/Info" "$@"
}

default_output=$(run_info "$volume:")
printf '%s\n' "$default_output" | grep -q '^Unit .*State' || \
    fail 'default output did not include the disk heading'
printf '%s\n' "$default_output" | grep -q "$volume" || \
    fail 'default output did not include the selected device'
printf '%s\n' "$default_output" | grep -q '^Volumes available:' || \
    fail 'default output did not include the volume section'

block_output=$(run_info BLOCKS "$volume:")
printf '%s\n' "$block_output" | grep -q 'Total blocks:' || \
    fail 'BLOCKS did not include block counts'
printf '%s\n' "$block_output" | grep -q 'Blocksize:' || \
    fail 'BLOCKS did not include the block size'

volume_output=$(run_info VOLUMES "$volume:")
printf '%s\n' "$volume_output" | grep -q '^Volumes available:' || \
    fail 'VOLUMES did not include the volume section'
printf '%s\n' "$volume_output" | grep -q "$volume_label" || \
    fail 'VOLUMES did not include the selected volume'

printf '%s\n' 'Info 32/64-bit reporting test passed'
