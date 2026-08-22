#!/bin/sh
# What a directory entry is called.
#
# A FileInfoBlock carries the entry's own name, and nothing else belongs
# there.  Two ways a volume name used to leak into one, both from taking the
# text after the last slash of a volume-qualified name:
#
#   - an entry directly inside a volume root has no slash after the colon, so
#     "RAM:dbus" survived whole and every entry in the root wore the volume;
#   - an entry with a filesystem mounted on it maps to that filesystem, so it
#     listed as "DEVTMPFS:" -- a mountpoint wearing a volume's name.
#
# The second is the one worth a test of its own.  AmigaDOS has no word for a
# mountpoint: a volume is one filesystem, every name on it belongs to that
# filesystem, and a directory with something mounted over it is just a
# directory on the volume that holds it.  Whatever is mounted there is a
# separate volume, reached by its own name and by no other route.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-dir-volume-root.XXXXXX")
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
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'Dir volume-root test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$runtime_dir"
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
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=dir-volume-root \
        "$repo_dir/build/$@"
}

# Whatever volumes this machine has; the test asks rather than assuming any
# particular one exists.
volume=$(run ace-brokerctl doslist 2>/dev/null | awk -F'\t' '$4 != "" {print $4; exit}')
[ -n "$volume" ] || fail 'no volume to list'

run ace-brokerctl cd "$volume:" >/dev/null 2>&1 ||
    fail "could not enter the root of $volume:"
listing=$(run Dir 2>&1) || fail "Dir failed in $volume: -- $listing"
[ -n "$listing" ] || fail "Dir listed nothing at all in $volume:"

# Every name in the listing is a name on this volume.  A colon is what makes a
# name a volume rather than an entry, so no entry may carry one.
offenders=$(printf '%s\n' "$listing" | tr -s ' ' '\n' | grep ':' || true)
if [ -n "$offenders" ]; then
    printf '%s\n' "$listing" >&2
    fail "entries in $volume: are wearing volume names: $(printf '%s' "$offenders" | tr '\n' ' ')"
fi

# And a mountpoint is listed by the name the holding volume has for it.  Only
# checked where the machine actually has one inside the volume being listed.
host_root=$(run ace-brokerctl resolve "$volume:" 2>/dev/null)
if [ -n "$host_root" ] && command -v findmnt >/dev/null 2>&1; then
    root_device=$(stat -c %d "$host_root" 2>/dev/null || true)
    for candidate in "$host_root"/*; do
        [ -d "$candidate" ] || continue
        findmnt -n "$candidate" >/dev/null 2>&1 || continue
        [ "$(stat -c %d "$candidate" 2>/dev/null)" = "$root_device" ] && continue
        base=$(basename "$candidate")
        printf '%s\n' "$listing" | grep -qw -- "$base" ||
            fail "the mounted-over directory $base is not listed under its own name"
        break
    done
fi

printf 'Dir named every entry after the volume it is actually on\n'
