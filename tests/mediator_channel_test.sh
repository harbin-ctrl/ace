#!/bin/sh
# The mediator channel: handshake, capability classes, and lifetime.
#
# Nothing here needs privilege.  The probe starts the mediator as the current
# user and tells the client to expect that uid, which exercises every part of
# the channel except the one thing only root can demonstrate.  A test that
# required pkexec would need a human to type a password, which means it would
# be skipped, which means the channel root speaks over would be the untested
# part of ACE.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-mediator.XXXXXX")

cleanup()
{
    # Anything the probe left behind is a bug worth failing over elsewhere,
    # but it must not be left running here.
    pkill -u "$(id -u)" -f "$repo_dir/build/ace-mediator " 2>/dev/null || true
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'ACE mediator channel test: %s\n' "$1" >&2
    exit 1
}

mediator="$repo_dir/build/ace-mediator"
probe="$repo_dir/build/mediator-channel-probe"

[ -x "$mediator" ] || fail "ace-mediator was not built"
[ -x "$probe" ] || fail "mediator-channel-probe was not built"

# The socket lives under XDG_RUNTIME_DIR, so give the test its own rather than
# writing into the real session's.
XDG_RUNTIME_DIR="$test_dir"
export XDG_RUNTIME_DIR

# A handshake that succeeds grants exactly what was asked for, answers a ping,
# and knows which process it is talking to.
out=$("$probe" handshake "$mediator") || fail "handshake probe failed: $out"
printf '%s\n' "$out" | grep -q '^granted=0x3$' ||
    fail "expected both capabilities granted, got: $out"
printf '%s\n' "$out" | grep -q '^authorisation=0$' ||
    fail "expected session-scoped authorisation (0), got: $out"
printf '%s\n' "$out" | grep -q '^ping=ok$' || fail "ping failed: $out"
printf '%s\n' "$out" | grep -q '^pid=known$' || fail "peer pid unknown: $out"
printf '%s\n' "$out" | grep -q '^closed$' || fail "close failed: $out"

# A capability that was not asked for is not granted, and a request from the
# class it would have covered is refused -- on the class check, before the
# mediator interprets anything the request carried.
out=$("$probe" partial "$mediator") || fail "partial probe failed: $out"
printf '%s\n' "$out" | grep -q '^granted=0x2$' ||
    fail "expected access-only grant, got: $out"
printf '%s\n' "$out" | grep -q '^volume=1$' ||
    fail "expected REFUSED (1) for an ungranted class, got: $out"

# Holding the capability, an operation that is contracted but not yet built is
# answered differently from a class that was never granted.  The distinction
# matters -- a broker built ahead of its mediator must be able to tell "you
# may not" from "not yet".  The opens are implemented now, so the operations
# still to come carry this.
out=$("$probe" unsupported "$mediator") || fail "unsupported probe failed: $out"
printf '%s\n' "$out" | grep -q '^access=6$' ||
    fail "expected UNSUPPORTED (6) for a granted but unbuilt operation, got: $out"

# Giving privilege up mid-session works, and the channel is spent afterwards.
out=$("$probe" drop "$mediator") || fail "drop probe failed: $out"
printf '%s\n' "$out" | grep -q '^drop=ok$' || fail "drop failed: $out"
printf '%s\n' "$out" | grep -q '^ping-after=failed$' ||
    fail "channel still answered after privilege was dropped: $out"

# A mediator that dies is reported to the broker as a failed request, not as a
# hang and not as a crash.  This is the case a real session meets when polkit
# times out or the mediator is killed underneath it.
out=$("$probe" death "$mediator") || fail "death probe failed: $out"
printf '%s\n' "$out" | grep -q '^ping-after-death=failed$' ||
    fail "ping claimed success after the mediator died: $out"
printf '%s\n' "$out" | grep -q '^survived$' ||
    fail "the client did not survive its mediator dying: $out"

# A broker that crashes sends nothing at all.  The mediator has to notice the
# EOF and go: a root process outliving the session that authorised it is
# precisely what session-scoped authorisation exists to prevent.
out=$("$probe" abandon "$mediator") || fail "abandon probe failed: $out"
abandoned=$(printf '%s\n' "$out" | sed -n 's/^abandoned=\([0-9][0-9]*\)$/\1/p')
[ -n "$abandoned" ] || fail "abandon probe did not start a mediator: $out"
# Name the process rather than counting them, so this cannot pass by watching
# the wrong thing disappear -- or by watching nothing at all.
gone=0
for _ in $(seq 1 200); do
    if ! kill -0 "$abandoned" 2>/dev/null; then
        gone=1
        break
    fi
    sleep 0.01
done
[ "$gone" -eq 1 ] ||
    fail "mediator $abandoned outlived the broker that authorised it"

