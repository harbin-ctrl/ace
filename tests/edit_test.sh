#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/.ace-edit.XXXXXX")
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
trap cleanup EXIT HUP INT TERM

fail()
{
    printf 'edit test: %s\n' "$1" >&2
    exit 1
}

work="$sys_dir/C"
mkdir -p "$work" "$sys_dir/S" "$sys_dir/Prefs/Env-Archive" "$runtime_dir"

ACE_SYS_DIR="$sys_dir" XDG_RUNTIME_DIR="$runtime_dir" \
    "$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.05
done
[ -S "$socket_path" ] || fail 'broker did not start'

edit()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
        "$repo_dir/build/Edit" "$@"
}

expect_file()
{
    name=$1
    shift
    printf '%s\n' "$@" > "$test_dir/expected"
    cmp -s "$work/$name" "$test_dir/expected" || {
        printf 'edit test: %s has the wrong contents:\n' "$name" >&2
        cat "$work/$name" >&2
        exit 1
    }
}

# Editing in place: the current line is selected by number, changed, and a new
# line is inserted before the line after it.
printf 'alpha-%s\nbeta\ngamma\ndelta\n' "$$" > "$work/one.txt"
cp "$work/one.txt" "$test_dir/one.original"
printf 'M2\nE/beta/BETA/\nN\nI\ninserted\nZ\nM*\nW\n' > "$work/one.cmd"
edit SYS:C/one.txt WITH SYS:C/one.cmd > /dev/null || fail 'in-place edit failed'
expect_file one.txt "alpha-$$" BETA inserted gamma delta

# The source file it replaced is kept as the backup the manual describes.
env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
    "$repo_dir/build/Type" T:Edit-backup > "$test_dir/backup" ||
    fail 'no backup file was kept'
cmp -s "$test_dir/backup" "$test_dir/one.original" ||
    fail 'the backup does not hold the original file'

# A TO file leaves the source alone, and the commands can come from standard
# input rather than a WITH file.
printf 'a\nb\nc\n' > "$work/two.txt"
printf 'M2\nD\nM*\nW\n' | edit SYS:C/two.txt TO SYS:C/two.out > /dev/null ||
    fail 'edit to a TO file failed'
expect_file two.txt a b c
expect_file two.out a c

# STOP discards the edit, and says so with a warning exit code.
printf 'a\nb\nc\n' > "$work/three.txt"
set +e
printf 'D\nSTOP\n' | edit SYS:C/three.txt > /dev/null
status=$?
set -e
[ "$status" -eq 5 ] || fail "STOP exited $status instead of 5"
expect_file three.txt a b c

# Global changes reach every line and every occurrence, as the manual's own
# DF0: example does.
printf 'one DF0:\ntwo DF0: DF0:\nthree\n' > "$work/four.txt"
printf 'GE /DF0:/DF2:/\nM*\nW\n' > "$work/four.cmd"
edit SYS:C/four.txt WITH SYS:C/four.cmd > /dev/null || fail 'global change failed'
expect_file four.txt 'one DF2:' 'two DF2: DF2:' three

# The line window: move the pointer past a string, change a character, delete
# one, and delete the rest of the line from a string on.
printf 'abcdefgh\n' > "$work/five.txt"
printf 'PA/abc/\n%%\n#\nDFA/f/\nM*\nW\n' > "$work/five.cmd"
edit SYS:C/five.txt WITH SYS:C/five.cmd > /dev/null || fail 'window edit failed'
expect_file five.txt abcDf

# Splitting and joining, and the trailing newline of the last line, which the
# file did not have and must not gain.
printf 'left right\nlast' > "$work/six.txt"
printf 'V-\nSB/right/\nN\nM*\nW\n' > "$work/six.cmd"
edit SYS:C/six.txt WITH SYS:C/six.cmd > /dev/null || fail 'split failed'
printf 'left \nright\nlast' > "$test_dir/expected"
cmp -s "$work/six.txt" "$test_dir/expected" || fail 'split wrote the wrong file'

printf 'left\nright\n' > "$work/seven.txt"
printf 'V-\nCL/ /\nM*\nW\n' > "$work/seven.cmd"
edit SYS:C/seven.txt WITH SYS:C/seven.cmd > /dev/null || fail 'join failed'
expect_file seven.txt 'left right'

