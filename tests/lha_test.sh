#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "$repo_dir/build/.ace-lha-test.XXXXXX")
socket_path="$test_dir/broker.sock"
broker_pid=""

mkdir "$test_dir/out" "$test_dir/dot-out"
printf 'LhA through ACE\n' >"$test_dir/input.txt"
printf 'colon name\n' >"$test_dir/:"
printf 'double-colon name\n' >"$test_dir/::"

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

export ACE_SYS_DIR="$repo_dir/build"

"$repo_dir/build/ace-broker" "$socket_path" &
broker_pid=$!
for _ in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.01
done
[ -S "$socket_path" ] || {
    echo "LhA test broker did not start" >&2
    exit 1
}

control()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=lha-test \
        "$repo_dir/build/ace-brokerctl" "$@"
}

run_shell()
{
    env PATH="$repo_dir/build:$PATH" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION=lha-test "$repo_dir/build/ace-user-shell"
}

root_name=$(control name "$test_dir")
archive_name="$root_name/archive.lha"
dot_archive_name="$root_name/dots.lha"

version_output=$(printf 'LhA --version\nEndCLI\n' | run_shell 2>&1)
case "$version_output" in
    *"LHa for UNIX for AROS version"*) ;;
    *)
        echo "ACE did not run the built LhA command" >&2
        echo "$version_output" >&2
        exit 1
        ;;
esac

ordinary_output=$(printf 'LhA c %s %s\nLhA l %s\nLhA x %s %s/\nEndCLI\n' \
    "$archive_name" "$root_name/input.txt" "$archive_name" \
    "$archive_name" "$root_name/out" | run_shell 2>&1)
[ -f "$test_dir/archive.lha" ] || {
    echo "LhA did not create an archive through ACE" >&2
    echo "$ordinary_output" >&2
    exit 1
}
[ -f "$test_dir/out/input.txt" ] || {
    echo "LhA did not extract through an assigned ACE path" >&2
    echo "$ordinary_output" >&2
    exit 1
}
cmp "$test_dir/input.txt" "$test_dir/out/input.txt"
case "$ordinary_output" in
    *"input.txt"*) ;;
    *)
        echo "LhA did not list the archive through ACE" >&2
        echo "$ordinary_output" >&2
        exit 1
        ;;
esac

# The broker presents these Linux spellings as the ordinary AmigaDOS names
# . and .., and the wrapper must pass the resolved files to LhA without
# allowing the host pathname to leak into the archive header.
dot_output=$(printf 'LhA c %s %s/. %s/..\nLhA x %s %s/\nEndCLI\n' \
    "$dot_archive_name" "$root_name" "$root_name" \
    "$dot_archive_name" "$root_name/dot-out" | run_shell 2>&1)
[ -f "$test_dir/dots.lha" ] || {
    echo "LhA did not archive Amiga . and .. names" >&2
    echo "$dot_output" >&2
    exit 1
}
[ -f "$test_dir/dot-out/:" ] || {
    echo "LhA did not extract the mapped . name" >&2
    echo "$dot_output" >&2
    exit 1
}
[ -f "$test_dir/dot-out/::" ] || {
    echo "LhA did not extract the mapped .. name" >&2
    echo "$dot_output" >&2
    exit 1
}
cmp "$test_dir/:" "$test_dir/dot-out/:"
cmp "$test_dir/::" "$test_dir/dot-out/::"

echo "LhA ACE integration passed"
