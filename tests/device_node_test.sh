#!/bin/sh
# Things AmigaDOS has no word for.
#
# Linux filesystems hold objects that are not files: character and block
# devices, sockets, FIFOs.  AmigaDOS has devices in plenty -- NIL:, SER:,
# PRT:, CON: -- but a device there is an entry in the DOS device list, opened
# by its own name, never a name sitting inside a drawer.  A node is a
# filesystem entry impersonating a device, and that is a category AmigaDOS
# does not have.
#
# ACE's answer is to let them be named and refuse to open them.  The name is
# real, so Examine, Rename and Delete must go on working; the contents are not
# a file, so Open answers ERROR_OBJECT_WRONG_TYPE -- AmigaDOS's own sentence
# for "not the kind of object you can do that to".
#
# The refusal has to happen before the open, which is what this test is really
# checking, because for these objects the attempt is the damage:
#
#   - a FIFO with no writer blocks in open() and ignores SIGINT, so Ctrl-C
#     cannot end it and the shell is wedged until something kills it;
#   - a character device opens instantly and never ends, so Type runs forever
#     and Copy fills the disk it is writing to.
#
# Both were true before the broker started answering the question itself.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-device-node.XXXXXX")
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
broker_pid=

cleanup()
{
    if [ -n "$broker_pid" ] && kill -0 "$broker_pid" 2>/dev/null; then
        kill -TERM "$broker_pid" 2>/dev/null || true
        for _ in $(seq 1 200); do
            kill -0 "$broker_pid" 2>/dev/null || break
            sleep 0.01
        done
    fi
    # The character device is root-made where sudo was available.
    sudo -n rm -rf "$test_dir" 2>/dev/null || true
    rm -rf "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'device node test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$runtime_dir"
printf 'ordinary\n' > "$sys_dir/C/a-file"
mkfifo "$sys_dir/C/a-fifo" || fail 'could not create a FIFO'
python3 -c "import socket,sys; s=socket.socket(socket.AF_UNIX); s.bind(sys.argv[1])" \
    "$sys_dir/C/a-socket" 2>/dev/null || fail 'could not create a socket'
# A character device needs root to make; where that is not available the FIFO
# and the socket still carry the test.
nodes="a-fifo a-socket"
if command -v sudo >/dev/null 2>&1 && sudo -n /usr/bin/true 2>/dev/null; then
    if sudo -n mknod "$sys_dir/C/a-zero" c 1 5 2>/dev/null; then
        sudo -n chmod 666 "$sys_dir/C/a-zero"
        nodes="$nodes a-zero"
    fi
fi

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 200); do
    [ -S "$socket_path" ] && break
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'the broker did not start'

run()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=device-node \
        "$repo_dir/build/$@"
}

# Named, because they are there.
listing=$(run List SYS:C 2>&1) || fail "List failed: $listing"
for node in $nodes; do
    printf '%s\n' "$listing" | grep -qw -- "$node" ||
        fail "$node is not listed, but it exists"
done

# Refused, and refused promptly.  The timeout is the assertion: before the
# broker answered this, the FIFO never returned at all.
for node in $nodes; do
    start=$(date +%s)
    out=$(timeout 10 env ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=device-node "$repo_dir/build/Type" "SYS:C/$node" 2>&1)
    outcome=$?
    elapsed=$(( $(date +%s) - start ))
    [ "$outcome" != 124 ] ||
        fail "Type on $node never returned -- it is blocked in open()"
    [ "$outcome" != 0 ] ||
        fail "Type on $node succeeded; it is not a file to be read"
    [ "$elapsed" -lt 5 ] ||
        fail "Type on $node took ${elapsed}s, so it tried the open first"
done

# And an ordinary file in the same drawer is unaffected.
out=$(run Type SYS:C/a-file 2>&1) || fail "an ordinary file stopped opening: $out"
[ "$out" = "ordinary" ] || fail "an ordinary file read back wrongly: $out"

# Copy refuses rather than reading an endless one onto the disk.
out=$(timeout 10 env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=device-node \
    "$repo_dir/build/Copy" SYS:C/a-fifo SYS:C/a-copy 2>&1)
outcome=$?
[ "$outcome" != 124 ] || fail 'Copy from a FIFO never returned'
[ ! -e "$sys_dir/C/a-copy" ] || fail 'Copy produced a file from a FIFO'

# The name is a name: it can be deleted like anything else.
run Delete SYS:C/a-socket >/dev/null 2>&1 ||
    fail 'a socket could not be deleted'
[ ! -e "$sys_dir/C/a-socket" ] || fail 'Delete reported success and left the socket'

# A name with a colon is a device, a volume or an assign.  One that is none of
# them has an ordinary answer -- AmigaDOS says the device is not mounted --
# and no second reading worth trying, because a colon cannot appear in a
# filename.  Resolving it against the current directory instead invented
# files: "Copy x TO NIL:" made a drawer called "NIL:" and put a real copy of x
# in it, and every unimplemented device did the same thing quietly.
for absent in ABC: DF0: FOO:bar; do
    out=$(run Type "$absent" 2>&1 || true)
    case "$out" in
        *"not mounted"*) ;;
        *) fail "$absent was not reported as an unmounted device: $out" ;;
    esac
done
# NIL: is implemented, so it answers rather than being absent: writes are
# thrown away and a read is immediately at end of file, which is what AmigaOS
# had it do and what /dev/null does behind it.
out=$(run Type NIL: 2>&1) || fail "NIL: could not be read: $out"
[ -z "$out" ] || fail "NIL: read back something: $out"
run Copy SYS:C/a-file TO NIL: >/dev/null 2>&1 ||
    fail 'a copy to NIL: was refused'
[ ! -e "NIL:" ] || fail 'copying to NIL: created a drawer called NIL:'

out=$(run Copy SYS:C/a-file TO ABC: 2>&1 || true)
[ ! -e "$sys_dir/C/ABC:" ] && [ ! -e "ABC:" ] ||
    fail 'Copy to an unmounted device invented something to copy into'

# And the assigns that do exist are untouched by that.
out=$(run Type SYS:C/a-file 2>&1) || fail "a real assign stopped working: $out"
[ "$out" = "ordinary" ] || fail "a real assign read back wrongly: $out"

printf 'ACE named the nodes, refused to open them, and did not stall doing it\n'
