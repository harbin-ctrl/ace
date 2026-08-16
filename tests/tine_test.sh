#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-tine-test.XXXXXX")
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
log_path="$test_dir/tine.log"
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
    printf 'tine test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" \
         "$runtime_dir"
printf 'hello\n' > "$sys_dir/C/esc.txt"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

# ED/Tine is a full-screen program, so feed it through a real pseudo-terminal.
# ESC enters extended-command mode, Q is Quit, and Return executes it. The
# command-line transcript must contain the asterisk prompt before Q.
set +e
{
    sleep 0.5
    printf '\033'
    sleep 0.1
    printf 'Q\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_BROKER_SOCKET='$socket_path' \
         ACE_SESSION=tine-esc-test '$repo_dir/tools/tine/tine' SYS:C/esc.txt" \
        "$log_path" >/dev/null 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    tail -c 512 "$log_path" >&2 || true
    fail "Tine did not exit after ESC-Q-Return (status $status)"
fi
grep -a -q '\*' "$log_path" ||
    fail 'ESC did not display the extended-command asterisk prompt'

# The ED wrapper must consume the Amiga argument template rather than passing
# keywords such as FROM and WITH to Tine as command-file names. TABS must also
# affect the initial tab stop: TB followed by TY should leave four spaces
# between the two characters when the tab distance is five.
printf 'I/a/;TB;TY/b/;X\n' > "$sys_dir/C/with.cmd"
env ACE_TINE_BINARY="$repo_dir/tools/tine/tine" \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=tine-ed-args \
    "$repo_dir/build/ED" FROM SYS:C/from.txt WITH SYS:C/with.cmd TABS 5 \
    > /dev/null 2>&1 || fail 'ED argument compatibility failed'
printf 'a    b\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/from.txt" "$test_dir/expected" ||
    fail 'ED FROM/WITH/TABS produced the wrong file'

printf 'tine ESC prompt test passed\n'
