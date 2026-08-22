#!/bin/sh
# The device view: a volume seen as itself, with nothing mounted over it.
#
# A Linux mount can cover a directory, and the directory is still there --
# still on that device, still holding whatever it held.  AmigaDOS has no word
# for that arrangement: a volume is one filesystem and every name on it
# belongs to that filesystem.  The device view is how ACE keeps that true.
# Each device is mounted once, on its own, inside the fmm's private
# namespace, so the covered directory has somewhere to be seen.
#
# What this test pins down is that the view is reached *only* for objects the
# host's own mount tree cannot show.  An ordinary path on the same device must
# resolve to the ordinary host path -- the user can open that themselves, and
# routing it through the view would turn every file on the disk into a
# privileged request.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE device-view test skipped (must run as an ordinary user)\n'
    exit 0
fi
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE device-view test skipped (no noninteractive root helper)\n'
    exit 0
fi

root_source=$(findmnt -n -o SOURCE -T / 2>/dev/null || true)
root_type=$(findmnt -n -o FSTYPE -T / 2>/dev/null || true)
case "$root_source:$root_type" in
    /dev/*:ext2|/dev/*:ext3|/dev/*:ext4|/dev/*:vfat) ;;
    *)
        printf 'ACE device-view test skipped (root is not a supported block filesystem)\n'
        exit 0
        ;;
esac

# Any mount sitting on a directory of the root device will do.  The old
# version of this test wanted /boot/efi specifically and skipped everywhere
# else, which on most machines meant it never ran at all.  What it is really
# asking for is a covered directory, and every Linux system has several.
root_device=$(stat -c %d /)
covered=
for candidate in /run /proc /sys /dev /tmp /boot; do
    [ -d "$candidate" ] || continue
    findmnt -n "$candidate" >/dev/null 2>&1 || continue
    [ "$(stat -c %d "$candidate")" = "$root_device" ] && continue
    covered=$candidate
    break
done
if [ -z "$covered" ]; then
    printf 'ACE device-view test skipped (no mount covering a root-device directory)\n'
    exit 0
fi

test_dir=$(mktemp -d "$repo_dir/.ace-device-view.XXXXXX")
socket_path="$test_dir/broker.sock"
secret=/tmp/.ace-device-view-secret
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
    sudo -n rm -f "$secret" 2>/dev/null || true
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'ACE device-view test: %s\n' "$1" >&2
    exit 1
}

# Only the workers this test caused.
#
# Identified by the binary they were launched from, not by name alone: an ACE
# the user happens to have open runs the installed one, and a test that
# counted every root ace-fmm on the machine would be measuring that session
# too.  These tests assert things like "nothing privileged is running yet",
# which is a statement about this test's session and cannot be made about the
# machine.
#
# argv[0] rather than a -f match, because the sudo that launches a worker
# carries the same path on its own command line and is not itself a worker.
test_fmm_processes()
{
    for pid in $(pgrep -u 0 -x ace-fmm 2>/dev/null); do
        set -- $(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)
        [ "${1:-}" = "$repo_dir/build/ace-fmm" ] && printf '%s\n' "$pid"
    done
    return 0
}


# The broker is the user's own process, here as everywhere.  An earlier
# version of this test started it under sudo; that stopped being possible when
# running any part of ACE as root became a refusal, and the test skipped on
# every machine that would have caught it.
sudo -n sh -c "printf 'secret\n' > $secret && chmod 600 $secret" ||
    fail 'could not create the protected file'
ACE_BROKER_SOCKET="$socket_path" "$repo_dir/build/ace-broker" --root \
    "$socket_path" &
broker_pid=$!
for _ in $(seq 1 400); do
    [ -S "$socket_path" ] && break
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'the broker did not start'

ace()
{
    ACE_BROKER_SOCKET="$socket_path" ACE_MODE_PRIVILEGE=root \
        ACE_MODE_VIEW=device ACE_MODE_OWNER_UID="$owner_uid" "$@"
}
ctl() { ace "$repo_dir/build/ace-brokerctl" "$@"; }

# The view is built with the privilege, and the privilege arrives when
# something needs it.  Reading a root-only file is the shortest way to ask.
ace "$repo_dir/build/Type" "$(ctl name "$secret")" >/dev/null 2>&1 ||
    fail 'the session could not reach a protected file'

fmm_pid=$(test_fmm_processes | head -1 || true)
[ -n "$fmm_pid" ] || fail 'no fmm is running, so there is no device view'

root_alias=$(ctl name / | sed 's/:.*//')
[ -n "$root_alias" ] || fail 'the root device has no ACE name'

# An ordinary directory on the root device: the host can show it, so that is
# what ACE must name.  A view path here would be a file the user owns that
# only root can open.
ordinary=$(ctl resolve "$root_alias:home") ||
    fail 'an ordinary directory on the root device did not resolve'
case "$ordinary" in
    /run/ace-*|*/device-roots/*)
        fail "an ordinary directory was routed through the device view: $ordinary" ;;
esac
[ "$ordinary" = "/home" ] ||
    fail "an ordinary directory resolved to $ordinary, not /home"

# The covered directory: the host's tree cannot show it, and the view can.
# ${covered#/} because ACE names it relative to the volume root.
hidden=$(ctl resolve "$root_alias:${covered#/}") ||
    fail "the covered directory ${covered} did not resolve"
case "$hidden" in
    */device-roots/*) ;;
    *) fail "the covered directory did not resolve into the device view: $hidden" ;;
esac

# And it really is the underlying directory, not what is mounted over it: in
# the fmm's namespace that path is on the root device, while on the host the
# same name is on another one.
hidden_device=$(sudo -n nsenter -t "$fmm_pid" -m stat -c %d "$hidden") ||
    fail 'could not stat the covered directory inside the device view'
[ "$hidden_device" = "$root_device" ] ||
    fail 'the device view crossed into the mount that covers the directory'
[ "$(stat -c %d "$covered")" != "$root_device" ] ||
    fail "test precondition failed: $covered is not a separate filesystem"

# The covering mount is still its own volume, named as itself.
visible_name=$(ctl name "$covered") || fail "$covered has no ACE name"
case "$visible_name" in "$root_alias:"*)
    fail 'the covering mount mapped back to the underlying root device' ;;
esac

printf 'ACE showed a covered directory through the device view, and everything else directly\n'
