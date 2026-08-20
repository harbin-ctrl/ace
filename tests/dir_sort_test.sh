#!/bin/sh
set -eu

# Confirms src/dir.c: Dir must sort subdirectories the same
# way it already sorts files, and ALL must recurse into a subdirectory tree
# even when FILES restricts what gets printed. Both were checked against a
# real Amiga 1200 under emulation before being fixed; see HANDOFF.md.

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-dir-sort.XXXXXX")
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
    printf 'dir sort test: %s\n' "$1" >&2
    exit 1
}

work="$sys_dir/C"
mkdir -p "$work" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" "$runtime_dir"

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
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=dir-sort \
        "$repo_dir/build/$command_name" "$@"
}

# A directory tree named so that raw filesystem order (creation order, or an
# ext4 hash) would not coincidentally match alphabetical order: subdir first,
# then empty, then Sub2 -- case-insensitive alphabetical is empty, Sub2,
# subdir, a different order from all three of creation order, reverse
# creation order, and the plain ASCII case-sensitive order.
mkdir -p "$work/subdir" "$work/empty" "$work/Sub2"
printf 'a\n' > "$work/alpha.txt"
printf 'i\n' > "$work/subdir/inner.txt"
printf 'd\n' > "$work/subdir/deeper.txt"
printf 'o\n' > "$work/Sub2/one.txt"

listing=$(run_command Dir SYS:C ALL)

# Directories must appear in case-insensitive alphabetical order: empty
# before Sub2 before subdir.
empty_line=$(printf '%s\n' "$listing" | grep -n 'empty (dir)' | cut -d: -f1)
sub2_line=$(printf '%s\n' "$listing" | grep -n 'Sub2 (dir)' | cut -d: -f1)
subdir_line=$(printf '%s\n' "$listing" | grep -n 'subdir (dir)' | cut -d: -f1)
[ -n "$empty_line" ] && [ -n "$sub2_line" ] && [ -n "$subdir_line" ] ||
    fail 'not all three directories were listed'
[ "$empty_line" -lt "$sub2_line" ] ||
    fail 'empty did not sort before Sub2'
[ "$sub2_line" -lt "$subdir_line" ] ||
    fail 'Sub2 did not sort before subdir'

# ALL FILES must still recurse into the subdirectories, even though FILES
# alone would otherwise suppress the directory-walk that shows them.
files_only=$(run_command Dir SYS:C ALL FILES)
case "$files_only" in
    *inner.txt*) ;;
    *) fail 'ALL FILES did not recurse into subdir' ;;
esac
case "$files_only" in
    *one.txt*) ;;
    *) fail 'ALL FILES did not recurse into Sub2' ;;
esac

printf 'Dir sort and recurse test passed\n'
