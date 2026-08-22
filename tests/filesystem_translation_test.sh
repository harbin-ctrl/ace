#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ace-filesystem-test.XXXXXX")
socket_path="$test_dir/broker.sock"
mapping_test_dir=$(mktemp -d "$repo_dir/.ace-filesystem-mapping.XXXXXX")
mapping_colon_dir="$mapping_test_dir/Hi:This:is:a:long:filename:blahblahblah"
mapping_long_component=$(awk 'BEGIN { for (i = 0; i < 150; i++) printf "l" }')
mapping_long_dir="$mapping_test_dir/$mapping_long_component"
# A real Debian package-archive basename (Contents-amd64.gz, an sha256sum
# style account- hash) that exceeds AMIGA_COMPONENT_LIMIT and stays that way
# after compression: 39 hex digits pack into ~0.66 bytes/char under PACK39
# regardless of "randomness" -- it is a positional radix conversion, not an
# entropy coder -- and 140 chars still lands at ~92 bytes, well past the
# ~65-byte budget 106 base32 characters actually buy.
mapping_incompressible_component="account-002bee2f8e16f5de4db0d3b8ce9227c8c0b7f9688348b028e022cb43f210968b40a69cdc8531fd4a2e7c9e144eec48bb477733d70ce5f9b85338a07cb10b849ad8fb"
mapping_incompressible_dir="$mapping_test_dir/$mapping_incompressible_component"
# Mixed case defeats PACK39 (its alphabet is lowercase-only), so this one
# can only succeed through DEFLATE -- highly repetitive so it still fits,
# and specifically exercises DENSE128 encoding of DEFLATE's own output
# rather than PACK39's, which the all-lowercase fixture above cannot.
mapping_deflate_component=$(awk 'BEGIN { for (i = 0; i < 30; i++) printf "AaBbCcDd" }')
mapping_deflate_dir="$mapping_test_dir/$mapping_deflate_component"
mapping_dot_dir="$mapping_test_dir/:"
mapping_dotdot_dir="$mapping_test_dir/::"
mapping_dot_create_parent="$mapping_test_dir/ace-dot-names"
mapping_dot_create_dir="$mapping_dot_create_parent/:"
mapping_dotdot_create_dir="$mapping_dot_create_parent/::"
case_collision_dir="$mapping_test_dir/case-collisions"
case_collision_primary="$case_collision_dir/CaseCollision"
case_collision_secondary="$case_collision_dir/casecollision"
mapping_union_first="$mapping_test_dir/union-first"
mapping_union_second="$mapping_test_dir/union-second"
rename_source="$mapping_test_dir/rename-source"
rename_target="$mapping_test_dir/rename-target"
rename_question_source="$mapping_test_dir/rename-question-source"
rename_question_target="$mapping_test_dir/rename-question-target"
run_created_dir="$mapping_test_dir/run-created"
delete_dir="$mapping_test_dir/delete-tree"
delete_protected="$mapping_test_dir/delete-protected"
note_dir="$mapping_test_dir/filenote"
note_file="$note_dir/noted.txt"
softlink_dir="$mapping_test_dir/softlinks"
softlink_name="$softlink_dir/dangling"
relative_softlink_name="$softlink_dir/relative-dangling"
directory_softlink_name="$softlink_dir/directory-link"
nested_relative_softlink_name="$softlink_dir/nested-relative"
absolute_file_softlink_name="$softlink_dir/absolute-file"
absolute_directory_softlink_name="$softlink_dir/absolute-directory"
absolute_dangling_softlink_name="$softlink_dir/absolute-dangling"
softlink_target_dir="$softlink_dir/target-directory"
softlink_target_file="$softlink_target_dir/inside.txt"
relative_target_file="$softlink_dir/relative-target.txt"
absolute_target_file="$mapping_test_dir/absolute-target.txt"
absolute_missing_target="$mapping_test_dir/absolute-missing.txt"
mkdir "$mapping_colon_dir" "$mapping_long_dir" "$mapping_incompressible_dir" \
      "$mapping_deflate_dir" \
      "$mapping_dot_dir" \
      "$mapping_dotdot_dir" "$mapping_dot_create_parent" "$mapping_union_first" \
      "$mapping_union_second" "$mapping_union_second/only-second" \
      "$delete_dir" "$delete_dir/nested" "$note_dir" "$softlink_dir" \
      "$softlink_target_dir" "$case_collision_dir"
touch "$case_collision_primary" "$case_collision_secondary"
touch "$rename_source"
touch "$rename_question_source"
touch "$delete_dir/one.txt" "$delete_dir/two.txt" \
      "$delete_dir/nested/three.txt" "$delete_protected" "$note_file"
touch "$softlink_target_file"
touch "$relative_target_file"
touch "$absolute_target_file"

cd "$repo_dir"

