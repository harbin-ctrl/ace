#!/bin/sh
# A shell standing in a drawer that gets deleted underneath it.
#
# AmigaOS does not allow this: the current directory is held by a Lock, a Lock
# is a real reference, and Delete on a locked object answers "object is in
# use".  ACE has no such reference to offer -- Linux deletes a directory that
# processes are sitting in and says nothing -- so the situation arises here
# and the shell has to survive it.
#
# It did not.  The command-directory list Cli() rebuilds owns the locks that
# Shell.c installs as the current directory while it searches the path, and
# ACE's CurrentDir() keeps the caller's pointer rather than a copy.  The Shell
# normally puts the previous directory back before anything rebuilds that
# list; it cannot when that directory has been deleted, because the restore
# fails at the broker and returns without restoring.  The process was then
# left standing on a lock the list owned, the next Cli() freed it, and the
# read after that was of freed memory:
#
#     free(): invalid pointer
#     Aborted
#
# What this checks is only what a person would notice: the shell keeps
# running, and the commands after the deletion still work.  Exit 134 is the
# specific way it used to fail, and it is named so that a regression is
# recognisable rather than merely a number.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE deleted-cwd test skipped (must run as an ordinary user)\n'
    exit 0
fi

work=$(mktemp -d "$repo_dir/.ace-deleted-cwd.XXXXXX")
sys_dir="$work/sys"
socket_path="$work/broker.sock"
drawer=$(mktemp -d /tmp/ace-deleted-cwd.XXXXXX)
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
    rm -rf "$drawer" "$work"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'ACE deleted-cwd test: %s\n' "$1" >&2
    [ -n "${output-}" ] && printf 'shell output was:\n%s\n' "$output" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S"
for command in Echo CD Delete FailAt Dir List MakeDir; do
    [ -x "$repo_dir/build/$command" ] &&
        cp "$repo_dir/build/$command" "$sys_dir/C/"
done

drawer_name="RAM4:${drawer#/tmp/}"

# Deleted with the shell standing in it, and then asked to carry on: another
# command, a listing, and a move somewhere that still exists.  The commands
# after the deletion are the point -- the abort happened at the next prompt,
# so a test that stopped at the Delete would have passed while broken.
script="$work/script"
cat > "$script" <<SCRIPT
FailAt 100
CD $drawer_name
Delete $drawer_name ALL
Echo after-delete
Dir
Echo still-running
CD RAM4:
Echo recovered
SCRIPT

ACE_SYS_DIR="$sys_dir" "$repo_dir/build/ace-broker" "$socket_path" \
    >"$work/broker.log" 2>&1 &
broker_pid=$!
for _ in $(seq 1 400); do
    [ -S "$socket_path" ] && break
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'the broker did not start'

output=$(ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=deleted-cwd ACE_MODE_PRIVILEGE=user ACE_MODE_VIEW=mount \
    ACE_MODE_OWNER_UID="$owner_uid" \
    ACE_STARTUP_SCRIPT="rootfs:${script#/}" \
    "$repo_dir/build/ace-user-shell" </dev/null 2>&1)
status=$?

# 134 is 128+SIGABRT, which is exactly how this failed.
if [ "$status" -eq 134 ]; then
    fail 'the shell aborted after its current directory was deleted'
fi
case "$output" in
    *"free():"*|*"double free"*|*"Aborted"*|*"corruption"*)
        fail 'the shell reported a heap error after its current directory was deleted' ;;
esac

# And it really did keep going, rather than exiting quietly at the deletion.
for expected in after-delete still-running recovered; do
    case "$output" in
        *"$expected"*) ;;
        *) fail "the shell stopped before printing $expected" ;;
    esac
done

printf 'ACE survived having its current directory deleted underneath it\n'
