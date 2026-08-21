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

# Holding the capability, a class that is contracted but not yet built is
# answered differently from one that was never granted.  The distinction
# matters -- a broker built ahead of its mediator must be able to tell "you
# may not" from "not yet".  The volume class is implemented now, so the access
# class is what carries this.
out=$("$probe" unsupported "$mediator") || fail "unsupported probe failed: $out"
printf '%s\n' "$out" | grep -q '^access=6$' ||
    fail "expected UNSUPPORTED (6) for a granted but unbuilt class, got: $out"

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

# A channel name that is not a mediator rendezvous is refused before any
# connection is attempted.  The nonce is in the name; a name without one was
# not produced by a broker.
if "$mediator" "$test_dir/not-a-rendezvous.sock" 2>/dev/null; then
    fail "the mediator accepted a channel name with no nonce"
fi

printf 'ACE mediator channel test: ok\n'
