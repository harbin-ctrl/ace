#!/bin/sh
# The escalation contract, checked from outside.
#
# The rule this file exists to hold in place:
#
#   every AmigaDOS call is tried first as the user; only a refusal on
#   permission grounds, in a session that asked for --root, is retried with
#   privilege; and neither the AmigaDOS path nor the Linux path takes any part
#   in that decision -- only the outcome of the first attempt does.
#
# The check that gives the rest their teeth is process arithmetic.  An
# authorised session runs no privileged process at all until something is
# actually refused, so "was this done as the user?" has an observable answer:
# count the root processes.  Nothing here inspects ACE's internals; a change
# that reintroduced blanket escalation would still be visible from here.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE escalation contract test skipped (must run as an ordinary user)\n'
    exit 0
fi
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE escalation contract test skipped (no noninteractive root helper)\n'
    exit 0
fi

test_dir=$(mktemp -d "$repo_dir/.ace-contract.XXXXXX")
socket_path="$test_dir/broker.sock"
secret=/tmp/.ace-contract-test-secret
closed_dir=/tmp/.ace-contract-test-closed
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
    sudo -n rm -rf "$secret" "$closed_dir" 2>/dev/null || true
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'ACE escalation contract test: %s\n' "$1" >&2
    exit 1
}

# A file the user cannot read, and a directory the user cannot even enter.
# The second one matters on its own: an object whose *parent* is closed is the
# case where ACE has to name something it cannot look at.
sudo -n sh -c "printf 'top-secret\n' > $secret && chmod 600 $secret" ||
    fail 'could not create the protected file'
sudo -n sh -c "mkdir -p $closed_dir && chmod 700 $closed_dir" ||
    fail 'could not create the protected directory'
if cat "$secret" >/dev/null 2>&1 || ls "$closed_dir" >/dev/null 2>&1; then
    fail 'the test user can reach the protected objects, so this proves nothing'
fi
printf 'plain\n' > "$test_dir/plain.txt"

