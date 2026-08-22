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
# In a directory the user can traverse but a file the user cannot read: the
# cheapest thing that makes a session need its privilege at all.
secret=/tmp/.ace-fmm-broker-test-secret
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

ace_ctl()
{
    ACE_BROKER_SOCKET="$socket_path" ACE_MODE_PRIVILEGE=root \
        ACE_MODE_VIEW=device ACE_MODE_OWNER_UID="$owner_uid" "$ctl" "$@"
}

# Nothing privileged has happened yet, and so nothing privileged is running.
# Elevation is lazy: --root says a session may ask, not that it has.
pgrep -u 0 -x ace-fmm >/dev/null 2>&1 &&
    fail 'a root fmm was started before anything needed one'

# Ask for something the user cannot do, which is what starts the fmm.
sudo -n sh -c "printf 'top-secret\n' > $secret && chmod 600 $secret" ||
    fail 'could not create the protected file'
if cat "$secret" >/dev/null 2>&1; then
    fail 'the test user can read the protected file, so it proves nothing'
fi
name=$(ace_ctl name "$secret") || fail 'the protected file has no Amiga name'
ACE_BROKER_SOCKET="$socket_path" ACE_MODE_PRIVILEGE=root \
    ACE_MODE_VIEW=device ACE_MODE_OWNER_UID="$owner_uid" \
    "$repo_dir/build/Type" "$name" >/dev/null 2>&1 ||
    fail 'the session could not reach the protected file'

# The privilege is somewhere else, and is real.
fmm_pid=$(pgrep -u 0 -x ace-fmm 2>/dev/null | head -1 || true)
[ -n "$fmm_pid" ] || fail 'no root fmm was started'
fmm_uid=$(stat -c %u "/proc/$fmm_pid")
[ "$fmm_uid" = "0" ] || fail "the fmm is not root (uid $fmm_uid)"

# And it is more than one process.  The supervisor is the broker's only
# authenticated peer and performs no privileged operation itself: the mounts
# and the file opens belong to workers it forked, which is what makes a
# compromised file worker unable to mount anything.
fmm_count=$(pgrep -u 0 -x ace-fmm 2>/dev/null | wc -l)
[ "$fmm_count" -ge 2 ] ||
    fail "the fmm is a single process ($fmm_count); the workers were not split out"
for pid in $(pgrep -u 0 -x ace-fmm 2>/dev/null); do
    [ "$pid" = "$fmm_pid" ] && continue
    parent=$(sudo -n awk '{print $4}' "/proc/$pid/stat")
    [ "$parent" = "$fmm_pid" ] ||
        fail "root fmm $pid is not a child of the supervisor $fmm_pid"
done

# Counting sockets is what tells this design from the one before it, where the
# broker's peer was itself the volume worker and held one channel.  Here the
# supervisor holds two -- the broker's, and the private one to the volume
# worker it forwards across -- and every worker holds exactly the one channel
# it exists to serve.  A worker with two would be a worker with a route to
# something that is not its business.
sockets_held()
{
    sudo -n ls -l "/proc/$1/fd" 2>/dev/null | grep -c 'socket:' || true
}
held=$(sockets_held "$fmm_pid")
[ "$held" = "2" ] ||
    fail "the supervisor holds $held channels, not the broker's and the volume worker's"
for pid in $(pgrep -u 0 -x ace-fmm 2>/dev/null); do
    [ "$pid" = "$fmm_pid" ] && continue
    held=$(sockets_held "$pid")
    [ "$held" = "1" ] ||
        fail "root worker $pid holds $held channels, not the one it serves"
done

# The mount namespace is the fmm's, and the broker is not in it.  This is
# the property that made the two-process split necessary: an unprivileged
# process cannot enter one, so it must not need to.
broker_ns=$(readlink "/proc/$broker_pid/ns/mnt")
fmm_ns=$(sudo -n readlink "/proc/$fmm_pid/ns/mnt")
[ -n "$fmm_ns" ] || fail 'could not read the fmm namespace'
[ "$broker_ns" != "$fmm_ns" ] ||
    fail 'the broker is inside the fmm mount namespace'

# One namespace, made once, before either worker existed.  Two workers that
# had each unshared for themselves would hold two private views of the disks
# that looked identical and were not -- and the file worker's would be the one
# without the device the volume worker mounted.
for pid in $(pgrep -u 0 -x ace-fmm 2>/dev/null); do
    worker_ns=$(sudo -n readlink "/proc/$pid/ns/mnt")
    [ "$worker_ns" = "$fmm_ns" ] ||
        fail "root fmm $pid is in its own mount namespace, not the session's"
done

# And the session works: the broker answers, with its DOS device list built
# from mounts it never made itself.
#
# Every request carries the session mode and the broker refuses one that
# disagrees with its own, so a client has to be told which session it is
# joining.  That check predates the fmm and is worth keeping: a client
# that thought it was in a different view would resolve paths against a
# topology the broker is not serving.
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
    # Every one of them, not just the supervisor: a worker that outlived its
    # supervisor would be a root process with a channel and no owner.
    if ! pgrep -u 0 -x ace-fmm >/dev/null 2>&1; then
        gone=1
        break
    fi
    sleep 0.01
done
broker_pid=
[ "$gone" -eq 1 ] ||
    fail 'a root fmm outlived the broker that authorised it'

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
