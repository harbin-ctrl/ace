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

# The source file it replaced is kept as the backup, under the name the
# original uses.
env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
    "$repo_dir/build/Type" T:EDIT-BACKUP > "$test_dir/backup" ||
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

# STOP discards the edit, and says so with a warning exit code. It also
# leaves its work file in T:, which is what the original does -- the next
# edit in the same process opens the same name over the top of it.
printf 'a\nb\nc\n' > "$work/three.txt"
set +e
printf 'D\nSTOP\n' | edit SYS:C/three.txt > /dev/null
status=$?
set -e
[ "$status" -eq 5 ] || fail "STOP exited $status instead of 5"
expect_file three.txt a b c
find "$runtime_dir" -name 'E*-WK1' | grep -q . ||
    fail 'STOP did not leave its work file behind'

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
grep -q 'No more previous lines' "$test_dir/output" ||
    fail 'PREVIOUS did not limit backward movement'
grep -A1 '^3\.$' "$test_dir/output" | grep -q '^3$' ||
    fail 'backward move stopped on the wrong line'
expect_file eight.txt 1 2 3 4 5

# REWIND: the file written so far becomes the source, so an inserted line is
# an original line with a number of its own on the second pass.
printf 'one\ntwo\nthree\n' > "$work/nine.txt"
printf 'V-\nM2\nI\nnew\nZ\nREWIND\nTL\nM*\nW\n' > "$work/nine.cmd"
edit SYS:C/nine.txt WITH SYS:C/nine.cmd > "$test_dir/output" ||
    fail 'rewind failed'
grep -q '^    2  new$' "$test_dir/output" ||
    fail 'rewind did not renumber the inserted line'
expect_file nine.txt one new two three
# The work files live in T: and are named as the original names them. The
# original does not clean them up, and neither does this, so what matters is
# that a rewind alternates between two names rather than making a new one each
# time: at most E<nn>-WK1 and E<nn>-WK2 exist however many rewinds ran.
work_files=$(find "$runtime_dir" -name 'E*-WK*' | wc -l)
[ "$work_files" -le 2 ] ||
    fail "rewind left $work_files work files instead of reusing two"
find "$runtime_dir" -name 'E*-WK*' | grep -qv 'WK[12]$' &&
    fail 'a work file was named outside the WK1/WK2 pair'
true

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

# A line number that is not in the file walks to the end and reports running
# out of input, the way the original does, rather than searching for it
# forever: the direction is decided before the search starts, so neither the
# extra line past the end nor a non-original line can send it back and forth.
# The timeout is the point of the case.
printf '1\n2\n3\n4\n' > "$work/thirteen.txt"
printf 'V-\nM999\nM2\nI\nins\nZ\nM4\nM2\n?\nM*\nW\n' > "$work/thirteen.cmd"
env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
    timeout 20 "$repo_dir/build/Edit" SYS:C/thirteen.txt \
    WITH SYS:C/thirteen.cmd > "$test_dir/output" ||
    fail 'a line number outside the file did not finish'
grep -q 'Input exhausted' "$test_dir/output" ||
    fail 'a line number outside the file was not reported'
expect_file thirteen.txt 1 ins 2 3 4

# A line number that has been passed -- deleted, or left behind -- is its own
# error, and the number is in it.
printf '1\n2\n3\n4\n' > "$work/small.txt"
printf 'V-\nM3\nM2\nI\nx\nZ\nSTOP\n' > "$work/small.cmd"
env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
    timeout 20 "$repo_dir/build/Edit" SYS:C/small.txt WITH SYS:C/small.cmd \
    > "$test_dir/output" 2>&1 || true
printf 'D2 3\nM1;I3\nZ\nSTOP\n' > "$work/small.cmd"
cp "$test_dir/original" "$work/small.txt" 2>/dev/null || printf '1\n2\n3\n4\n' > "$work/small.txt"
env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=edit-test \
    timeout 20 "$repo_dir/build/Edit" SYS:C/small.txt WITH SYS:C/small.cmd \
    > "$test_dir/output" 2>&1 || true
grep -q 'Line number 3 too small' "$test_dir/output" ||
    fail 'reaching past a wanted line number was not reported'

# Fidelity to the original. These three cases are transcripts taken from
# AmigaDOS's own EDIT running under emulation, on a file of six lines and a
# trailing empty one, replayed here and compared byte for byte. The only
# things left out are the ":" prompt, which appears when commands are typed
# rather than read from a file, and the echo of the typed commands.
# Two files, because the transcripts were taken on two of them: the earlier
# session's file ended with a blank line, which is why its extra line is 8,
# and the later one does not, which is why its extra line is 7.
printf 'one cat\ntwo dog\nthree cat cat\nfour\nfive\nsix\n\n' > "$test_dir/original"
printf 'one cat\ntwo dog\nthree cat cat\nfour\nfive\nsix\n' > "$test_dir/original6"

transcript()
{
    name=$1
    source=${2:-"$test_dir/original"}
    cp "$source" "$work/$name.txt"
    edit "SYS:C/$name.txt" WITH "SYS:C/$name.cmd" > "$test_dir/$name.out" 2>&1 ||
        true
    cmp -s "$test_dir/$name.out" "$test_dir/$name.expected" || {
        printf 'edit test: %s does not match the Amiga transcript:\n' "$name" >&2
        diff "$test_dir/$name.expected" "$test_dir/$name.out" >&2 || true
        exit 1
    }
}

