#!/bin/sh
set -eu

# A prompt may contain newlines. "*N" is the AmigaDOS star-escape for one,
# ReadItem() converts it inside quotes, and a prompt that puts a blank line
# or a second line before the cursor is an ordinary thing for an Amiga user
# to ask for -- Prompt "*E[33m%S*N> " is the shape colour prompts usually
# take, because the colour then runs to the end of its own line.
#
# The broker keeps the prompt whole, but its GETCLI reply packs the return
# code, Result2, the fail level and the prompt as newline-separated fields.
# Cli() used to read the prompt with strtok_r(), which stopped at the first
# newline inside it and silently dropped the rest of the prompt -- visible
# as a prompt that ends where its first "*N" was. The prompt is the last
# field precisely so that it can be taken whole; this checks that it is.

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-prompt-newline.XXXXXX")
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
    printf 'prompt newline test: %s\n' "$1" >&2
    printf 'shell output was:\n%s\n' "${output-}" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$runtime_dir"
# The shell finds its commands through C:, so the two this test types have
# to be there. Copies rather than links keep the test independent of how
# the build tree is laid out.
cp "$repo_dir/build/Prompt" "$repo_dir/build/Echo" "$sys_dir/C/"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

# The shell draws the new prompt before it reads the next line, so running
# one more command is what makes the prompt observable on stdout.
output=$(printf 'Prompt "A*NB> "\nEcho marker\n' |
    env ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=prompt-newline \
        "$repo_dir/build/ace-user-shell" 2>&1 | tr -d '\r')

# Both halves, on their own lines: a truncated prompt puts "marker" straight
# after the "A" instead, and never prints "B> " at all.
printf '%s\n' "$output" | grep -qx 'A' ||
    fail 'first line of the prompt is missing'
printf '%s\n' "$output" | grep -q '^B> marker$' ||
    fail 'prompt was cut at its newline'

printf 'prompt newline test: ok\n'
