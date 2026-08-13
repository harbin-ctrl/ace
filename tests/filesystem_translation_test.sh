#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ace-filesystem-test.XXXXXX")
socket_path="$test_dir/broker.sock"
log_path="$test_dir/broker.log"

cleanup()
{
    if [ "${broker_pid:-}" ]; then
        kill "$broker_pid" 2>/dev/null || true
        wait "$broker_pid" 2>/dev/null || true
    fi
    rmdir "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

(
    cd "$repo_dir"
    exec env ACE_BROKER_SOCKET="$socket_path" \
        "$repo_dir/build/ace-broker" "$socket_path"
) >"$log_path" 2>&1 &
broker_pid=$!
for attempt in $(seq 1 100); do
    [ -S "$socket_path" ] && break
    sleep 0.01
done
[ -S "$socket_path" ]

control()
{
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=filesystem-test \
        "$repo_dir/build/ace-brokerctl" "$@"
}

root_name=$(control name "$repo_dir")
[ "$(control resolve "$root_name")" = "$repo_dir" ]

tmp_type=$(stat -fc %T /tmp)
if [ "$tmp_type" = tmpfs ]; then
    tmp_name=$(control name /tmp)
    case "$tmp_name" in
        RAM:|RAM[0-9]*:) ;;
        *) echo "tmpfs translated to unexpected name: $tmp_name" >&2; exit 1 ;;
    esac
    [ "$(control resolve "$tmp_name")" = "$(realpath /tmp)" ]
fi

printf 'Echo filesystem-ok\nEndCLI\n' |
    env ACE_BROKER_SOCKET="$socket_path" ACE_SESSION=shell-test \
    "$repo_dir/build/ace-user-shell" >/dev/null

echo "filesystem translation tests passed"
