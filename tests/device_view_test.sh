#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -ne 0 ] &&
   ! command -v sudo >/dev/null 2>&1; then
    printf 'ACE device-view test skipped (no noninteractive root helper)\n'
    exit 0
fi
if [ "$owner_uid" -ne 0 ] && ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE device-view test skipped (sudo needs authentication)\n'
    exit 0
fi

root_source=$(findmnt -n -o SOURCE -T /)
root_type=$(findmnt -n -o FSTYPE -T /)
case "$root_source:$root_type" in
    /dev/*:ext2|/dev/*:ext3|/dev/*:ext4|/dev/*:vfat) ;;
    *)
        printf 'ACE device-view test skipped (root is not a supported block filesystem)\n'
        exit 0
        ;;
esac
if ! findmnt -n /boot/efi >/dev/null 2>&1; then
    printf 'ACE device-view test skipped (no nested /boot/efi mount)\n'
    exit 0
fi
nested_source=$(findmnt -n -o SOURCE -T /boot/efi)
nested_type=$(findmnt -n -o FSTYPE -T /boot/efi)
case "$nested_source:$nested_type" in
    /dev/*:ext2|/dev/*:ext3|/dev/*:ext4|/dev/*:vfat) ;;
    *)
        printf 'ACE device-view test skipped (/boot/efi is not a supported block filesystem)\n'
        exit 0
        ;;
esac

test_dir=$(mktemp -d "$repo_dir/.ace-device-view.XXXXXX")
socket_path="$test_dir/broker.sock"
broker_pid=

as_root()
{
    if [ "$owner_uid" -eq 0 ]; then
        "$@"
    else
        sudo -n "$@"
    fi
}

cleanup()
{
    if [ -n "$broker_pid" ]; then
        as_root kill -TERM "$broker_pid" 2>/dev/null || true
    fi
    as_root rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'ACE device-view test: %s\n' "$1" >&2
    exit 1
}

as_root env ACE_MODE_OWNER_UID="$owner_uid" \
    ACE_BROKER_SOCKET="$socket_path" ACE_MOUNT_ROOT="$test_dir/mounts" \
    "$repo_dir/build/ace-broker" --root "$socket_path" &
launcher_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    kill -0 "$launcher_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'
broker_pid=$(as_root sed -n '1p' "$socket_path.lock")

root_alias=$(basename "$root_source")
resolved=$(as_root env ACE_MODE_VIEW=device ACE_MODE_OWNER_UID="$owner_uid" \
    ACE_BROKER_SOCKET="$socket_path" "$repo_dir/build/ace-brokerctl" \
    resolve "$root_alias:boot/efi") || fail 'underlying directory did not resolve'

root_device=$(stat -c %d /)
visible_device=$(stat -c %d /boot/efi)
hidden_device=$(as_root nsenter -t "$broker_pid" -m stat -c %d "$resolved")
[ "$hidden_device" = "$root_device" ] ||
    fail 'device view crossed into the nested /boot/efi filesystem'
[ "$visible_device" != "$root_device" ] ||
    fail 'test precondition failed: /boot/efi is not a separate filesystem'

visible_name=$(as_root env ACE_MODE_VIEW=device \
    ACE_MODE_OWNER_UID="$owner_uid" ACE_BROKER_SOCKET="$socket_path" \
    "$repo_dir/build/ace-brokerctl" name /boot/efi)
case "$visible_name" in "$root_alias:"*)
    fail 'the ordinary Linux path mapped back to the underlying root device' ;;
esac

printf 'ACE device view exposed a directory hidden by a nested mount\n'
