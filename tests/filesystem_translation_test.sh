#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ace-filesystem-test.XXXXXX")
socket_path="$test_dir/broker.sock"

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