start_broker()
{
    if [ "${1-}" = "--root" ]; then
        ACE_BROKER_SOCKET="$socket_path" "$repo_dir/build/ace-broker" \
            --root "$socket_path" &
    else
        ACE_BROKER_SOCKET="$socket_path" "$repo_dir/build/ace-broker" \
            "$socket_path" &
    fi
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

name_of() { ace "$1" "$repo_dir/build/ace-brokerctl" name "$2"; }
privileged_processes() { pgrep -u 0 -x ace-fmm 2>/dev/null | wc -l; }

start_broker --root || fail 'the authorised broker did not start'

plain_name=$(name_of root "$test_dir/plain.txt") ||
    fail 'an ordinary file has no Amiga name'
secret_name=$(name_of root "$secret") ||
    fail 'the protected file has no Amiga name'
closed_name=$(name_of root "$closed_dir") ||
    fail 'the protected directory has no Amiga name'
missing_name=$(printf '%s' "$plain_name" | sed 's/plain\.txt$/nothing-here.txt/')

# 1. Something the user can do.  Not one privileged process exists yet, and
#    reading a file they own must not create one -- an authorisation is
#    permission to ask when refused, not an instruction to stop trying.
[ "$(privileged_processes)" -eq 0 ] ||
    fail 'a privileged process existed before anything was refused'
out=$(ace root "$repo_dir/build/Type" "$plain_name" 2>&1) ||
    fail "an ordinary file could not be read: $out"
[ "$out" = "plain" ] || fail "an ordinary file read back wrongly: $out"
[ "$(privileged_processes)" -eq 0 ] ||
    fail 'reading a file the user owns started a privileged process'

# 2. A name that is not there.  The commonest failure there is, and the one
#    that must never raise privilege: a typo that authenticated would teach
#    the user to wave prompts away.
ace root "$repo_dir/build/Type" "$missing_name" >/dev/null 2>&1 &&
    fail 'a missing file was somehow read'
[ "$(privileged_processes)" -eq 0 ] ||
    fail 'a missing file started a privileged process'

# 3. A real refusal, in a session that may ask.  This is the one case that
#    escalates, and it must produce the file.
out=$(ace root "$repo_dir/build/Type" "$secret_name" 2>&1) ||
    fail "the protected file could not be read: $out"
[ "$out" = "top-secret" ] ||
    fail "the protected file read back wrongly: $out"
[ "$(privileged_processes)" -ge 1 ] ||
    fail 'the protected read produced the file without any privileged process'

# 4. Now that privilege exists and the session is authorised, the ordinary
#    cases must be exactly as they were.  This is where a session-scoped
#    authorisation goes wrong if it goes wrong: everything afterwards done as
#    root, every new file root-owned, and the user told nothing about it.
out=$(ace root "$repo_dir/build/Type" "$plain_name" 2>&1) ||
    fail "an ordinary file stopped being readable once privilege existed: $out"
[ "$out" = "plain" ] ||
    fail "an ordinary file read back wrongly once privilege existed: $out"
ace root "$repo_dir/build/Type" "$missing_name" >/dev/null 2>&1 &&
    fail 'a missing file was somehow read once privilege existed'

made_name=$(printf '%s' "$plain_name" | sed 's/plain\.txt$/made-here/')
ace root "$repo_dir/build/MakeDir" "$made_name" >/dev/null 2>&1 ||
    fail 'a directory could not be made in the user own directory'
[ -d "$test_dir/made-here" ] || fail 'the directory was not made'
owner=$(stat -c %u "$test_dir/made-here")
[ "$owner" = "$owner_uid" ] ||
    fail "a directory the user could make themselves came out owned by uid $owner"

# 5. An object whose parent the user cannot enter.  ACE has to be able to name
#    it -- a path it refuses to translate is an operation that never reaches
#    the seam, and so never gets the retry the session was authorised for.
ace root "$repo_dir/build/MakeDir" "$closed_name/made" >/dev/null 2>&1 ||
    fail 'a directory could not be made inside a directory the user cannot enter'
sudo -n test -d "$closed_dir/made" ||
    fail 'MakeDir reported success inside the closed directory and made nothing'
ace root "$repo_dir/build/Delete" "$closed_name/made" >/dev/null 2>&1 ||
    fail 'a directory could not be deleted from inside a closed directory'
sudo -n test -d "$closed_dir/made" &&
    fail 'Delete reported success inside the closed directory and removed nothing'

# 6. A date on a protected file.  Stamping one is as much an AmigaDOS
#    operation as reading one, and a session that could Type a file but not
#    Touch it would have a boundary that depended on which system call the
#    command reached for rather than on what it was allowed to touch.
sudo -n touch -d '2001-01-01 00:00:00' "$secret" ||
    fail 'could not age the protected file'
before=$(sudo -n stat -c %Y "$secret")
ace root "$repo_dir/build/Touch" "$secret_name" >/dev/null 2>&1 ||
    fail 'the date of a protected file could not be set'
after=$(sudo -n stat -c %Y "$secret")
[ "$after" != "$before" ] ||
    fail 'Touch reported success on the protected file and changed nothing'
owner=$(sudo -n stat -c %U "$secret")
[ "$owner" = "root" ] ||
    fail "stamping a protected file changed its owner to $owner"

stop_broker

# 7. The same refusal without --root.  The command's own answer stands, and
#    nothing privileged is started for it: --root is the permission, and this
#    is what its absence has to mean.
start_broker || fail 'the unprivileged broker did not start'
ace user "$repo_dir/build/Type" "$secret_name" >/dev/null 2>&1 &&
    fail 'an unauthorised session read the protected file'
[ "$(privileged_processes)" -eq 0 ] ||
    fail 'an unauthorised session started a privileged process'
out=$(ace user "$repo_dir/build/Type" "$plain_name" 2>&1) ||
    fail "an unauthorised session could not read an ordinary file: $out"
[ "$out" = "plain" ] || fail "an unauthorised session read back wrongly: $out"
stop_broker

printf 'ACE escalated only what was refused, and only where it was allowed to\n'
