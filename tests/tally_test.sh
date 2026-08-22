#!/bin/sh
# Tally: the shell saying how much of what just happened needed root.
#
# ACE reaches the CRM quietly, which is the right default -- privilege is a
# detail of how an operation completed, not a mode the session is in -- but it
# is worth being able to ask. With the tally on, the shell reports the number
# of operations that needed root before each prompt, and says nothing at all
# when that number is zero.
#
# Driven through a pipe into ace-user-shell rather than through a startup
# script, because the report is tied to the prompt: a script has no prompt to
# draw, and deliberately neither reports nor consumes the count. A test that
# used a startup script would see nothing and conclude the feature was broken.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE tally test skipped (must run as an ordinary user)\n'
    exit 0
fi
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE tally test skipped (no noninteractive root helper)\n'
    exit 0
fi

work=$(mktemp -d "$repo_dir/.ace-tally.XXXXXX")
sys_dir="$work/sys"
socket_path="$work/broker.sock"
secrets="/tmp/.ace-tally-secret"
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
    sudo -n rm -f "$secrets-a" "$secrets-b" "$secrets-c" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

checks=0
failures=0
check_failed()
{
    failures=$((failures + 1))
    printf 'ACE tally test: %s\n' "$1" >&2
}
fail()
{
    check_failed "$1"
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S"
for command in Echo CD Type Tally Delete List FailAt; do
    [ -x "$repo_dir/build/$command" ] &&
        cp "$repo_dir/build/$command" "$sys_dir/C/"
done

# Three files the user cannot read, so that reading one is an operation that
# genuinely needs root rather than one that merely might.
for suffix in a b c; do
    sudo -n sh -c "printf 'secret\n' > $secrets-$suffix && chmod 600 $secrets-$suffix" ||
        fail "could not create $secrets-$suffix"
done
if cat "$secrets-a" >/dev/null 2>&1; then
    fail 'the test user can read the protected file, so this proves nothing'
fi

ACE_SYS_DIR="$sys_dir" "$repo_dir/build/ace-broker" --root "$socket_path" \
    >"$work/broker.log" 2>&1 &
broker_pid=$!
for _ in $(seq 1 400); do
    [ -S "$socket_path" ] && break
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'the broker did not start'

# One shell per case, each with its own session, so a case cannot inherit the
# tally state or the count of the one before it.
session=0
shell_with()
{
    session=$((session + 1))
    printf '%s\n' "$1" |
        env ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
            ACE_SESSION="tally-$session" ACE_MODE_PRIVILEGE=root \
            ACE_MODE_VIEW=device ACE_MODE_OWNER_UID="$owner_uid" \
            "$repo_dir/build/ace-user-shell" 2>&1 | tr -d '\r'
}

# The reported lines only, with the prompts that share their line removed:
# the prompt carries no trailing newline, so the report of one cycle prints
# straight after the prompt of the one before it.
reports_in()
{
    printf '%s\n' "$1" | sed 's/.*[0-9]\.[^>]*> //' |
        grep -E 'required root to complete' || true
}

expect_reports()
{
    label=$1
    wanted=$2
    got=$3
    checks=$((checks + 1))
    [ "$got" = "$wanted" ] ||
        check_failed "$label: reported [$got], wanted [$wanted]"
}

# --------------------------------------------------------------------------
# 1. The switch itself, in all three spellings of the argument.
#
# MODE is a positional argument, so the keyword is optional and may be
# written with or without an equals sign. All three have to mean the same
# thing, and a fourth thing has to be refused rather than quietly ignored.
# --------------------------------------------------------------------------
for form in "Tally ON" "Tally MODE ON" "Tally MODE=ON"; do
    output=$(shell_with "$form
Tally")
    checks=$((checks + 1))
    case "$output" in
        *"Tally is on"*) ;;
        *) check_failed "[$form] did not turn the tally on: $output" ;;
    esac
done

output=$(shell_with "Tally")
checks=$((checks + 1))
case "$output" in
    *"Tally is off"*) ;;
    *) check_failed "a fresh session did not report the tally off: $output" ;;
esac

output=$(shell_with "Tally ON
Tally OFF
Tally")
checks=$((checks + 1))
case "$output" in
    *"Tally is off"*) ;;
    *) check_failed "OFF did not turn the tally off: $output" ;;
esac

output=$(shell_with "Tally sideways")
checks=$((checks + 1))
case "$output" in
    *"is not ON or OFF"*) ;;
    *) check_failed "an unknown mode was not refused: $output" ;;
esac

# --------------------------------------------------------------------------
# 2. Off is silent, whatever happens.
#
# The default has to leave a session looking exactly as it did before this
# feature existed, including a session that is using its privilege heavily.
# --------------------------------------------------------------------------
output=$(shell_with "Type RAM4:${secrets#/tmp/}-a
Echo end")
checks=$((checks + 1))
case "$output" in
    *secret*) ;;
    *) check_failed "the protected file was not read at all: $output" ;;
esac
expect_reports 'tally off' '' "$(reports_in "$output")"

# --------------------------------------------------------------------------
# 3. One operation, and the singular has no number in front of it.
# --------------------------------------------------------------------------
output=$(shell_with "Tally ON
Type RAM4:${secrets#/tmp/}-a
Echo end")
expect_reports 'one operation' 'operation required root to complete' \
    "$(reports_in "$output")"

# --------------------------------------------------------------------------
# 4. Several in one prompt cycle, counted and pluralised.
# --------------------------------------------------------------------------
output=$(shell_with "Tally ON
Delete RAM4:${secrets#/tmp/}-a RAM4:${secrets#/tmp/}-b RAM4:${secrets#/tmp/}-c
Echo end")
expect_reports 'three operations' '3 operations required root to complete' \
    "$(reports_in "$output")"
# Restore them for any later case.
for suffix in a b c; do
    sudo -n sh -c "printf 'secret\n' > $secrets-$suffix && chmod 600 $secrets-$suffix"
done

# --------------------------------------------------------------------------
# 5. Nothing privileged, nothing said -- with the tally on.
#
# The feature is meant to be invisible when there is nothing to report, or it
# becomes something to turn off again.
# --------------------------------------------------------------------------
output=$(shell_with "Tally ON
Echo one
Echo two
Echo end")
expect_reports 'nothing privileged' '' "$(reports_in "$output")"

# --------------------------------------------------------------------------
# 6. The count covers one prompt to the next, and no more.
#
# Two privileged reads on separate lines are two reports of one, not one
# report of two and not a running total.
# --------------------------------------------------------------------------
output=$(shell_with "Tally ON
Type RAM4:${secrets#/tmp/}-a
Type RAM4:${secrets#/tmp/}-b
Echo end")
expect_reports 'one per prompt' \
    'operation required root to complete
operation required root to complete' "$(reports_in "$output")"

# --------------------------------------------------------------------------
# 7. Turning it off stops the reporting again.
# --------------------------------------------------------------------------
output=$(shell_with "Tally ON
Tally OFF
Type RAM4:${secrets#/tmp/}-a
Echo end")
expect_reports 'turned off again' '' "$(reports_in "$output")"

if [ "$failures" -ne 0 ]; then
    printf 'ACE tally test: %s of %s checks failed\n' "$failures" "$checks" >&2
    exit 1
fi
printf 'ACE tally reported privileged operations, and only when asked (%s checks)\n' \
    "$checks"