# This test is about path translation, not about the system layout, so point
# SYS: at the build tree rather than letting the broker find whatever is
# installed on the machine running the test. C: then falls back to SYS:
# itself, which is where these binaries are, and S: holds no startup script
# to put output into what the assertions below read.
ACE_SYS_DIR="$repo_dir/build"
export ACE_SYS_DIR

cleanup()
{
    if [ "${broker_pid:-}" ]; then
        if kill -0 "$broker_pid" 2>/dev/null; then
            kill -TERM "$broker_pid" 2>/dev/null || true
            for attempt in $(seq 1 100); do
                kill -0 "$broker_pid" 2>/dev/null || break
                sleep 0.01
            done
        fi
    fi
    for file in "$socket_path" "$socket_path.lock" \
                "$socket_path.pid" "$socket_path.start.lock"; do
        [ -e "$file" ] && unlink "$file" 2>/dev/null || true
    done
    [ -e "$rename_source" ] && unlink "$rename_source" 2>/dev/null || true
    [ -e "$rename_target" ] && unlink "$rename_target" 2>/dev/null || true
    [ -e "$rename_question_source" ] && unlink "$rename_question_source" 2>/dev/null || true
    [ -e "$rename_question_target" ] && unlink "$rename_question_target" 2>/dev/null || true
    [ -e "$run_created_dir" ] && rmdir "$run_created_dir" 2>/dev/null || true
    chmod u+w "$delete_protected" 2>/dev/null || true
    rm -rf "$delete_dir" "$delete_protected" "$note_dir" 2>/dev/null || true
    [ -e "$relative_target_file" ] && unlink "$relative_target_file" 2>/dev/null || true
    [ -e "$softlink_target_file" ] && unlink "$softlink_target_file" 2>/dev/null || true
    [ -e "$absolute_target_file" ] && unlink "$absolute_target_file" 2>/dev/null || true
    [ -e "$case_collision_primary" ] && unlink "$case_collision_primary" 2>/dev/null || true
    [ -e "$case_collision_secondary" ] && unlink "$case_collision_secondary" 2>/dev/null || true
    for file in "$softlink_name" "$relative_softlink_name" \
                "$directory_softlink_name" "$nested_relative_softlink_name" \
                "$absolute_file_softlink_name" "$absolute_directory_softlink_name" \
                "$absolute_dangling_softlink_name"; do
        [ -L "$file" ] && unlink "$file" 2>/dev/null || true
    done
    rmdir "$test_dir" 2>/dev/null || true
    rmdir "$mapping_dot_create_dir" "$mapping_dotdot_create_dir" \
          "$mapping_dot_create_parent" "$mapping_union_second/only-second" \
          "$mapping_union_first" "$mapping_union_second" "$mapping_colon_dir" \
          "$mapping_long_dir" "$mapping_incompressible_dir" "$mapping_deflate_dir" \
          "$mapping_dot_dir" "$mapping_dotdot_dir" \
          "$case_collision_dir" \
          "$softlink_target_dir" \
          "$softlink_dir" \
          "$mapping_test_dir" \
          2>/dev/null || true
}
# Failure reporting.
#
# This file is a long run of assertions under "set -e", which means a check
# that fails ends the script by simply stopping -- no message, no line, exit
# 1.  Finding out which of seven hundred lines it was meant running the whole
# thing again under "sh -x" and reading backwards from the cleanup trap.
#
# assert() gives the ones that had no message of their own a message: the
# expression that failed, printed as written.  It also remembers the last
# check that passed, so a failure somewhere else -- a command that returned
# non-zero on its own, which "set -e" treats identically -- can still say
# where in the run it happened.
last_passed='(nothing yet)'
assert()
{
    if "$@"; then
        last_passed="$*"
        return 0
    fi
    printf 'filesystem translation test: FAILED: %s\n' "$*" >&2
    return 1
}

on_exit()
{
    status=$?
    cleanup
    [ "$status" -eq 0 ] && return 0
    printf 'filesystem translation test: exited %s after: %s\n' \
        "$status" "$last_passed" >&2
    return 0
}
trap on_exit EXIT HUP INT TERM

control()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
        "$repo_dir/build/ace-brokerctl" "$@"
}

# There is deliberately no broker startup here. The first client request must
# start the companion broker through native_broker_ensure().
root_name=$(control name "$repo_dir")
assert [ -S "$socket_path" ]
assert [ "$(control resolve "$root_name")" = "$repo_dir" ]

volume_name=${root_name%%:*}:
volume_root=$(control resolve "$volume_name")
assert [ "$(control resolve :)" = "$volume_root" ]

