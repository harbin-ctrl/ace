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
volume=${drawer_name%%:*}

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

# --------------------------------------------------------------------------
# And it is somewhere sensible afterwards.
#
# Surviving is not enough on its own: a session left holding the name of a
# drawer that is gone goes on printing it at the prompt, and the next relative
# name resolved against it lands somewhere else with nothing about it looking
# wrong.  The broker walks up to the nearest ancestor that still exists, which
# is what a person would do by hand.
# --------------------------------------------------------------------------
mkdir -p "$drawer/a/b/c" || fail 'could not build the nested drawer'

settle_script="$work/settle.script"
cat > "$settle_script" <<SCRIPT
FailAt 100
CD $drawer_name/a/b/c
Delete $drawer_name/a/b/c ALL
Echo @@one-level
CD
CD $drawer_name/a
Delete $drawer_name ALL
Echo @@all-the-way
CD
Echo @@listing
Dir
SCRIPT

output=$(ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=deleted-cwd-settle ACE_MODE_PRIVILEGE=user \
    ACE_MODE_VIEW=mount ACE_MODE_OWNER_UID="$owner_uid" \
    ACE_STARTUP_SCRIPT="rootfs:${settle_script#/}" \
    "$repo_dir/build/ace-user-shell" </dev/null 2>&1)

answer_after()
{
    printf '%s\n' "$output" |
        awk -v want="@@$1" '$0==want{grab=1;next} /^@@/{grab=0} grab && NF {print; exit}'
}

# One level: the drawer it was standing in is gone, its parent is not.
got=$(answer_after one-level)
[ "$got" = "$drawer_name/a/b" ] ||
    fail "after deleting the current drawer the session was at [$got], wanted [$drawer_name/a/b]"

# Several levels at once: everything up to the volume root has gone, so that
# is where it has to stop -- a mounted volume's root exists.
got=$(answer_after all-the-way)
[ "$got" = "$volume:" ] ||
    fail "after deleting the whole tree the session was at [$got], wanted [$volume:]"

# And a relative command really does act there, rather than wherever the
# shell's own process happened to be left.  This is the symptom that was
# reported: a Dir after the deletion listed an unrelated directory.
listing=$(printf '%s\n' "$output" | awk '/^@@listing$/{grab=1;next} grab')
case "$listing" in
    *"could not"*|*"not found"*)
        fail "a listing after the deletion failed: $listing" ;;
esac

printf 'ACE survived having its current directory deleted underneath it\n'
