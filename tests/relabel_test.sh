#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-relabel.XXXXXX")
sys_dir=$test_dir/sys
runtime_dir=$test_dir/run
socket_path=$test_dir/broker.sock
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
    printf 'relabel test: %s\n' "$1" >&2
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

run_command()
{
    command_name=$1
    shift
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=relabel-test \
        "$repo_dir/build/$command_name" "$@"
}

dos_list=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=relabel-test \
    "$repo_dir/build/ace-brokerctl" doslist)
ram_name=$(printf '%s\n' "$dos_list" | awk -F '\t' '$2 == "tmpfs" { print $1; exit }')
[ -n "$ram_name" ] || fail 'broker did not publish a tmpfs volume'

run_command Relabel "$ram_name:" ACE_RAM_LABEL
dos_list=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=relabel-test \
    "$repo_dir/build/ace-brokerctl" doslist)
printf '%s\n' "$dos_list" | awk -F '\t' \
    -v name="$ram_name" '$1 == name && $4 == "ACE_RAM_LABEL" { found = 1 } END { exit !found }' \
    || fail 'tmpfs relabel did not update the live DOS catalog'

unsupported_name=$(printf '%s\n' "$dos_list" | awk -F '\t' \
    '$2 != "" && $2 != "tmpfs" && $2 !~ /^(vfat|fat|msdos)$/ && $2 !~ /^ext[234]$/ { print $1; exit }')
[ -n "$unsupported_name" ] || fail 'broker did not publish an unsupported volume type'
set +e
unsupported_output=$(run_command Relabel "$unsupported_name:" ACE_OTHER_LABEL 2>&1)
unsupported_status=$?
set -e
[ "$unsupported_status" -ne 0 ] || fail 'unsupported filesystem relabel unexpectedly succeeded'
printf '%s\n' "$unsupported_output" | grep -q 'filesystem action type unknown' \
    || fail 'unsupported filesystem error was not reported'

printf '%s\n' 'Relabel tmpfs and unsupported-filesystem tests passed'