# AmigaDOS uses each leading slash as a parent traversal: / is the parent and
# // is the grandparent. Internal double slashes mean parent after the
# preceding component.
# Its current directory spelling is empty; . and .. are ordinary AmigaDOS
# names represented by the otherwise-illegal Linux components : and ::.
control cd "$root_name"
assert [ "$(control resolve /)" = "$(dirname "$repo_dir")" ]
assert [ "$(control resolve //)" = "$(dirname "$(dirname "$repo_dir")")" ]
assert [ "$(control resolve ///)" = "$(dirname "$(dirname "$(dirname "$repo_dir")")")" ]
mapped_dot=$(control name "$mapping_dot_dir")
mapped_dotdot=$(control name "$mapping_dotdot_dir")
assert [ "${mapped_dot##*/}" = . ]
assert [ "${mapped_dotdot##*/}" = .. ]
assert [ "$(control resolve "$mapped_dot")" = "$mapping_dot_dir" ]
assert [ "$(control resolve "$mapped_dotdot")" = "$mapping_dotdot_dir" ]

# The reverse direction is equally important: creating the two AmigaDOS
# names creates their fixed Linux spellings, rather than generic ^ mappings.
dot_create_parent_name=$(control name "$mapping_dot_create_parent")
dot_create_output=$(printf 'MakeDir %s/.\nMakeDir %s/..\nEndCLI\n' \
    "$dot_create_parent_name" "$dot_create_parent_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=filesystem-dot-create-test "$repo_dir/build/ace-user-shell")
assert [ -d "$mapping_dot_create_dir" ]
assert [ -d "$mapping_dotdot_create_dir" ]
case "$dot_create_output" in
    *"Error"*|*"error"*) echo "could not create mapped dot names" >&2; exit 1 ;;
esac