# Moving backward through the queue of previous lines, and the limit the
# PREVIOUS argument puts on it.
printf '1\n2\n3\n4\n5\n' > "$work/eight.txt"
printf 'V-\n4N\n3P\n?\nM*\nW\n' > "$work/eight.cmd"
edit SYS:C/eight.txt PREVIOUS 2 WITH SYS:C/eight.cmd > "$test_dir/output" ||
    fail 'backward move failed'
grep -q 'no more previous lines' "$test_dir/output" ||
    fail 'PREVIOUS did not limit backward movement'
grep -q '3: 3' "$test_dir/output" ||
    fail 'backward move stopped on the wrong line'
expect_file eight.txt 1 2 3 4 5

# REWIND: the file written so far becomes the source, so an inserted line is
# an original line with a number of its own on the second pass.
printf 'one\ntwo\nthree\n' > "$work/nine.txt"
printf 'V-\nM2\nI\nnew\nZ\nREWIND\nTL\nM*\nW\n' > "$work/nine.cmd"
edit SYS:C/nine.txt WITH SYS:C/nine.cmd > "$test_dir/output" ||
    fail 'rewind failed'
grep -q '   2: new' "$test_dir/output" ||
    fail 'rewind did not renumber the inserted line'
expect_file nine.txt one new two three
if ls "$work" | grep -q 'Edit-Temp'; then
    fail 'rewind left a temporary file behind'
fi

# FROM switches the input file without closing the one it leaves, exactly as
# the worked example in the manual does.
printf 'A1\nA2\nA3\nA4\n' > "$work/ten.txt"
printf 'B1\nB2\n' > "$work/ten-b.txt"
printf 'V-\nM2\nFROM .SYS:C/ten-b.txt.\nM*\nFROM\nM*\nW\n' > "$work/ten.cmd"
edit SYS:C/ten.txt WITH SYS:C/ten.cmd > /dev/null || fail 'FROM switching failed'
expect_file ten.txt A1 A2 B1 B2 A3 A4
expect_file ten-b.txt B1 B2

# A command file can call another one, and Q returns from it.
printf 'k1\nk2\n' > "$work/eleven.txt"
printf 'Z/END/\nR\nreplaced\nEND\nQ\nnot reached\n' > "$work/eleven-inner.cmd"
printf 'V-\nC .SYS:C/eleven-inner.cmd.\nI*\ntail\nEND\nM*\nW\n' > "$work/eleven.cmd"
edit SYS:C/eleven.txt WITH SYS:C/eleven.cmd > /dev/null ||
    fail 'nested command file failed'
expect_file eleven.txt replaced k2 tail

# A line longer than WIDTH is carried in two while it is edited and written
# back as one, so a narrow window does not damage the file.
printf 'abcdefghijklmnopqrstuvwxyz\nshort\n' > "$work/twelve.txt"
printf 'V-\nM*\nW\n' > "$work/twelve.cmd"
edit SYS:C/twelve.txt WIDTH 16 WITH SYS:C/twelve.cmd > /dev/null ||
    fail 'narrow width failed'
expect_file twelve.txt abcdefghijklmnopqrstuvwxyz short

# A line number that is not in the file is reported rather than searched for
# forever: the direction of the search is decided before it starts, so neither
# the extra line past the end nor a non-original line can send it back and
# forth. The timeout is the point of the case.
printf '1\n2\n3\n4\n' > "$work/thirteen.txt"
printf 'V-\nM999\nM2\nI\nins\nZ\nM4\nM2\n?\nM*\nW\n' > "$work/thirteen.cmd"
env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
    timeout 20 "$repo_dir/build/Edit" SYS:C/thirteen.txt \
    WITH SYS:C/thirteen.cmd > "$test_dir/output" ||
    fail 'a line number outside the file did not finish'
grep -q 'no such line' "$test_dir/output" ||
    fail 'a line number outside the file was not reported'
expect_file thirteen.txt 1 ins 2 3 4

# A missing source file is a failure, not an empty edit.
set +e
edit SYS:C/nosuch.txt < /dev/null > /dev/null 2>&1
status=$?
set -e
[ "$status" -eq 20 ] || fail "a missing file exited $status instead of 20"

printf 'Edit line editor test passed\n'
