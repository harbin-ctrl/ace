#!/bin/sh
# The standard assigns, and the two layers that make them.
#
# dos.library's boot code establishes SYS: and the drawers under it before any
# shell exists; the Startup-Sequence does the rest as ordinary commands. This
# checks both halves, and the things that only work because of them: finding a
# command through C:, the conditionals that let a script decide anything, and
# an environment kept as files in ENV: and ENVARC:.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
# Inside the repo, deliberately: a volume never spans filesystems, so SYS:C
# and the commands it points at have to be on the same one.
test_dir=$(mktemp -d "$repo_dir/.ace-assign-test.XXXXXX")
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
broker_pid=""

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
trap cleanup EXIT INT TERM

fail()
{
    printf 'system assigns test: %s\n' "$1" >&2
    exit 1
}

expect_contains()
{
    case "$1" in
        *"$2"*) ;;
        *) fail "$3
--- output ---
$1
--------------" ;;
    esac
}

expect_missing()
{
    case "$1" in
        *"$2"*) fail "$3
--- output ---
$1
--------------" ;;
    esac
}

# A SYS: laid out the way the installer lays one out: the commands are
# elsewhere and SYS:C holds links to them. L:, LIBS:, DEVS: and FONTS: are
# deliberately absent, so the fallback in AddBootAssign() has something to do.
mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" "$runtime_dir"
for command in "$repo_dir"/build/*; do
    [ -f "$command" ] && [ -x "$command" ] || continue
    ln -s "$command" "$sys_dir/C/$(basename "$command")"
done

XDG_RUNTIME_DIR="$runtime_dir" ACE_SYS_DIR="$sys_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail "broker did not start"

session=0
run_shell()
{
    session=$((session + 1))
    printf '%s' "$1" | ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION="assign-test-$session" \
        "$repo_dir/build/ace-user-shell" 2>&1
}

# --- the boot assigns ------------------------------------------------------
output=$(run_shell 'Assign
')
for name in SYS C S L LIBS DEVS FONTS ENV ENVARC T; do
    expect_contains "$output" "$name " "$name: was not established at boot"
done

# AddBootAssign(): the drawer if it is there, SYS: itself if it is not. So C:
# and S: name their own drawers and L: names the volume root.
expect_contains "$output" "C  " "C: should be a drawer of its own"
sys_line=$(printf '%s\n' "$output" | awk '$1 == "SYS" { print $2 }')
l_line=$(printf '%s\n' "$output" | awk '$1 == "L" { print $2 }')
c_line=$(printf '%s\n' "$output" | awk '$1 == "C" { print $2 }')
[ "$l_line" = "$sys_line" ] || fail "L: should fall back to SYS: ($l_line)"
[ "$c_line" != "$sys_line" ] || fail "C: should be SYS:C, not SYS:"

# --- finding a command through C: -----------------------------------------
output=$(run_shell 'Echo found-through-c
')
expect_contains "$output" "found-through-c" "a bare command did not resolve"

# An Amiga filesystem does not distinguish case, so a command answers to any
# spelling of its name. This is the whole reason anyone can type "dir".
output=$(run_shell 'echo lower-case-name
ECHO UPPER-CASE-NAME
')
expect_contains "$output" "lower-case-name" "a lower-case command name did not resolve"
expect_contains "$output" "UPPER-CASE-NAME" "an upper-case command name did not resolve"

# The same for a path, and a name being created keeps the spelling it was
# given rather than one borrowed from something that is not there.
output=$(run_shell 'MakeDir T:CaseMade
Dir T:
Delete T:casemade
Dir T:
')
expect_contains "$output" "CaseMade" "a created name did not keep its own case"
expect_contains "$output" "Deleted" "a path did not resolve without regard to case"

# Typing a directory's name changes to it, which is how an Amiga user reaches
# C: or SYS:. The Shell does that itself, but only when opening the name as a
# command fails the way AmigaDOS fails: Open() on a directory has to report
# ERROR_OBJECT_WRONG_TYPE rather than handing back a readable handle, which
# is what Linux would do, and a directory must not pass for an executable
# just because it carries the execute bit.
output=$(run_shell 'C:
CD
')
expect_contains "$output" "/C" "typing C: did not change directory to it"
output=$(run_shell 'SYS:
CD
')
expect_missing "$output" "/C" "typing SYS: should not land in SYS:C"

# The command path is the loader's business. Open() must not quietly find a
# command when what was asked for was a file.
output=$(run_shell 'Type Echo
')
expect_missing "$output" "found" "Type should not open a command as a file"
expect_contains "$output" "can't open" "Type should report a missing file"

# --- If, Else and EndIf ----------------------------------------------------
cat > "$sys_dir/S/ACE-Startup" <<'EOF'
Echo A-start
If EXISTS SYS:C
Echo B-taken
Else
Echo C-not-taken
EndIf
If EXISTS SYS:NoSuchDrawer
Echo D-not-taken
Else
Echo E-taken
EndIf
If EXISTS SYS:NoSuchDrawer
Echo F-not-taken
If EXISTS SYS:C
Echo G-not-taken
EndIf
Echo H-not-taken
EndIf
Echo I-end
EOF
output=$(run_shell '')
for taken in A-start B-taken E-taken I-end; do
    expect_contains "$output" "$taken" "$taken should have run"
done
for skipped in C-not-taken D-not-taken F-not-taken G-not-taken H-not-taken; do
    expect_missing "$output" "$skipped" "$skipped should have been skipped"
done

# --- the startup scripts, in order -----------------------------------------
printf 'Echo one-startup-sequence\n' > "$sys_dir/S/Startup-Sequence"
printf 'Echo two-shell-startup\n' > "$sys_dir/S/Shell-Startup"
printf 'Echo three-ace-startup\n' > "$sys_dir/S/ACE-Startup"
output=$(run_shell '')
order=$(printf '%s\n' "$output" | sed -n 's/.*\(one-startup-sequence\|two-shell-startup\|three-ace-startup\).*/\1/p' | tr '\n' ' ')
[ "$order" = "one-startup-sequence two-shell-startup three-ace-startup " ] ||
    fail "startup scripts ran in the wrong order: $order"