volume_relative=${root_name#*:}
volume_first=${volume_relative%%/*}
if [ -d "$volume_root/$volume_first" ]; then
    volume_first_host=$(realpath "$volume_root/$volume_first")
    [ "$(control resolve ":$volume_first")" = "$volume_first_host" ]
fi

# Only four things cannot be spelled: ':', '/', '^', and being too long or
# case-colliding.  Everything else -- spaces, punctuation, non-ASCII -- is an
# ordinary AmigaDOS filename character and must survive untouched.
for legal in 'plain name.txt' 'report(final)#1.txt' 'Grüße.txt'; do
    : > "$mapping_test_dir/$legal"
    legal_name=$(control name "$mapping_test_dir/$legal")
    if [ "${legal_name##*/}" != "$legal" ]; then
        echo "a legal AmigaDOS name was escaped: $legal -> ${legal_name##*/}" >&2
        exit 1
    fi
    [ "$(control resolve "$legal_name")" = "$(realpath "$mapping_test_dir/$legal")" ]
done

# A caret is only a marker when what follows it spells a payload.  These are
# ordinary names and must survive untouched -- including "x^AAAA", which is
# valid base32 but decodes to a host name with an embedded NUL, and so is not
# a payload ACE could ever have produced.
for carets in 'a^b.txt' 'caret^symbol' 'x^AAAA' 'a^b^AAAA'; do
    : > "$mapping_test_dir/$carets"
    caret_name=$(control name "$mapping_test_dir/$carets")
    if [ "${caret_name##*/}" != "$carets" ]; then
        echo "an ordinary caret was treated as a marker: $carets -> ${caret_name##*/}" >&2
        exit 1
    fi
    [ "$(control resolve "$caret_name")" = "$(realpath "$mapping_test_dir/$carets")" ]
done

# A host name that does look like an escape has to be escaped, so that it
# cannot be confused with the name it imitates.
: > "$mapping_test_dir/Hi^HJKGQZLSMUXHI6DUAA"
imitator=$(control name "$mapping_test_dir/Hi^HJKGQZLSMUXHI6DUAA")
if [ "${imitator##*/}" = "Hi^HJKGQZLSMUXHI6DUAA" ]; then
    echo "a name imitating an escape was not escaped" >&2
    exit 1
fi
assert [ "$(control resolve "$imitator")" = "$(realpath "$mapping_test_dir/Hi^HJKGQZLSMUXHI6DUAA")" ]

# Linux names that are not safe AROS components are spelled <header>^<base32>.
# The spelling is a pure function of the host name -- no broker state -- so it
# must be short enough for an AROS FileInfoBlock, visibly synthetic, resolve
# back to the exact host path, and be the same in every broker.  base32
# because AmigaDOS filesystems are case-insensitive, so the alphabet must not
# distinguish 'A' from 'a'.
only_base32()
{
    case $1 in
        ''|*[!ABCDEFGHIJKLMNOPQRSTUVWXYZ234567]*) return 1 ;;
    esac
    return 0
}
mapped_colon=$(control name "$mapping_colon_dir")
mapped_colon_component=${mapped_colon##*/}
only_base32 "${mapped_colon_component#*^}" ||
    { echo "unsafe component was not given a visible mapping: $mapped_colon" >&2; exit 1; }
assert [ "$(control resolve "$mapped_colon")" = "$(realpath "$mapping_colon_dir")" ]
# A name too long to spell as a literal escape gets one more chance: the
# tail component_split_point() chose to keep -- the same split the literal
# form already tried -- is compressed with whichever of two engines does
# better, PACK39 (direct radix packing of [a-z0-9_.-]) or raw DEFLATE.  150
# identical characters is the easy case for both.
mapped_long=$(control name "$mapping_long_dir") ||
    { echo "a compressible over-length name was refused" >&2; exit 1; }
mapped_long_component=${mapped_long##*/}
[ "${#mapped_long_component}" -le 107 ] ||
    { echo "compressed component exceeds the AROS FileInfoBlock limit: $mapped_long_component" >&2; exit 1; }
assert [ "$(control resolve "$mapped_long")" = "$(realpath "$mapping_long_dir")" ]

mapped_deflate=$(control name "$mapping_deflate_dir") ||
    { echo "a DEFLATE-compressible over-length name was refused" >&2; exit 1; }
mapped_deflate_component=${mapped_deflate##*/}
[ "${#mapped_deflate_component}" -le 107 ] ||
    { echo "DEFLATE-compressed component exceeds the AROS FileInfoBlock limit: $mapped_deflate_component" >&2; exit 1; }
assert [ "$(control resolve "$mapped_deflate")" = "$(realpath "$mapping_deflate_dir")" ]

# Not every over-length name can be rescued this way, and the fallback is to
# fail rather than guess: 106 base32 characters carry 530 bits, a name may be
# NAME_MAX bytes, and a positional radix pack or a general compressor can
# only do so much with genuinely high-entropy content -- there are simply
# more names than payloads at the limit.  A scheme that fails predictably on
# what compression cannot rescue is worth more than one that fails on a
# subset nobody can characterise.
if control name "$mapping_incompressible_dir" >/dev/null 2>&1; then
    echo "an uncompressible unspellable name was given a spelling: $(control name "$mapping_incompressible_dir")" >&2
    exit 1
fi

# Linux can hold two names AmigaDOS considers equal.  The lexical first keeps
# its visible spelling; every additional variant gets the ordinary broker ^
# escape so both entries remain addressable from ACE.
case_primary_name=$(control name "$case_collision_primary")
case_secondary_name=$(control name "$case_collision_secondary")
assert [ "${case_primary_name##*/}" = "CaseCollision" ]
case "${case_secondary_name##*/}" in
    *^*) only_base32 "${case_secondary_name##*^}" ||
        { echo "case-colliding mapping is not base32: $case_secondary_name" >&2; exit 1; } ;;
    *) echo "case-colliding component was not given a visible mapping: $case_secondary_name" >&2; exit 1 ;;
esac
assert [ "$(control resolve "$case_primary_name")" = "$(realpath "$case_collision_primary")" ]
assert [ "$(control resolve "$case_secondary_name")" = "$(realpath "$case_collision_secondary")" ]
# An ordinary case-insensitive path names the primary entry, never whichever
# entry Linux happened to return first.
case_ordinary_name=${case_primary_name%/*}/CASECOLLISION
assert [ "$(control resolve "$case_ordinary_name")" = "$(realpath "$case_collision_primary")" ]
case_collision_dir_name=$(control name "$case_collision_dir")
case_listing=$(printf 'DIR %s\nEndCLI\n' "$case_collision_dir_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=filesystem-test "$repo_dir/build/ace-user-shell")
printf '%s\n' "$case_listing" | grep -F -q 'CaseCollision'
printf '%s\n' "$case_listing" | grep -F -q "${case_secondary_name##*/}"
mapped_dir_output=$(printf 'DIR %s OPT A\nEndCLI\n' "$mapped_colon" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=filesystem-test "$repo_dir/build/ace-user-shell")
case "$mapped_dir_output" in
    *"Could not get information"*|*"Error 2"*)
        echo "mapped directory could not be opened" >&2
        exit 1
        ;;
esac

# A softlink is itself a directory entry, even if its target is gone.  This
# is the AmigaDOS distinction between examining a link and following it:
# ExNext reports ST_SOFTLINK, while a subsequent Lock on the name fails.
ln -s "$softlink_dir/missing-target" "$softlink_name"
ln -s '../missing-target' "$relative_softlink_name"
ln -s 'target-directory' "$directory_softlink_name"
ln -s 'target-directory/../relative-target.txt' "$nested_relative_softlink_name"
ln -s "$absolute_target_file" "$absolute_file_softlink_name"
ln -s "$softlink_target_dir" "$absolute_directory_softlink_name"
ln -s "$absolute_missing_target" "$absolute_dangling_softlink_name"
softlink_dir_name=$(control name "$softlink_dir")
softlink_types=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" exnext-types "$softlink_dir_name")
case "$softlink_types" in
    *"dangling$(printf '\t')3"*) ;;
    *) echo "ExNext did not report dangling link as ST_SOFTLINK: $softlink_types" >&2; exit 1 ;;
