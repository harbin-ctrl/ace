#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-mode.XXXXXX")
socket_path="$test_dir/broker.sock"
broker_pid=

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
    printf 'ACE mode test: %s\n' "$1" >&2
    exit 1
}

broker="$repo_dir/build/ace-broker"
ctl="$repo_dir/build/ace-brokerctl"
root_mount=$($broker --root --mountview --print-socket)
root_device=$($broker --root --deviceview --print-socket)

[ "$($broker --root --print-socket)" = "$root_device" ] ||
    fail '--root did not default to device view'
if [ "$(id -u)" -eq 0 ]; then
    if $broker --user --print-socket >/dev/null 2>&1; then
        fail '--user print-socket succeeded under uid 0'
    fi
    [ "$($broker --print-socket)" = "$root_device" ] ||
        fail 'uid 0 did not default to root/device view'
else
    user_mount=$($broker --user --mountview --print-socket)
    [ "$($broker --user --print-socket)" = "$user_mount" ] ||
        fail '--user did not default to mount view'
    [ "$($broker --print-socket)" = "$user_mount" ] ||
        fail 'an ordinary user did not default to user/mount view'
    [ "$user_mount" != "$root_mount" ] ||
        fail 'user and root brokers have the same identity'
    case "$user_mount" in *-u"$(id -u)"-um-*) ;; *)
        fail "user/mount socket does not encode its mode: $user_mount" ;;
    esac
fi

[ "$root_mount" != "$root_device" ] ||
    fail 'mount and device-view brokers have the same identity'
case "$root_device" in *-u"$(id -u)"-rd-*) ;; *)
    fail "root/device socket does not retain the originating uid: $root_device" ;;
esac

if $broker --root --user --print-socket >/dev/null 2>&1; then
    fail '--root and --user were accepted together'
fi
if $broker --deviceview --mountview --print-socket >/dev/null 2>&1; then
    fail '--deviceview and --mountview were accepted together'
fi

if [ "$(id -u)" -ne 0 ]; then
    if $broker --user --deviceview "$test_dir/impossible.sock" \
        >/dev/null 2>&1; then
        fail 'an unprivileged device view started'
    fi
else
    if $broker --user --mountview "$test_dir/impossible.sock" \
        >/dev/null 2>&1; then
        fail '--user started under uid 0'
    fi
fi

privilege_switch=--user
privilege_name=user
if [ "$(id -u)" -eq 0 ]; then
    privilege_switch=--root
    privilege_name=root
fi
ACE_MODE_OWNER_UID="$(id -u)" ACE_BROKER_SOCKET="$socket_path" \
    $broker "$privilege_switch" --mountview "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.02
done
[ -S "$socket_path" ] || fail 'user/mount broker did not start'

status=$(ACE_MODE_VIEW=mount ACE_MODE_OWNER_UID="$(id -u)" \
    ACE_BROKER_SOCKET="$socket_path" "$ctl" status) ||
    fail 'matching client could not reach broker'
case "$status" in *"privilege"*"$privilege_name"*"view"*"mount"*) ;; *)
    fail "broker did not report its active mode: $status" ;;
esac

wrong_uid=$(( $(id -u) + 1 ))
if ACE_MODE_VIEW=mount ACE_MODE_OWNER_UID="$wrong_uid" \
    ACE_BROKER_SOCKET="$socket_path" "$ctl" status >/dev/null 2>&1; then
    fail 'broker accepted a client with a different originating uid'
fi

printf 'ACE privilege and filesystem-view mode tests passed\n'
