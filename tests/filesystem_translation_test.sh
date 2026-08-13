#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ace-filesystem-test.XXXXXX")
socket_path="$test_dir/broker.sock"
mapping_test_dir=$(mktemp -d "$repo_dir/.ace-filesystem-mapping.XXXXXX")
mapping_colon_dir="$mapping_test_dir/Hi:This:is:a:long:filename:blahblahblah"
mapping_long_component=$(awk 'BEGIN { for (i = 0; i < 150; i++) printf "l" }')
mapping_long_dir="$mapping_test_dir/$mapping_long_component"
mapping_union_first="$mapping_test_dir/union-first"
mapping_union_second="$mapping_test_dir/union-second"
mkdir "$mapping_colon_dir" "$mapping_long_dir" "$mapping_union_first" \
      "$mapping_union_second" "$mapping_union_second/only-second"

cd "$repo_dir"

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
    rmdir "$test_dir" 2>/dev/null || true
    rmdir "$mapping_union_second/only-second" "$mapping_union_first" \
          "$mapping_union_second" "$mapping_colon_dir" "$mapping_long_dir" \
          "$mapping_test_dir" \
          2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

control()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
        "$repo_dir/build/ace-brokerctl" "$@"
}

# There is deliberately no broker startup here. The first client request must
# start the companion broker through native_broker_ensure().
root_name=$(control name "$repo_dir")
[ -S "$socket_path" ]
[ "$(control resolve "$root_name")" = "$repo_dir" ]

volume_name=${root_name%%:*}:
volume_root=$(control resolve "$volume_name")
[ "$(control resolve :)" = "$volume_root" ]

volume_relative=${root_name#*:}
volume_first=${volume_relative%%/*}
if [ -d "$volume_root/$volume_first" ]; then
    volume_first_host=$(realpath "$volume_root/$volume_first")
    [ "$(control resolve ":$volume_first")" = "$volume_first_host" ]
fi

# Linux names that are not safe AROS components get a broker-lifetime,
# directory-specific spelling. The spelling must be short enough for an AROS
# FileInfoBlock, visibly synthetic, and resolve back to the exact host path.
mapped_colon=$(control name "$mapping_colon_dir")
mapped_colon_component=${mapped_colon##*/}
case "$mapped_colon_component" in
    *^????????) ;;
    *) echo "unsafe component was not given a visible mapping: $mapped_colon" >&2; exit 1 ;;
esac
[ "$(control resolve "$mapped_colon")" = "$(realpath "$mapping_colon_dir")" ]
mapped_long=$(control name "$mapping_long_dir")
mapped_long_component=${mapped_long##*/}
[ "${#mapped_long_component}" -le 107 ]
case "$mapped_long_component" in
    *^????????) ;;
    *) echo "long component was not mapped: $mapped_long_component" >&2; exit 1 ;;
esac
[ "$(control resolve "$mapped_long")" = "$(realpath "$mapping_long_dir")" ]
mapped_dir_output=$(printf 'DIR %s OPT A\nEndCLI\n' "$mapped_colon" |
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=filesystem-test "$repo_dir/build/ace-user-shell")
case "$mapped_dir_output" in
    *"Could not get information"*|*"Error 2"*)
        echo "mapped directory could not be opened" >&2
        exit 1
        ;;
esac

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
        *"$volume_first_name"*"$relative_child_name"*"CD: Error 2"*"$relative_child_name"*) ;;
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

assign_output=$(printf 'Assign ACE_TEST: :\nAssign ACE_TEST: EXISTS\nCD ACE_TEST:\nCD\nAssign ACE_TEST: LIST\nEndCLI\n' |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-assign-test \
    PATH="$repo_dir/build:$PATH" "$repo_dir/build/ace-user-shell")
case "$assign_output" in
    *"ACE_TEST"*"sda2:"*"$volume_name"*) ;;
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
[ "$recovered_name" = "$root_name" ]
recovered_pid=$(sed -n '1p' "$socket_path.lock")
[ "$recovered_pid" != "$broker_pid" ]
kill -0 "$recovered_pid"

echo "filesystem translation and broker recovery tests passed"