esac
relative_softlink_target=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" readlink "$softlink_dir_name" relative-dangling)
[ "$relative_softlink_target" = "/missing-target" ] || {
    echo "relative softlink target was not translated to AmigaDOS: $relative_softlink_target" >&2
    exit 1
}
directory_softlink_target=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" readlink "$softlink_dir_name" directory-link)
[ "$directory_softlink_target" = "target-directory" ] || {
    echo "relative directory softlink target changed: $directory_softlink_target" >&2
    exit 1
}
nested_relative_target=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" readlink "$softlink_dir_name" nested-relative)
[ "$nested_relative_target" = "target-directory//relative-target.txt" ] || {
    echo "nested relative softlink target was not translated: $nested_relative_target" >&2
    exit 1
}
control cd "$softlink_dir_name"
[ "$(control resolve "$nested_relative_target")" = "$relative_target_file" ] || {
    echo "translated nested softlink target did not resolve correctly" >&2
    exit 1
}
absolute_target_name=$(control name "$absolute_target_file")
absolute_file_target=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" readlink "$softlink_dir_name" absolute-file)
[ "$absolute_file_target" = "$absolute_target_name" ] || {
    echo "absolute file softlink target was not volume-qualified: $absolute_file_target" >&2
    exit 1
}
absolute_directory_name=$(control name "$softlink_target_dir")
absolute_directory_target=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" readlink "$softlink_dir_name" absolute-directory)
[ "$absolute_directory_target" = "$absolute_directory_name" ] || {
    echo "absolute directory softlink target was not volume-qualified: $absolute_directory_target" >&2
    exit 1
}
absolute_missing_name=$(control name "$absolute_missing_target")
absolute_dangling_target=$(env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" readlink "$softlink_dir_name" absolute-dangling)
[ "$absolute_dangling_target" = "$absolute_missing_name" ] || {
    echo "absolute dangling softlink target was not volume-qualified: $absolute_dangling_target" >&2
    exit 1
}
softlink_dir_output=$(printf 'DIR %s\nEndCLI\n' "$softlink_dir_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=filesystem-test "$repo_dir/build/ace-user-shell")
case "$softlink_dir_output" in
    *"dangling"*) ;;
    *) echo "DIR did not list dangling softlink: $softlink_dir_output" >&2; exit 1 ;;
esac
case "$softlink_dir_output" in
    *"Could not get information for $softlink_dir_name"*)
        echo "DIR failed while listing dangling softlink" >&2
        exit 1
        ;;
esac
softlink_all_output=$(printf 'DIR %s OPT A\nEndCLI\n' "$softlink_dir_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=filesystem-test "$repo_dir/build/ace-user-shell")
case "$softlink_all_output" in
    *"Could not get information"*|*"object not found"*|*"Object not found"*)
        echo "DIR OPT A followed a dangling softlink: $softlink_all_output" >&2
        exit 1
        ;;
esac
for entry in relative-dangling directory-link inside.txt; do
    case "$softlink_all_output" in
        *"$entry"*) ;;
        *) echo "DIR OPT A did not list and follow softlinks: $softlink_all_output" >&2; exit 1 ;;
    esac
done
if env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
    "$repo_dir/build/dos-comment-test" examine "$softlink_dir_name/dangling" \
    >/dev/null 2>&1; then
    echo "Lock followed a dangling softlink successfully" >&2
    exit 1
fi

# Assign is a true multi-directory union. The AROS GetDeviceProc() iterator
# must advance from the first AssignList target to the second when the object
# is absent from the first target.
union_first=$(control name "$mapping_union_first")
union_second=$(control name "$mapping_union_second")
union_output=$(printf 'Assign ACE_UNION: %s %s\nCD ACE_UNION:only-second\nCD\nEndCLI\n' \
    "$union_first" "$union_second" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-union-test "$repo_dir/build/ace-user-shell")
case "$union_output" in
    *"$union_second/only-second"*) ;;
    *) echo "multi-assign union did not search its later target" >&2; exit 1 ;;
esac

# Type and Rename are the unmodified AROS commands. Type exercises the
# existing Open/Read/Write seam; Rename exercises the host Rename/SameLock
# seam and the ReadArgs /M trailing-required-argument rule.
readme_name=$(control name "$repo_dir/README.md")
type_output=$(printf 'Type %s\nEndCLI\n' "$readme_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-type-test "$repo_dir/build/ace-user-shell")
case "$type_output" in
    *"ACE"*"AROS"*) ;;
    *) echo "Type did not print the translated README path" >&2; exit 1 ;;
