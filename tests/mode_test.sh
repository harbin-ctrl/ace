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

if [ "$(id -u)" -eq 0 ]; then
    if $broker --print-socket >/dev/null 2>&1 ||
       $broker --root --print-socket >/dev/null 2>&1; then
        fail 'ACE accepted a root-owned session'
    fi
else
    user_mount=$($broker --print-socket)
    root_device=$($broker --root --print-socket)
    [ "$user_mount" != "$root_device" ] ||
        fail 'user and root authorization sockets have the same identity'
    case "$user_mount" in *-u"$(id -u)"-um-*) ;; *)
        fail "user/mount socket does not encode its mode: $user_mount" ;;
    esac
    case "$root_device" in *-u"$(id -u)"-rd-*) : ;; *)
        fail "root authorization socket does not retain the originating uid: $root_device" ;;
    esac
    for retired in --user --deviceview --mountview; do
        if $broker "$retired" --print-socket >/dev/null 2>&1; then
            fail "$retired was still accepted"
        fi
    done
fi

ACE_MODE_OWNER_UID="$(id -u)" ACE_BROKER_SOCKET="$socket_path" \
    $broker "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.02
done
[ -S "$socket_path" ] || fail 'user/mount broker did not start'

status=$(ACE_MODE_PRIVILEGE=user ACE_MODE_VIEW=mount \
    ACE_MODE_OWNER_UID="$(id -u)" ACE_BROKER_SOCKET="$socket_path" \
    "$ctl" status) || fail 'matching client could not reach broker'
case "$status" in *"privilege"*user*"view"*mount*) ;; *)
    fail "broker did not report its active mode: $status" ;;
esac

wrong_uid=$(( $(id -u) + 1 ))
if ACE_MODE_VIEW=mount ACE_MODE_OWNER_UID="$wrong_uid" \
    ACE_BROKER_SOCKET="$socket_path" "$ctl" status >/dev/null 2>&1; then
    fail 'broker accepted a client with a different originating uid'
fi

printf 'ACE authorization policy and default-view tests passed\n'
