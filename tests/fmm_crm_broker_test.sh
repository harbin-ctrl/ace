#!/bin/sh
# The broker and the fmm together, as a session actually runs them.
#
# The device-view test next door checks a property of the view itself and
# skips wherever the machine has no nested mount to reveal -- which is most
# machines, including the one this was written on.  This test checks the thing
# that changed instead: that the broker is the user's own process, that the
# privilege lives in a separate root one, and that the two have the lifetime
# relationship they are supposed to have.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE fmm broker test skipped (must run as an ordinary user)\n'
    exit 0
fi
# pkexec would need a human at the keyboard.  Where sudo can be taken
# noninteractively the whole elevated path runs; where it cannot, say so
# rather than passing quietly.
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE fmm broker test skipped (no noninteractive root helper)\n'
    exit 0
fi

test_dir=$(mktemp -d "$repo_dir/.ace-fmm-broker.XXXXXX")
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
    printf 'ACE fmm broker test: %s\n' "$1" >&2
    exit 1
}

broker="$repo_dir/build/ace-broker"
ctl="$repo_dir/build/ace-brokerctl"

# Started as the user, with no sudo in front of it.  That is the change.
ACE_BROKER_SOCKET="$socket_path" "$broker" --root "$socket_path" &
broker_pid=$!
for _ in $(seq 1 400); do
    [ -S "$socket_path" ] && break
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'the broker did not start'

# It is the user's own process, and stays one.  A broker that had elevated
# itself would pass every other check in this file.
broker_uid=$(stat -c %u "/proc/$broker_pid")
[ "$broker_uid" = "$owner_uid" ] ||
    fail "the broker is running as uid $broker_uid, not $owner_uid"

# The privilege is somewhere else, and is real.
fmm_pid=
for _ in $(seq 1 200); do
    fmm_pid=$(pgrep -u 0 -x ace-fmm 2>/dev/null | head -1 || true)
    [ -n "$fmm_pid" ] && break
    sleep 0.05
done
[ -n "$fmm_pid" ] || fail 'no root fmm was started'
fmm_uid=$(stat -c %u "/proc/$fmm_pid")
[ "$fmm_uid" = "0" ] || fail "the fmm is not root (uid $fmm_uid)"

# The mount namespace is the fmm's, and the broker is not in it.  This is
# the property that made the two-process split necessary: an unprivileged
# process cannot enter one, so it must not need to.
broker_ns=$(readlink "/proc/$broker_pid/ns/mnt")
fmm_ns=$(sudo -n readlink "/proc/$fmm_pid/ns/mnt")
[ -n "$fmm_ns" ] || fail 'could not read the fmm namespace'
[ "$broker_ns" != "$fmm_ns" ] ||
    fail 'the broker is inside the fmm mount namespace'

# And the session works: the broker answers, with its DOS device list built
# from mounts it never made itself.
#
# Every request carries the session mode and the broker refuses one that
# disagrees with its own, so a client has to be told which session it is
# joining.  That check predates the fmm and is worth keeping: a client
# that thought it was in a different view would resolve paths against a
# topology the broker is not serving.
ace_ctl()
{
    ACE_BROKER_SOCKET="$socket_path" ACE_MODE_PRIVILEGE=root \
        ACE_MODE_VIEW=device ACE_MODE_OWNER_UID="$owner_uid" "$ctl" "$@"
}

ace_ctl status >/dev/null ||
    fail 'the broker did not answer a status request'
ace_ctl doslist >/dev/null ||
    fail 'the broker could not produce its DOS device list'
ace_ctl status | grep -q '^privilege	root$' ||
    fail 'the broker did not report itself as an authorised session'
ace_ctl status | grep -q '^view	device$' ||
    fail 'the broker did not report a device view'

# When the broker goes, the fmm goes.  Nobody signals it: it is a root
# process and the broker holds no lever on it, so what ends it is the EOF on
# the channel that the broker's exit produces.
kill -TERM "$broker_pid"
for _ in $(seq 1 400); do
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.01
done
gone=0
for _ in $(seq 1 400); do
    if ! kill -0 "$fmm_pid" 2>/dev/null; then
        gone=1
        break
    fi
    sleep 0.01
done
broker_pid=
[ "$gone" -eq 1 ] ||
    fail "fmm $fmm_pid outlived the broker that authorised it"

# Running ACE as root is refused rather than accommodated: a root shell would
# have root's session bus, configuration and HOME, and would be a different
# user's desktop wearing this one's name.
if sudo -n "$broker" --root "$test_dir/refused.sock" \
        >"$test_dir/asroot.out" 2>&1; then
    fail 'the broker started as root'
fi
grep -q 'normal user' "$test_dir/asroot.out" ||
    fail "running as root failed without explaining why: $(cat "$test_dir/asroot.out")"

printf 'ACE broker ran as the user while a root fmm held the privilege\n'