esac
rename_source_name=$(control name "$rename_source")
rename_parent_name=$(control name "$mapping_test_dir")
rename_target_name="$rename_parent_name/rename-target"
rename_output=$(printf 'Rename %s AS=%s QUIET\nEndCLI\n' \
    "$rename_source_name" "$rename_target_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-rename-test "$repo_dir/build/ace-user-shell")
assert [ -e "$rename_target" ]
assert [ ! -e "$rename_source" ]
case "$rename_output" in
    *"required argument missing"*|*"object not found"*)
        echo "Rename failed through the AROS command seam" >&2
        exit 1
        ;;
esac
rename_question_source_name=$(control name "$rename_question_source")
rename_question_output=$(printf 'Rename ?\nFROM=%s AS=%s QUIET\nEndCLI\n' \
    "$rename_question_source_name" "$rename_parent_name/rename-question-target" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-rename-question-test "$repo_dir/build/ace-user-shell")
assert [ -e "$rename_question_target" ]
assert [ ! -e "$rename_question_source" ]
case "$rename_question_output" in
    *"required argument missing"*|*"object not found"*)
        echo "Rename '?' did not read its second argument line" >&2
        exit 1
        ;;
esac

# Stack is present as the AROS command and accepts a normal stack setting.
# Linux command processes do not inherit an Amiga stack-size contract, so the
# launcher deliberately ignores that setting rather than inventing one.
stack_output=$(printf 'Stack 4096\nEndCLI\n' |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-stack-test "$repo_dir/build/ace-user-shell")
case "$stack_output" in
    *"Requested size is too small"*)
        echo "Stack rejected a setting that should be accepted by the host seam" >&2
        exit 1
        ;;
esac

# Run starts an ACE command as a detached background process. Use a filesystem
# effect instead of output timing to prove the child really ran.
run_parent_name=$(control name "$mapping_test_dir")
run_created_name="$run_parent_name/run-created"
run_output=$(printf 'Run MakeDir %s\nEndCLI\n' "$run_created_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-run-test "$repo_dir/build/ace-user-shell")
run_completed=0
for attempt in $(seq 1 100); do
    if [ -d "$run_created_dir" ]; then
        run_completed=1
        break
    fi
    sleep 0.01
done
assert [ "$run_completed" -eq 1 ]
case "$run_output" in
    *"Run:"*"object not found"*)
        echo "Run could not launch the ACE command" >&2
        exit 1
        ;;
esac

# Delete and Protect are the unmodified AROS commands, and the first
# destructive use of the filesystem seam. Between them they exercise the real
# pattern matcher, the recursive ALL walk, and the protection bits in both
# directions: Protect writes them through SetProtection(), and Delete reads
# them back through Examine() to decide whether an object may be removed.
delete_dir_name=$(control name "$delete_dir")
delete_output=$(printf 'Delete %s/#?.txt\nDelete %s ALL\nEndCLI\n' \
    "$delete_dir_name" "$delete_dir_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-delete-test "$repo_dir/build/ace-user-shell")
assert [ ! -e "$delete_dir" ]
case "$delete_output" in
    *"Not Deleted"*)
        echo "Delete could not remove a pattern match or a directory tree" >&2
        exit 1
        ;;
esac

# Withdrawing the delete bit has to actually stop a deletion, or Delete's
# protection check is reading something ACE never wrote. FORCE then clears it
# and removes the file, which is the same seam in the other direction.
delete_protected_name=$(control name "$delete_protected")
protect_output=$(printf 'Protect %s d SUB\nDelete %s\nEndCLI\n' \
    "$delete_protected_name" "$delete_protected_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-protect-test "$repo_dir/build/ace-user-shell")
assert [ -e "$delete_protected" ]
case "$protect_output" in
    *"protected from deletion"*) ;;
    *)
        echo "Protect did not stop Delete from removing the file" >&2
        exit 1
        ;;
esac
force_output=$(printf 'Delete %s FORCE\nEndCLI\n' "$delete_protected_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-force-test "$repo_dir/build/ace-user-shell")
assert [ ! -e "$delete_protected" ]
case "$force_output" in
    *"Not Deleted"*)
        echo "Delete FORCE did not clear the protection it was given" >&2
        exit 1
        ;;
esac

# Filenote keeps an AmigaDOS file comment in an extended attribute, which is
# the one piece of Amiga file metadata with no Unix field to live in. The
# check is the round trip rather than the xattr, because reading the attribute
# back directly would prove only that setxattr ran: what has to hold is that
# Examine() and ExNext() put the comment in a FileInfoBlock, which is where
# every AmigaDOS caller looks for it. The command-level check below then proves
# that List's %C formatter sees the same metadata.
note_file_name=$(control name "$note_file")
note_dir_name=$(control name "$note_dir")
note_output=$(printf 'Filenote %s "kept on the inode"\nEndCLI\n' \
    "$note_file_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-filenote-test "$repo_dir/build/ace-user-shell")
case "$note_output" in
    *"Error"*|*"error"*)
        echo "Filenote could not set a comment" >&2
        exit 1
        ;;
