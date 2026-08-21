#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-peek.XXXXXX")
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
    printf 'peek test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" \
         "$sys_dir/PeekTest" "$runtime_dir"
printf 'peek\n' > "$sys_dir/PeekTest/plain.txt"
printf 'peek\n' > "$sys_dir/PeekTest/colon:name"
printf 'peek\n' > "$sys_dir/PeekTest/hello@HJ2GQZLSMUAA"
printf 'peek\n' > "$sys_dir/PeekTest/hello@HJ2GQZLSMUBB"
printf 'peek\n' > "$sys_dir/PeekTest/hello:there"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

run_peek()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=peek-test \
        "$repo_dir/build/Peek" "$@"
}

[ "$(run_peek SYS:PeekTest/plain.txt)" = \
  "$sys_dir/PeekTest/plain.txt" ] ||
    fail 'AmigaDOS name did not resolve to its full Linux path'

matches=$(run_peek 'SYS:PeekTest/hello#?')
[ "$(printf '%s\n' "$matches" | wc -l)" -eq 3 ] ||
    fail 'AmigaDOS wildcard did not match the translated filename'
printf '%s\n' "$matches" | grep -F -q \
    "$sys_dir/PeekTest/hello@HJ2GQZLSMUAA" ||
    fail 'wildcard output omitted the first matching filename'
printf '%s\n' "$matches" | grep -F -q \
    "$sys_dir/PeekTest/hello@HJ2GQZLSMUBB" ||
    fail 'wildcard output omitted the second matching filename'
printf '%s\n' "$matches" | grep -F -q \
    "$sys_dir/PeekTest/hello:there" ||
    fail 'wildcard output omitted the mapped filename'

host_name="$sys_dir/PeekTest/colon:name"
mapped_name=$(run_peek "$host_name" HOST)
expected_name=$(env ACE_BROKER_SOCKET="$socket_path" \
    "$repo_dir/build/ace-brokerctl" name "$host_name")
[ "$mapped_name" = "$expected_name" ] ||
    fail 'HOST mode did not use the broker name mapping'

[ "$(run_peek "$host_name" LINUX)" = "$expected_name" ] ||
    fail 'LINUX alias did not select HOST mode'

printf 'Peek mapping test passed\n'