# A script that is not there is skipped rather than complained about.
rm "$sys_dir/S/Shell-Startup"
output=$(run_shell '')
expect_contains "$output" "one-startup-sequence" "Startup-Sequence should still run"
expect_missing "$output" "two-shell-startup" "a removed script should not run"

# --- Execute ---------------------------------------------------------------
printf 'Echo executed-body\nIf EXISTS SYS:C\nEcho executed-conditional\nEndIf\n' \
    > "$sys_dir/S/User-Startup"
# From inside a running script: spliced into the caller's own input, so the
# caller's next line still follows it.
cat > "$sys_dir/S/Startup-Sequence" <<'EOF'
Echo before-execute
If EXISTS S:User-Startup
Execute S:User-Startup
EndIf
Echo after-execute
EOF
rm "$sys_dir/S/ACE-Startup"
output=$(run_shell '')
order=$(printf '%s\n' "$output" | sed -n 's/.*\(before-execute\|executed-body\|executed-conditional\|after-execute\).*/\1/p' | tr '\n' ' ')
[ "$order" = "before-execute executed-body executed-conditional after-execute " ] ||
    fail "Execute did not splice into the caller's script: $order"

# Typed at a prompt there is no script to splice into, so the script gets a
# shell of its own -- and that shell must not go on to read the caller's
# standard input when the script ends.
rm "$sys_dir/S/Startup-Sequence"
output=$(run_shell 'Echo before-interactive
Execute S:User-Startup
Echo after-interactive
')
order=$(printf '%s\n' "$output" | sed -n 's/.*\(before-interactive\|executed-body\|after-interactive\).*/\1/p' | tr '\n' ' ')
[ "$order" = "before-interactive executed-body after-interactive " ] ||
    fail "Execute from a prompt did not run the script in order: $order"

# --- ENV: and ENVARC: ------------------------------------------------------
output=$(run_shell 'Setenv Editor vim
Getenv Editor
Type ENV:Editor
')
expect_contains "$output" "vim" "Setenv/Getenv did not round-trip"
[ -f "$runtime_dir/ace/env/Editor" ] || fail "ENV:Editor is not a file"
[ ! -f "$sys_dir/Prefs/Env-Archive/Editor" ] ||
    fail "a variable set without SAVE must not reach ENVARC:"

output=$(run_shell 'Setenv Kept SAVE forever
Type ENVARC:Kept
')
expect_contains "$output" "forever" "SAVE did not write ENVARC:"
[ -f "$sys_dir/Prefs/Env-Archive/Kept" ] || fail "ENVARC:Kept is not a file"

output=$(run_shell 'Unsetenv Editor
Getenv Editor
')
expect_missing "$output" "vim" "Unsetenv did not remove the variable"

# A boot restores the saved half into the live one, which is what makes SAVE
# mean anything. The broker start is ACE's boot.
kill -TERM "$broker_pid" 2>/dev/null || true
for _ in $(seq 1 100); do
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.01
done
rm -f "$runtime_dir/ace/env/"*
rm -f "$socket_path" "$socket_path.lock" "$socket_path.pid" \
      "$socket_path.start.lock"
XDG_RUNTIME_DIR="$runtime_dir" ACE_SYS_DIR="$sys_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail "broker did not restart"
output=$(run_shell 'Getenv Kept
')
expect_contains "$output" "forever" "ENVARC: was not restored into ENV: at boot"

printf 'system assigns and startup script tests passed\n'