esac
note_examine=$(env ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=shell-filenote-test \
    "$repo_dir/build/dos-comment-test" examine "$note_file_name")
case "$note_examine" in
    *"kept on the inode"*) ;;
    *) echo "Examine did not read the comment back: $note_examine" >&2; exit 1 ;;
esac
note_exnext=$(env ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=shell-filenote-test \
    "$repo_dir/build/dos-comment-test" exnext "$note_dir_name")
case "$note_exnext" in
    *"kept on the inode"*) ;;
    *) echo "ExNext did not read the comment back: $note_exnext" >&2; exit 1 ;;
esac
note_list=$(printf 'List %s LFORMAT "%%C"\nEndCLI\n' "$note_file_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=shell-list-comment-test "$repo_dir/build/ace-user-shell")
case "$note_list" in
    *"kept on the inode"*) ;;
    *) echo "List did not print the comment: $note_list" >&2; exit 1 ;;
esac

# An empty comment is how AmigaDOS clears one, and a comment past the 79
# characters a FileInfoBlock can carry is refused rather than truncated.
note_clear=$(printf 'Filenote %s ""\nEndCLI\n' "$note_file_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-filenote-clear-test "$repo_dir/build/ace-user-shell")
note_cleared=$(env ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=shell-filenote-test \
    "$repo_dir/build/dos-comment-test" examine "$note_file_name")
case "$note_cleared" in
    *"kept on the inode"*)
        echo "Filenote did not clear the comment" >&2
        exit 1
        ;;
esac
note_long=$(awk 'BEGIN { for (i = 0; i < 80; i++) printf "n" }')
note_toolong=$(printf 'Filenote %s "%s"\nEndCLI\n' "$note_file_name" "$note_long" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-filenote-long-test "$repo_dir/build/ace-user-shell")
case "$note_toolong" in
    *"too long"*) ;;
    *) echo "Filenote accepted a comment longer than a FileInfoBlock: $note_toolong" >&2; exit 1 ;;
esac

# Protect and Filenote both refuse to work on a whole volume without ALL,
# through AROS's own IsDosEntryA(). That call reads a DosList entry's name as
# a length-prefixed BSTR and gives up entirely on a NULL list, so it is the
# one caller that catches ACE disagreeing with AROS about either.
volume_refusal=$(printf 'Filenote %s x\nProtect %s w SUB\nEndCLI\n' \
    "$volume_name" "$volume_name" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=shell-volume-refusal-test "$repo_dir/build/ace-user-shell")
refusals=$(printf '%s\n' "$volume_refusal" | grep -c "not of required type" || true)
[ "$refusals" -eq 2 ] || {
    echo "Filenote and Protect did not both refuse a volume: $volume_refusal" >&2
    exit 1
}

broker_pid=$(sed -n '1p' "$socket_path.lock")
case "$broker_pid" in
    ''|*[!0-9]*) echo "broker lock did not contain a PID" >&2; exit 1 ;;
esac
kill -0 "$broker_pid"

tmp_type=$(stat -fc %T /tmp)
if [ "$tmp_type" = tmpfs ]; then
    tmp_name=$(control name /tmp)
    case "$tmp_name" in
        RAM:|RAM[0-9]*:) ;;
        *) echo "tmpfs translated to unexpected name: $tmp_name" >&2; exit 1 ;;
    esac
    [ "$(control resolve "$tmp_name")" = "$(realpath /tmp)" ]
fi

shell_output=$(printf 'Echo filesystem-ok\nEndCLI\n' |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-test \
    "$repo_dir/build/ace-user-shell")
case "$shell_output" in
    *"$root_name"*) ;;
    *) echo "shell prompt did not contain translated volume name" >&2; exit 1 ;;
esac

# The shell loader has a native current-directory lock of its own.  It must
# refresh that lock after a command process changes the shared broker session,
# or Shell.c's next command-load pass restores the old directory.
root_output=$(printf 'CD :\nCD\nEndCLI\n' |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-root-test \
    "$repo_dir/build/ace-user-shell")
case "$root_output" in
    *"> $volume_name"*) ;;
    *) echo "CD : did not reach the current volume root" >&2; exit 1 ;;
esac

# Shell.c also treats a directory-shaped command as a CD fallback.  A failed
# command lookup must not leave its ERROR_OBJECT_NOT_FOUND behind after the
# fallback succeeds.  Leading '/' is AROS parent-directory syntax: one slash
# moves up one level and additional slashes continue toward the volume root.
bare_path_output=$(printf '%s\nCD\n:\nCD\nEndCLI\n' "$volume_name$volume_first" |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-bare-path-test \
    "$repo_dir/build/ace-user-shell")
