#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-tine-test.XXXXXX")
sys_dir="$test_dir/sys"
runtime_dir="$test_dir/run"
socket_path="$test_dir/broker.sock"
log_path="$test_dir/tine.log"
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
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'tine test: %s\n' "$1" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" \
         "$runtime_dir"
printf 'hello\n' > "$sys_dir/C/esc.txt"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

# ED/Tine is a full-screen program, so feed it through a real pseudo-terminal.
# ESC enters extended-command mode, Q is Quit, and Return executes it. The
# command-line transcript must contain the asterisk prompt before Q. The ACE
# console delivers the physical Return key as LF, so exercise that path here.
set +e
{
    sleep 0.5
    printf '\033'
    sleep 0.1
    printf 'Q\n'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_BROKER_SOCKET='$socket_path' \
         ACE_SESSION=tine-esc-test '$repo_dir/tools/tine/tine' SYS:C/esc.txt" \
        "$log_path" >/dev/null 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
    tail -c 512 "$log_path" >&2 || true
    fail "Tine did not exit after ESC-Q-Return (status $status)"
fi
grep -a -q '\*' "$log_path" ||
    fail 'ESC did not display the extended-command asterisk prompt'

# Amiga Backspace/Delete is byte 0x08. In ED extended-command mode it must
# edit the command line, not be rendered as the visible control text ^H.
set +e
{
    sleep 0.5
    printf '\033QX\010\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_BROKER_SOCKET='$socket_path' \
         ACE_SESSION=tine-command-backspace-test '$repo_dir/tools/tine/tine' SYS:C/esc.txt" \
        "$test_dir/command-backspace.log" >/dev/null 2>&1
backspace_status=$?
set -e
[ "$backspace_status" -eq 0 ] ||
    fail 'Backspace did not edit the extended-command prompt'
! grep -a -q '\^H' "$test_dir/command-backspace.log" ||
    fail 'Backspace was rendered as ^H in the extended-command prompt'

# The ED wrapper must consume the Amiga argument template rather than passing
# keywords such as FROM and WITH to Tine as command-file names. TABS must also
# affect the initial tab stop: TB followed by TY should leave four spaces
# between the two characters when the tab distance is five.
printf 'I/a/;TB;TY/b/;X\n' > "$sys_dir/C/with.cmd"
env ACE_TINE_BINARY="$repo_dir/tools/tine/tine" \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=tine-ed-args \
    "$repo_dir/build/Ed" FROM SYS:C/from.txt WITH SYS:C/with.cmd TABS 5 \
    > /dev/null 2>&1 || fail 'ED argument compatibility failed'
printf 'a    b\n\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/from.txt" "$test_dir/expected" ||
    fail 'ED FROM/WITH/TABS produced the wrong file'

# ED 2.00 opens a missing file with one blank line.  Its line insertion
# commands preserve that line, and U does not partially undo a structural
# I/A insertion.  These bytes are the golden results from the real
# AmigaOS 3.1 ED 2.00 probe in tools/ed-amiga-probe.
printf 'I/alpha/\nA/beta/\nX\n' > "$sys_dir/C/new-file.cmd"
env ACE_TINE_BINARY="$repo_dir/tools/tine/tine" \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=tine-new-file-corpus \
    "$repo_dir/build/Ed" FROM SYS:C/new-file.txt WITH SYS:C/new-file.cmd \
    > /dev/null 2>&1 || fail 'ED new-file corpus session failed'
printf 'alpha\nbeta\n\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/new-file.txt" "$test_dir/expected" ||
    fail 'ED new-file line insertion differs from AmigaOS 3.1'

printf 'X\n' > "$sys_dir/C/new-empty.cmd"
env ACE_TINE_BINARY="$repo_dir/tools/tine/tine" \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=tine-new-empty-corpus \
    "$repo_dir/build/Ed" FROM SYS:C/new-empty.txt WITH SYS:C/new-empty.cmd \
    > /dev/null 2>&1 || fail 'ED empty-new-file corpus session failed'
printf '\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/new-empty.txt" "$test_dir/expected" ||
    fail 'ED empty new-file output differs from AmigaOS 3.1'

printf 'I/abc/\nU\nX\n' > "$sys_dir/C/undo-insert.cmd"
env ACE_TINE_BINARY="$repo_dir/tools/tine/tine" \
    ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=tine-undo-insert-corpus \
    "$repo_dir/build/Ed" FROM SYS:C/undo-insert.txt \
    WITH SYS:C/undo-insert.cmd > /dev/null 2>&1 ||
    fail 'ED undo-insert corpus session failed'
printf 'abc\n\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/undo-insert.txt" "$test_dir/expected" ||
    fail 'ED U partially undid a line insertion'

# A missing file is a normal ED session, not an input-file error.
printf 'Q\n' > "$sys_dir/C/quit.cmd"
missing_log="$test_dir/missing.log"
timeout 5 script -qefc \
    "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
     ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-missing-test \
     '$repo_dir/build/Ed' FROM SYS:C/missing.txt WITH SYS:C/quit.cmd" \
    "$missing_log" >/dev/null 2>&1 || fail 'missing-file ED session failed'
grep -a -q 'Creating new file' "$missing_log" ||
    fail 'ED did not report Creating new file'

# ED rejects binary input. A command file quits after the diagnostic so the
# test does not need to edit the terminal session interactively.
printf 'text\000binary\n' > "$sys_dir/C/binary.dat"
binary_log="$test_dir/binary.log"
timeout 5 script -qefc \
    "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
     ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-binary-test \
     '$repo_dir/build/Ed' SYS:C/binary.dat WITH SYS:C/quit.cmd" \
    "$binary_log" >/dev/null 2>&1 || fail 'binary-file ED session failed'
grep -a -q 'Binary file' "$binary_log" ||
    fail 'ED did not reject binary input'

# A 255-character line is accepted, but adding one more character is refused.
awk 'BEGIN { for (i = 0; i < 255; i++) printf "a"; printf "\n" }' \
    > "$sys_dir/C/line-limit.txt"
printf 'TY/x/\n' > "$sys_dir/C/line-limit.cmd"
line_log="$test_dir/line-limit.log"
set +e
{
    sleep 0.7
    printf '\033'
    sleep 0.1
    printf 'Q\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-line-limit \
         '$repo_dir/build/Ed' SYS:C/line-limit.txt WITH SYS:C/line-limit.cmd" \
        "$line_log" >/dev/null 2>&1
line_status=$?
set -e
[ "$line_status" -eq 0 ] || fail 'line-limit ED session failed'
grep -a -q 'Line Too Long' "$line_log" ||
    fail 'ED did not enforce the 255-character line limit'
awk 'BEGIN { for (i = 0; i < 255; i++) printf "a"; printf "\n" }' \
    > "$test_dir/expected"
cmp -s "$sys_dir/C/line-limit.txt" "$test_dir/expected" ||
    fail 'line-limit rejection changed the file'

# SIZE limits the total in-memory text buffer. The existing two-byte file has
# no room for another character when SIZE 2 is specified.
printf 'a\n' > "$sys_dir/C/size-limit.txt"
printf 'TY/x/\n' > "$sys_dir/C/size-limit.cmd"
size_log="$test_dir/size-limit.log"
set +e
{
    sleep 0.7
    printf '\033'
    sleep 0.1
    printf 'Q\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-size-limit \
         '$repo_dir/build/Ed' SYS:C/size-limit.txt WITH SYS:C/size-limit.cmd SIZE 2" \
        "$size_log" >/dev/null 2>&1
size_status=$?
set -e
[ "$size_status" -eq 0 ] || fail 'SIZE-limit ED session failed'
grep -a -q 'Buffer full' "$size_log" ||
    fail 'ED did not enforce SIZE'
printf 'a\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/size-limit.txt" "$test_dir/expected" ||
    fail 'SIZE rejection changed the file'

# ED's SH page exposes the 3.1 defaults observed on the real editor. Feed one
# character to dismiss SH, then leave through the normal extended prompt.
printf 'SH\n' > "$sys_dir/C/status.cmd"
status_log="$test_dir/status.log"
set +e
{
    sleep 0.7
    printf 'x'
    sleep 0.1
    printf '\033Q\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-status-test \
         '$repo_dir/build/Ed' SYS:C/status.txt WITH SYS:C/status.cmd" \
        "$status_log" >/dev/null 2>&1
status_status=$?
set -e
[ "$status_status" -eq 0 ] || fail 'ED status-page session failed'
grep -a -q 'Right margin.*77' "$status_log" ||
    fail 'ED status page did not show right margin 77'
grep -a -q 'Buffer size.*59960' "$status_log" ||
    fail 'ED status page did not show buffer size 59960'

[ "$("$repo_dir/build/Ed" '?')" = \
    'FROM/A,SIZE/N,WITH/K,WINDOW/K,TABS/N,WIDTH=COLS/N,HEIGHT=ROWS/N' ] ||
    fail 'ED ? did not print the ED template'

# ED 2.00's immediate control-key defaults: Ctrl-H deletes left, Ctrl-I moves
# to the next tab stop, Ctrl-A inserts a line, and undefined controls do
# nothing. Save the resulting file through extended X and verify the layout.
: > "$sys_dir/C/key-map.txt"
set +e
{
    sleep 0.7
    printf 'a\010\003\001c\011b\013d\033X\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-key-map-test \
         '$repo_dir/build/Ed' SYS:C/key-map.txt" \
        "$test_dir/key-map.log" >/dev/null 2>&1
key_status=$?
set -e
[ "$key_status" -eq 0 ] || fail 'ED immediate-key session failed'
printf 'c  bd\n\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/key-map.txt" "$test_dir/expected" ||
    fail 'ED immediate control-key mappings differ'

# ED's U command reverses edits on the current line.
: > "$sys_dir/C/undo-current.txt"
set +e
{
    sleep 0.7
    printf 'abc\033U\r\033X\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-undo-current-test \
         '$repo_dir/build/Ed' SYS:C/undo-current.txt" \
        "$test_dir/undo-current.log" >/dev/null 2>&1
undo_current_status=$?
set -e
[ "$undo_current_status" -eq 0 ] || fail 'ED current-line undo session failed'
printf '\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/undo-current.txt" "$test_dir/expected" ||
    fail 'ED did not undo current-line edits'

# Moving to another line prevents U from undoing an edit made on the old line.
: > "$sys_dir/C/undo-moved.txt"
set +e
{
    sleep 0.7
    printf 'a\rb\033[A\033U\r\033X\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-undo-moved-test \
         '$repo_dir/build/Ed' SYS:C/undo-moved.txt" \
        "$test_dir/undo-moved.log" >/dev/null 2>&1
undo_moved_status=$?
set -e
[ "$undo_moved_status" -eq 0 ] || fail 'ED moved-line undo session failed'
printf 'a\nb\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/undo-moved.txt" "$test_dir/expected" ||
    fail 'ED undid an edit after moving lines'

# ED cannot undo a line deletion.
printf 'a\nb\n' > "$sys_dir/C/undo-delete.txt"
set +e
{
    sleep 0.7
    printf '\033D\r\033U\r\033X\r'
} |
    timeout 5 script -qefc \
        "env TERM=xterm ACE_TINE_BINARY='$repo_dir/tools/tine/tine' \
         ACE_BROKER_SOCKET='$socket_path' ACE_SESSION=tine-undo-delete-test \
         '$repo_dir/build/Ed' SYS:C/undo-delete.txt" \
        "$test_dir/undo-delete.log" >/dev/null 2>&1
undo_delete_status=$?
set -e
[ "$undo_delete_status" -eq 0 ] || fail 'ED line-delete undo session failed'
printf 'b\n' > "$test_dir/expected"
cmp -s "$sys_dir/C/undo-delete.txt" "$test_dir/expected" ||
    fail 'ED undid a line deletion'

printf 'tine ESC prompt test passed\n'
