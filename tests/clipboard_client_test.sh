#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ace-clipboard-client.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

clip_dir=$test_dir/clips
host_file=$test_dir/host
broker_socket=$test_dir/broker.sock
mkdir "$clip_dir"

clip_env="ACE_CLIPBOARD_DIR=$clip_dir ACE_CLIPBOARD_HOST_FILE=$host_file ACE_SYS_DIR=$repo_dir/build ACE_BROKER_SOCKET=$broker_socket ACE_SESSION=clipboard-client-test"

printf 'from host' >"$host_file"
env $clip_env "$repo_dir/build/Clip" GET >"$test_dir/get-host"
test "$(cat "$test_dir/get-host")" = 'from host'

env $clip_env "$repo_dir/build/Clip" SET 'from amiga'
test "$(cat "$host_file")" = 'from amiga'

test "$(env $clip_env "$repo_dir/build/Clip" COUNT)" = 1

printf 'unit seven' | env $clip_env "$repo_dir/build/acepaste" --unit 7 --set
test -f "$clip_dir/clip7"
test "$(env $clip_env "$repo_dir/build/acepaste" --unit 7 --get)" = 'unit seven'
test "$(env $clip_env "$repo_dir/build/Clip" COUNT)" = 2

env $clip_env "$repo_dir/build/Clip" GET WAIT >"$test_dir/wait-output" 2>"$test_dir/wait-error" &
wait_pid=$!
sleep 0.15
printf 'waited text' | env $clip_env "$repo_dir/build/acepaste" --set
wait "$wait_pid"
test "$(cat "$test_dir/wait-output")" = 'waited text'
test ! -s "$test_dir/wait-error"

printf '%s\n' 'clipboard client tests passed'