case "$bare_path_output" in
    *"object not found"*)
        echo "successful bare directory path retained a stale error" >&2
        exit 1
        ;;
    *"> $volume_name$volume_first"*"> $volume_name"*) ;;
    *) echo "bare directory path fallback did not change directories" >&2; exit 1 ;;
esac

slash_output=$(printf 'CD /\nCD\nCD //\nCD\nCD ///\nCD\nEndCLI\n' |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-slash-test \
    "$repo_dir/build/ace-user-shell")
case "$slash_output" in
    *"> $volume_name$volume_first"*"> $volume_name"*"> $volume_name"*) ;;
    *) echo "leading slash parent traversal is incorrect" >&2; exit 1 ;;
esac

if [ -d "$volume_root/$volume_first" ]; then
    volume_first_name="$volume_name$volume_first"
    relative_child=${volume_relative#*/}
    relative_child=${relative_child%%/*}
    relative_child_name="$volume_first_name/$relative_child"
    relative_output=$(printf 'CD :%s\nCD %s\nCD\nCD does-not-exist\nCD\nEndCLI\n' \
        "$volume_first" "$relative_child" |
        env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-relative-test \
        "$repo_dir/build/ace-user-shell")
    case "$relative_output" in
        *"$volume_first_name"*"$relative_child_name"*"object not found"*"$relative_child_name"*) ;;
        *) echo "current-volume paths or failed CD state are incorrect" >&2; exit 1 ;;
    esac
fi

# Assign is the unmodified AROS command.  Its DOS APIs are broker-backed so
# the assignment survives the child command process and is visible to the
# shell's resolver in the same session.
assign_list_output=$(timeout 5s env ACE_BROKER_SOCKET="$socket_path" \
    ACE_SESSION=shell-assign-list-test PATH="$repo_dir/build:$PATH" \
    sh -c 'printf "Assign\nEndCLI\n" | ace-user-shell')
case "$assign_list_output" in
    *"Volumes:"*) ;;
    *) echo "bare Assign did not list assignments" >&2; exit 1 ;;
esac
case "$assign_list_output" in
    *"/dev/"*)
        echo "bare Assign exposed a Linux block-device path" >&2
        exit 1
        ;;
esac

assign_output=$(printf 'Assign ACE_TEST: :\nAssign ACE_TEST: EXISTS\nCD ACE_TEST:\nCD\nAssign ACE_TEST: LIST\nEndCLI\n' |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-assign-test \
    PATH="$repo_dir/build:$PATH" "$repo_dir/build/ace-user-shell")
# The volume is named from whatever the host's own filesystem carries, so the
# expected spelling has to come from the broker rather than from a device name
# written into the test: a root filesystem with a label is named for the label,
# one without for its kernel device.
case "$assign_output" in
    *"ACE_TEST"*"$volume_name"*"$volume_name"*) ;;
    *) echo "Assign did not create or resolve a broker-backed assignment" >&2; exit 1 ;;
esac
case "$assign_output" in
    *"not assigned"*) echo "Assign EXISTS falsely reported no assignment" >&2; exit 1 ;;
esac

# Kill the broker and prove the next standalone client request starts a fresh
# one. This is the regression that the old test could not detect.
kill -TERM "$broker_pid"
for attempt in $(seq 1 100); do
    kill -0 "$broker_pid" 2>/dev/null || break
    sleep 0.01
done
recovered_name=$(control name "$repo_dir")
assert [ "$recovered_name" = "$root_name" ]
recovered_pid=$(sed -n '1p' "$socket_path.lock")
assert [ "$recovered_pid" != "$broker_pid" ]
kill -0 "$recovered_pid"
broker_pid=$recovered_pid

# The escaped spellings are a pure function of the host name, so the fresh
# broker must produce exactly what the dead one did, and must resolve names it
# has never seen before -- including the hashed form for a name too long to
# spell out, which it can only answer by encoding the directory forward.
# Previously each broker invented random suffixes, so every name minted before
# a restart became permanently unopenable.
for pair in "$mapping_colon_dir|$mapped_colon" \
            "$mapping_long_dir|$mapped_long" \
            "$case_collision_secondary|$case_secondary_name"; do
    host=${pair%%|*}
    before=${pair#*|}
    after=$(control name "$host")
    if [ "$after" != "$before" ]; then
        echo "mapping changed across broker restart: $before -> $after" >&2
        exit 1
    fi
    if [ "$(control resolve "$before")" != "$(realpath "$host")" ]; then
        echo "mapping did not resolve on a broker that never minted it: $before" >&2
        exit 1
    fi
done

echo "filesystem translation and broker recovery tests passed"
