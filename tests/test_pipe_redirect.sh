#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
input_path="$runtime_dir/ace/t/input.txt"
fifo_path="$runtime_dir/ace-pipes/testpipe"
output_path="$test_dir/output.txt"
broker_pid=""
reader_pid=""

cleanup()
{
    if [ -n "$reader_pid" ] && kill -0 "$reader_pid" 2>/dev/null; then
        kill "$reader_pid" 2>/dev/null || true
    fi
    if [ -n "$broker_pid" ] && kill -0 "$broker_pid" 2>/dev/null; then
        kill -TERM "$broker_pid" 2>/dev/null || true
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT INT TERM

mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" \
         "$runtime_dir/ace/t" "$runtime_dir/ace-pipes"
for command in "$repo_dir"/build/*; do
    [ -f "$command" ] && [ -x "$command" ] || continue
    ln -s "$command" "$sys_dir/C/$(basename "$command")"
done

printf 'alpha\nbeta\ngamma\n' > "$input_path"
mkfifo "$fifo_path"
cat "$fifo_path" > "$output_path" &
reader_pid=$!

XDG_RUNTIME_DIR="$runtime_dir" ACE_SYS_DIR="$sys_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done

XDG_RUNTIME_DIR="$runtime_dir" ACE_SYS_DIR="$sys_dir" \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION="pipe-redirect-test" \
    "$repo_dir/build/Type" T:input.txt TO PIPE:testpipe >/dev/null

wait "$reader_pid"
reader_pid=""
cmp "$input_path" "$output_path"
