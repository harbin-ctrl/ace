#!/bin/sh
# Shutdown and Reboot: confirming before stopping the machine.
#
# Nothing here stops the machine. ACE_HALT_COMMAND points the commands at a
# recorder that writes down what it was asked to do and exits, so every path
# can be walked to its end and the end can be inspected. That seam grants
# nothing: these commands run as the user, so anyone who can set the variable
# could have run the program themselves.
#
# What is being checked is the confirmation, which is the whole of the safety
# here. The commands ask a person, and refuse rather than ask when there is no
# person -- a script that reads its answer would take its own next line as the
# reply and then run the rest of itself against a question it never meant to
# answer.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE halt test skipped (must run as an ordinary user)\n'
    exit 0
fi

work=$(mktemp -d "$repo_dir/.ace-halt.XXXXXX")
sys_dir="$work/sys"
socket_path="$work/broker.sock"
recorder="$work/recorder"
halt_log="$work/halt.log"
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
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

checks=0
failures=0
check_failed()
{
    failures=$((failures + 1))
    printf 'ACE halt test: %s\n' "$1" >&2
}
fail()
{
    check_failed "$1"
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S"
for command in Echo Shutdown Reboot FailAt; do
    [ -x "$repo_dir/build/$command" ] &&
        cp "$repo_dir/build/$command" "$sys_dir/C/"
done

# Stands in for systemctl. Records the action and succeeds, so the commands
# see the outcome they would see on a machine that agreed to stop.
cat > "$recorder" <<'RECORDER'
#!/bin/sh
printf '%s\n' "$1" >> "$ACE_HALT_LOG"
exit 0
RECORDER
chmod +x "$recorder"

ACE_SYS_DIR="$sys_dir" "$repo_dir/build/ace-broker" "$socket_path" \
    >"$work/broker.log" 2>&1 &
broker_pid=$!
for _ in $(seq 1 400); do
    [ -S "$socket_path" ] && break
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'the broker did not start'

session=0

# In a script: no prompt is drawn and nobody is reading, which is where the
# commands have to refuse rather than ask.
in_script()
{
    session=$((session + 1))
    : > "$halt_log"
    printf 'FailAt 100\n%s\n' "$1" > "$work/script"
    output=$(env ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION="halt-$session" ACE_HALT_COMMAND="$recorder" \
        ACE_HALT_LOG="$halt_log" \
        ACE_STARTUP_SCRIPT="rootfs:${work#/}/script" \
        "$repo_dir/build/ace-user-shell" </dev/null 2>&1)
}

# At a prompt, with the answer typed after the command.
at_a_prompt()
{
    session=$((session + 1))
    : > "$halt_log"
    output=$(printf '%s\n' "$1" |
        env ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
            ACE_SESSION="halt-$session" ACE_HALT_COMMAND="$recorder" \
            ACE_HALT_LOG="$halt_log" \
            "$repo_dir/build/ace-user-shell" 2>&1 | tr -d '\r')
}

recorded() { tr '\n' ' ' < "$halt_log" | sed 's/ *$//'; }

expect_recorded()
{
    checks=$((checks + 1))
    [ "$(recorded)" = "$2" ] ||
        check_failed "$1: the machine was asked to [$(recorded)], wanted [$2]"
}

expect_said()
{
    checks=$((checks + 1))
    case "$output" in
        *"$2"*) ;;
        *) check_failed "$1: did not say [$2]; output was: $output" ;;
    esac
}

# --------------------------------------------------------------------------
# 1. CONFIRM means it has already been answered.
# --------------------------------------------------------------------------
in_script 'Shutdown CONFIRM'
expect_said 'Shutdown CONFIRM' 'Shutting down'
expect_recorded 'Shutdown CONFIRM' 'poweroff'

in_script 'Reboot CONFIRM'
expect_said 'Reboot CONFIRM' 'Rebooting'
expect_recorded 'Reboot CONFIRM' 'reboot'

# --------------------------------------------------------------------------
# 2. Without CONFIRM, and with nobody to ask, nothing happens.
#
# The refusal is the point, and so is the reason given: a script that is told
# to use CONFIRM can be fixed, one that is told nothing cannot.
# --------------------------------------------------------------------------
in_script 'Shutdown'
expect_said 'Shutdown in a script' 'nobody to ask'
expect_said 'Shutdown in a script' 'Shutdown cancelled'
expect_recorded 'Shutdown in a script' ''

in_script 'Reboot'
expect_said 'Reboot in a script' 'nobody to ask'
expect_recorded 'Reboot in a script' ''

# --------------------------------------------------------------------------
# 3. At a prompt it asks, and only "y" or "yes" is yes.
#
# Everything else has to mean no, because only one of the two answers can be
# taken back.
# --------------------------------------------------------------------------
at_a_prompt 'Shutdown
y'
expect_said 'answered y' 'Shut down the system?'
expect_recorded 'answered y' 'poweroff'

at_a_prompt 'Reboot
yes'
expect_said 'answered yes' 'Reboot the system?'
expect_recorded 'answered yes' 'reboot'

for answer in n N no '' maybe Y3S; do
    at_a_prompt "Shutdown
$answer"
    expect_recorded "answered [$answer]" ''
done

# An answer that is not there at all -- end of input where a person was
# expected -- is not consent either.
at_a_prompt 'Shutdown'
expect_recorded 'no answer at all' ''

# --------------------------------------------------------------------------
# 4. The command is asked for by name, not assembled from anything typed.
#
# Nothing the user writes reaches the program that stops the machine: the
# only argument is chosen here, between two constants.
# --------------------------------------------------------------------------
at_a_prompt 'Shutdown
y'
checks=$((checks + 1))
[ "$(wc -l < "$halt_log")" = "1" ] ||
    check_failed 'the halt program was asked to do more than one thing'

if [ "$failures" -ne 0 ]; then
    printf 'ACE halt test: %s of %s checks failed\n' "$failures" "$checks" >&2
    exit 1
fi
printf 'ACE confirmed before stopping the machine, and refused when it could not ask (%s checks)\n' \
    "$checks"
