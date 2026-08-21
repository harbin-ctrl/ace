#!/bin/sh
# An ordinary ACE command reaching a file only root can reach.
#
# This is the user-facing end of the whole fmm design, and the only test
# that exercises the entire path in one go: an unprivileged command, the
# shared DOS seam, the user's broker, a root fmm, an CRM, a
# descriptor passed back, and the bytes read by the command that asked.
#
# It also checks the half that makes the other half mean anything -- that the
# same user, without ACE, genuinely cannot open the file.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE privileged file test skipped (must run as an ordinary user)\n'
    exit 0
fi
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE privileged file test skipped (no noninteractive root helper)\n'
    exit 0
fi

test_dir=$(mktemp -d "$repo_dir/.ace-privileged.XXXXXX")
socket_path="$test_dir/broker.sock"
secret=/tmp/.ace-privileged-test-secret
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
    printf 'ACE privileged file test: %s\n' "$1" >&2
    exit 1
}

# A file in a directory the user can traverse, but which the user cannot read.
sudo -n sh -c "printf 'top-secret\n' > $secret && chmod 600 $secret" ||
    fail 'could not create the protected file'
# The premise.  Without this the rest proves nothing: a readable file would
# pass every check below.
if cat "$secret" >/dev/null 2>&1; then
    fail 'the test user can read the protected file, so it proves nothing'
fi

start_broker()
{
    broker_args=
    [ "${1-}" = "--root" ] && broker_args=--root
    ACE_BROKER_SOCKET="$socket_path" "$repo_dir/build/ace-broker" \
        ${broker_args:+'--root'} "$socket_path" &
    broker_pid=$!
    for _ in $(seq 1 400); do
        [ -S "$socket_path" ] && return 0
        kill -0 "$broker_pid" 2>/dev/null || return 1
        sleep 0.05
    done
    return 1
}

stop_broker()
{
    [ -n "$broker_pid" ] || return 0
    kill -TERM "$broker_pid" 2>/dev/null || true
    for _ in $(seq 1 400); do
        kill -0 "$broker_pid" 2>/dev/null || break
        sleep 0.01
    done
    broker_pid=
    rm -f "$socket_path" "$socket_path.lock"
}

ace()
{
    privilege=$1
    shift
    view=mount
    [ "$privilege" = root ] && view=device
    ACE_BROKER_SOCKET="$socket_path" ACE_MODE_PRIVILEGE="$privilege" \
        ACE_MODE_VIEW="$view" ACE_MODE_OWNER_UID="$owner_uid" "$@"
}

# An authorised session reaches it.
start_broker --root || fail 'the broker did not start'
name=$(ace root "$repo_dir/build/ace-brokerctl" name "$secret") ||
    fail 'the protected file has no Amiga name'
[ -n "$name" ] || fail 'the protected file resolved to an empty name'

output=$(ace root "$repo_dir/build/Type" "$name" 2>&1) ||
    fail "Type could not read the protected file: $output"
[ "$output" = "top-secret" ] ||
    fail "Type returned the wrong contents: $output"

# The fmm is what made that possible, and it is a root process.
fmm_pid=$(pgrep -u 0 -x ace-fmm 2>/dev/null | head -1 || true)
[ -n "$fmm_pid" ] ||
    fail 'the file was read without a root fmm, which should be impossible'

# Delete needs privilege too: /tmp is sticky, so the user cannot remove a file
# owned by root even though the directory is writable.
ace root "$repo_dir/build/Delete" "$name" >/dev/null 2>&1 ||
    fail 'Delete could not remove the protected file'
if sudo -n test -e "$secret"; then
    fail 'Delete reported success and the file is still there'
fi
stop_broker

# Without --root the same operation is refused, and the refusal is the
# ordinary one the user would have had with no fmm at all.
sudo -n sh -c "printf 'top-secret\n' > $secret && chmod 600 $secret"
start_broker || fail 'the unprivileged broker did not start'
if ace user "$repo_dir/build/Type" "$name" >/dev/null 2>&1; then
    fail 'an unauthorised session read the protected file'
fi
# And no fmm was started for it: --root is the permission, and without it
# nothing should be asking for one.
if pgrep -u 0 -x ace-fmm >/dev/null 2>&1; then
    fail 'an unauthorised session started a root fmm'
fi
stop_broker

printf 'ACE read and deleted a root-only file through the fmm\n'