# Movement, repeat groups, and running off the end of the file. The pointer
# under "3N" and "Input exhausted" are the original's, and so is showing the
# extra line past the end as 8* -- numbered, but marked as having no number
# of its own.
printf '?\n3(N)\n2(N;?)\n(N)\n3N\nSTOP\n' > "$work/groups.cmd"
printf 'Editor\n1.\none cat\n4.\nfour\n5.\nfive\n6.\nsix\n7.\n\n >\nInput exhausted\n8*\n\n' \
    > "$test_dir/groups.expected"
transcript groups

# The three change commands chained on one line, a global change, and the
# line window. "one YdogX" is the string the original produces, and it is
# what settles the argument order: the first string is always the one
# searched for. The marker is written without a newline after it, so the
# next verification runs onto its line.
printf 'M1;E/cat/dog/;A/dog/X/;B/dog/Y/;?\nM3;GE/cat/CAT/;PB/CAT;?\nI\nx\nZ\n?\nSTOP\n' \
    > "$work/change.cmd"
# The two marker columns here were written down as six spaces in the hand
# transcript. Re-running P,B on its own gave five, which is what the rule
# established by the window session gives: the mark sits under the character
# before the window. The six was a slip of the pen.
printf 'Editor\n1.\none YdogX\nG1\n3.\nthree CAT CAT\n     >3.\nthree CAT CAT\n     >' \
    > "$test_dir/change.expected"
transcript change

# Typing the output queue shows the lines held in memory -- everything passed
# so far, since nothing spills to the work file until the queue is full -- and
# then shows the current line again, because the queue listing left something
# other than the current line last on the screen. Both are the original's.
lines=55
i=1
while [ "$i" -le "$lines" ]; do
    printf 'line %s\n' "$i"
    i=$((i + 1))
done > "$test_dir/queue.original"
cp "$test_dir/queue.original" "$work/queue.txt"
printf 'M*\nTP\nSTOP\n' > "$work/queue.cmd"
edit SYS:C/queue.txt WITH SYS:C/queue.cmd > "$test_dir/queue.out" 2>&1 || true
{
    printf 'Editor\n56*\n\n'
    i=1
    while [ "$i" -le "$lines" ]; do
        printf 'line %s\n' "$i"
        i=$((i + 1))
    done
    printf '56*\n\n'
} > "$test_dir/queue.expected"
cmp -s "$test_dir/queue.out" "$test_dir/queue.expected" ||
    fail 'T,P did not type the queue and re-show the current line'
# STOP writes the lines already passed before it closes, so a session that
# walked the file to its end and stopped leaves a work file the size of the
# file it was editing -- 3301 bytes for 3301 on the original.
work_file=$(find "$runtime_dir" -name 'E*-WK1' | head -n 1)
cmp -s "$work_file" "$test_dir/queue.original" ||
    fail 'the abandoned work file does not hold the lines that were passed'
cmp -s "$work/queue.txt" "$test_dir/queue.original" ||
    fail 'STOP changed the file it was editing'

# An inserted line has no number of its own and verifies as +++, and the
# insertion does not re-display the line it was inserted before.
printf 'M2;I\nx\nZ\nP;?\nSTOP\n' > "$work/insert.cmd"
printf 'Editor\n2.\ntwo dog\n+++.\nx\n' > "$test_dir/insert.expected"
transcript insert

# More transcripts from the original. Delete with a range, insert before a
# numbered line, replace, and the period that is not an argument to D.
printf 'D2 3\n?\nI4\ninserted\nZ\n?\nR\nreplaced\nZ\n?\nD.*\n?\nSTOP\n' \
    > "$work/edits.cmd"
printf 'Editor\n4.\nfour\n4.\nfour\n4.\nfour\n5.\nfive\n5.\nfive\n >\nUnknown command - .\n6.\nsix\n6.\nsix\n' \
    > "$test_dir/edits.expected"
transcript edits "$test_dir/original6"

# T on the extra line past the end types it -- an empty line -- and then
# verifies it, because typing shows text without a number and only a numbered
# display satisfies the pending verification.
printf 'M*\nT\nSTOP\n' > "$work/typeend.cmd"
printf 'Editor\n7*\n\n\n7*\n\n' > "$test_dir/typeend.expected"
transcript typeend "$test_dir/original6"

# ! heads its two rows with the line number, and marks capitals underneath
# with underscores.
printf 'M1;E/one/ONE/\n!\nSTOP\n' > "$work/hex.cmd"
printf 'Editor\n1.\nONE cat\n1.\nONE cat\n___\n' > "$test_dir/hex.expected"
transcript hex "$test_dir/original6"

# The line window. The mark sits under the character before the window, so
# PR shows none at all, and %, _ and 2# act on the window and step it along.
# D,F,A cuts from after the string to the end of the line.
printf 'M3;PA/three /;?;%%;?;_;?;2#;?\nPR;?;>;>;?;<;?\nM2;DFA/two/;?\nSTOP\n' \
    > "$work/window.cmd"
printf 'Editor\n3.\nthree cat cat\n     >3.\nthree Cat cat\n      >3.\nthree C t cat\n       >3.\nthree C cat\n       >3.\nthree C cat\n3.\nthree C cat\n >3.\nthree C cat\n>2.\ntwo\n' \
    > "$test_dir/window.expected"
transcript window "$test_dir/original6"

# A missing source file is a failure, not an empty edit.
set +e
edit SYS:C/nosuch.txt < /dev/null > /dev/null 2>&1
status=$?
set -e
[ "$status" -eq 20 ] || fail "a missing file exited $status instead of 20"

printf 'Edit line editor test passed\n'
