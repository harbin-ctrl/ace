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
# Both ends are on one volume, so the target is stored as a route between them
# rather than as this mount's absolute path.  That is what AmigaDOS means by a
# link inside a volume, and it is the only spelling that still means the same
# thing after ACE exits -- an absolute host path would be true of this mount
# only, and inside the device view, of this session only.
[ "$(readlink "$sys_dir/C/soft-link")" = "source.txt" ] || \
    fail "MakeLink stored the wrong target: $(readlink "$sys_dir/C/soft-link")"
cmp "$sys_dir/C/soft-link" "$sys_dir/C/source.txt" || \
    fail 'the soft link does not resolve to the file it names'

# AmigaDOS hard-links directories; no Linux filesystem does, at any privilege
# level.  Both refusals must say which one they are: without FORCE the answer
# is the AmigaDOS rule, and with it the answer is that the host cannot, which
# is the difference between "try harder" and "stop".
mkdir -p "$sys_dir/C/a-drawer"
out=$(run_command MakeLink SYS:C/dir-link SYS:C/a-drawer HARD 2>&1 || true)
case "$out" in
    *"FORCE"*) ;;
    *) fail "MakeLink HARD on a drawer did not mention FORCE: $out" ;;
esac
out=$(run_command MakeLink SYS:C/dir-link SYS:C/a-drawer HARD FORCE 2>&1 || true)
case "$out" in
    *"cannot hard-link directories"*) ;;
    *) fail "MakeLink HARD FORCE on a drawer did not report the host refusal: $out" ;;
esac
if [ -e "$sys_dir/C/dir-link" ]; then
    fail 'MakeLink reported a refusal and made the link anyway'
fi

# A refused soft link says so.  Upstream returns a failure code and prints
# nothing, which looks exactly like success from a script.
out=$(run_command MakeLink SYS:C/missing-drawer/link SYS:C/source.txt 2>&1 || true)
[ -n "$out" ] || fail 'a refused soft link printed nothing at all'

run_command MakeLink SYS:C/dangling-link SYS:C/missing-target
[ -L "$sys_dir/C/dangling-link" ] || \
    fail 'MakeLink did not create a dangling soft link'

run_command MakeLink SYS:C/hard-link SYS:C/source.txt HARD
[ -f "$sys_dir/C/hard-link" ] || fail 'MakeLink did not create a hard link'
# Not merely a file of the same name: a hard link is a second name for one
# object, and the inode is the only thing that says so.
[ "$(stat -c %i "$sys_dir/C/source.txt")" = "$(stat -c %i "$sys_dir/C/hard-link")" ] || \
    fail 'MakeLink HARD made a separate object rather than a second name'
[ "$(stat -c '%i' "$sys_dir/C/hard-link")" = \
  "$(stat -c '%i' "$sys_dir/C/source.txt")" ] || \
    fail 'MakeLink hard link does not share the source inode'

printf 'second line\n' > "$sys_dir/C/second.txt"
run_command Join SYS:C/source.txt SYS:C/second.txt AS SYS:C/joined.txt
printf 'ACE file-command test\nsecond line\n' > "$sys_dir/C/expected-joined.txt"
cmp "$sys_dir/C/expected-joined.txt" "$sys_dir/C/joined.txt" || \
    fail 'Join produced the wrong contents'

eval_output=$(run_command Eval 2 + 3)
[ "$eval_output" = '5' ] || fail "Eval produced '$eval_output' instead of 5"

list_output=$(run_command List SYS:C NOHEAD FILES)
printf '%s\n' "$list_output" | grep -q 'source.txt' || fail 'List omitted source.txt'
printf '%s\n' "$list_output" | grep -q 'copied.txt' || fail 'List omitted copied.txt'
printf '%s\n' "$list_output" | grep -q 'touched.txt' || fail 'List omitted touched.txt'

printf '%s\n' 'Copy, List, Touch, MakeLink, Join, and Eval file-command test passed'