# The volume class refuses what it should, and refuses it before asking the
# kernel anything.  These results are identical with and without privilege,
# which is the point: they are decided by validation, not by whether the mount
# would have worked.
check_volume_refusals()
{
    printf '%s\n' "$1" | grep -q '^badname=2$' ||
        fail "a name that is a path was not refused as an escape: $1"
    printf '%s\n' "$1" | grep -q '^dotted=2$' ||
        fail "a device name containing a dot was accepted: $1"
    printf '%s\n' "$1" | grep -q '^badtype=6$' ||
        fail "an unsupported filesystem type was not refused: $1"
    printf '%s\n' "$1" | grep -q '^unterminated=7$' ||
        fail "an unterminated payload was not a protocol error: $1"
    printf '%s\n' "$1" | grep -q '^unmount-unknown=4$' ||
        fail "unmounting an unknown device did not report a host error: $1"
    printf '%s\n' "$1" | grep -q '^list=0 entries=0$' ||
        fail "an empty volume list did not come back empty: $1"
}

out=$("$probe" volume "$mediator") || fail "volume probe failed: $out"
check_volume_refusals "$out"
# Without the privilege to create a namespace there is no namespace, and a
# mount request is refused for that reason rather than attempted and failed.
printf '%s\n' "$out" | grep -q '^namespace=4$' ||
    fail "expected the namespace to be unavailable unprivileged: $out"
printf '%s\n' "$out" | grep -q '^absent=1$' ||
    fail "expected REFUSED before a namespace exists, got: $out"

# Given a namespace, the same requests are answered differently: the missing
# device becomes a host error rather than a refusal.  Run under a user
# namespace where one is available, and say so plainly when it is not -- a
# skipped check should be visible, not silent.
if unshare -Ur -m true 2>/dev/null; then
    out=$(unshare -Ur -m "$probe" volume "$mediator") ||
        fail "volume probe failed in a user namespace: $out"
    check_volume_refusals "$out"
    printf '%s\n' "$out" | grep -q '^namespace=0$' ||
        fail "the private mount namespace was not created: $out"
    printf '%s\n' "$out" | grep -q '^absent=4$' ||
        fail "expected a host error for a missing device, got: $out"
else
    printf 'ACE mediator channel test: no user namespaces here, '
    printf 'namespace creation not covered\n'
fi

# The two personalities are two processes.
#
# A namespace is not required to have one: a worker without a device view is
# still the right worker for protected host paths, and refusing to make one
# would mean escalation only worked for sessions that had also asked for a
# device view.  What such a worker cannot do is resolve a view-domain path,
# and it says so rather than inventing a root to resolve it under.
out=$("$probe" access "$mediator") || fail "access probe failed: $out"
printf '%s\n' "$out" | grep -q '^spawn=ok$' ||
    fail "no access worker without a namespace: $out"
printf '%s\n' "$out" | grep -q '^worker-rootless=1$' ||
    fail "a worker with no device view resolved a view-domain path: $out"
printf '%s\n' "$out" | grep -q '^worker-volume=1$' ||
    fail "the access worker did not refuse a volume operation: $out"

if unshare -Ur -m true 2>/dev/null; then
    out=$(unshare -Ur -m "$probe" access "$mediator") ||
        fail "access probe failed in a user namespace: $out"
    printf '%s\n' "$out" | grep -q '^spawn=ok$' ||
        fail "the access worker did not start: $out"
    # A different process, not a second branch in the same one.
    printf '%s\n' "$out" | grep -q '^separate=yes$' ||
        fail "the access worker is not a separate process: $out"
    # Inside the volume worker's namespace, which is the only way it can see
    # the device view at all.
    printf '%s\n' "$out" | grep -q '^same-namespace=yes$' ||
        fail "the access worker is outside the mount namespace: $out"
    # And that namespace is private: the unprivileged side is outside it.
    printf '%s\n' "$out" | grep -q '^private=yes$' ||
        fail "the mount namespace is not private: $out"
    printf '%s\n' "$out" | grep -q '^worker-ping=ok$' ||
        fail "the access worker did not answer: $out"
    # It holds no channel to the volume side, and says so as well.
    printf '%s\n' "$out" | grep -q '^worker-volume=1$' ||
        fail "the access worker did not refuse a volume operation: $out"
    # With no view prepared there is no subtree to resolve inside, and the
    # worker declines rather than falling back to somewhere it invented.
    printf '%s\n' "$out" | grep -q '^worker-rootless=1$' ||
        fail "the access worker resolved a path with no view root: $out"
    # Closing one channel must not disturb the other.
    printf '%s\n' "$out" | grep -q '^volume-alive=ok$' ||
        fail "closing the access worker broke the volume channel: $out"
fi

# What the access worker will open, and what it will not.
#
# This is the mechanism the whole design rests on: a root process opens the
# object, an unprivileged one reads it through the descriptor that comes back,
# and no unprivileged process ever enters the namespace or holds anything
# wider than a handle to one file.
if unshare -Ur -m true 2>/dev/null; then
    mount_root=$(mktemp -d "$test_dir/mounts.XXXXXX")
    out=$(ACE_MOUNT_ROOT="$mount_root" unshare -Ur -m "$probe" openat \
              "$mediator") || fail "openat probe failed: $out"
    printf '%s\n' "$out" | grep -q '^prepare=0$' ||
        fail "the view root was not prepared: $out"
    printf '%s\n' "$out" | grep -q '^read=0$' ||
        fail "the access worker would not open an ordinary file: $out"
    # Opened there, read here.
    printf '%s\n' "$out" | grep -q '^content=ok$' ||
        fail "the passed descriptor did not read back the file: $out"
    printf '%s\n' "$out" | grep -q '^dotdot=2$' ||
        fail "a .. path was not refused as an escape: $out"
    # An absolute path is not a thing this interface does, so it is refused as
    # malformed rather than quietly trimmed into something it might accept.
    printf '%s\n' "$out" | grep -q '^absolute=7$' ||
        fail "an absolute path was not refused as malformed: $out"
    # The case that separates resolving-with-constraints from checking a
    # string: "out/passwd" looks harmless and out is a symlink to /etc.
    printf '%s\n' "$out" | grep -q '^symlink=2$' ||
        fail "a symlink out of the tree was followed: $out"
    printf '%s\n' "$out" | grep -q '^dir=0$' ||
        fail "a directory would not open for enumeration: $out"
    printf '%s\n' "$out" | grep -q '^notdir=4$' ||
        fail "opening a file as a directory did not report a host error: $out"
    printf '%s\n' "$out" | grep -q '^stat=0$' ||
        fail "metadata could not be reached: $out"
    printf '%s\n' "$out" | grep -q '^size=ok$' ||
        fail "the O_PATH descriptor did not fstat correctly: $out"
fi

# The elevated path, on machines where root can be taken without a human.
#
# Everything above runs the mediator as the current user, which keeps the
# tests free of passwords but leaves the path that actually carries privilege
# uncovered.  This runs it for real: a root mediator, a private namespace, an
# access worker inside it, and both path domains.
if command -v sudo >/dev/null 2>&1 && sudo -n /usr/bin/true 2>/dev/null &&
   [ "$(id -u)" -ne 0 ]; then
    out=$("$probe" elevated "$mediator") || fail "elevated probe failed: $out"
    printf '%s\n' "$out" | grep -q '^granted=0x3$' ||
        fail "the elevated mediator granted nothing: $out"
    printf '%s\n' "$out" | grep -q '^namespace=0$' ||
        fail "the elevated mediator made no namespace: $out"
    printf '%s\n' "$out" | grep -q '^worker=ok$' ||
        fail "no access worker inside the elevated namespace: $out"
    printf '%s\n' "$out" | grep -q '^escape=2$' ||
        fail "a device-view escape was not refused: $out"

    # Escalation reaches a real object the user genuinely cannot open.  Both
    # halves matter: the second is what makes the first mean anything.
    printf '%s\n' "$out" | grep -q '^shadow=0$' ||
        fail "the access worker could not open a protected file: $out"
    printf '%s\n' "$out" | grep -q '^shadow-direct=refused$' ||
        fail "the test user could open /etc/shadow, so it proves nothing: $out"

    # A file created through the mediator is root-owned, as sudo cp leaves it.
    printf '%s\n' "$out" | grep -q '^create=0$' ||
        fail "the access worker could not create a protected file: $out"
    printf '%s\n' "$out" | grep -q '^owner=root$' ||
        fail "a file created through the mediator was not root-owned: $out"

    # The operations with no descriptor to hand back.
    printf '%s\n' "$out" | grep -q '^rename=0$' || fail "rename failed: $out"
    printf '%s\n' "$out" | grep -q '^unlink=0$' || fail "unlink failed: $out"
    printf '%s\n' "$out" | grep -q '^mkdir=0$' || fail "mkdir failed: $out"
    # AmigaDOS Delete removes a directory too, so one DOS operation stays one
    # operation here.
    printf '%s\n' "$out" | grep -q '^rmdir=0$' ||
        fail "Delete did not remove a directory: $out"
    # A final component that is really a path reference is refused before any
    # part of it is used.
    printf '%s\n' "$out" | grep -q '^dotname=7$' ||
        fail "a directory reference was accepted as a name: $out"
fi

# A channel name that is not a mediator rendezvous is refused before any
# connection is attempted.  The nonce is in the name; a name without one was
# not produced by a broker.
if "$mediator" "$test_dir/not-a-rendezvous.sock" 2>/dev/null; then
    fail "the mediator accepted a channel name with no nonce"
fi

printf 'ACE mediator channel test: ok\n'
