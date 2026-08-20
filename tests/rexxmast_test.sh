#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$repo_dir/build"
mast_pid=""
mast_output=$(mktemp)

cleanup()
{
    if [ -n "$mast_pid" ]; then
        kill -TERM "$mast_pid" 2>/dev/null || true
        wait "$mast_pid" 2>/dev/null || true
    fi
    rm -f "$mast_output"
}
trap cleanup EXIT HUP INT TERM

ACE_SESSION=phase1-rexxmast "$build_dir/rexxmast" >"$mast_output" 2>&1 &
mast_pid=$!

ready=0
for attempt in $(seq 1 200); do
    if "$build_dir/ace-brokerctl" status 2>/dev/null |
            grep -q "^port[[:space:]]REXX[[:space:]]"; then
        ready=1
        break
    fi
    sleep 0.05
done
[ "$ready" -eq 1 ]

# This is the unmodified Regina-side ARexx client from the AROS tree.
timeout "${ACE_REXXMAST_TEST_TIMEOUT:-30}s" "$build_dir/sendrexxmsg" |
    grep -q '^All OK$'

# Several messages must be able to enter the upstream server loop at once;
# RexxMast deliberately runs each one in its own worker.
result_pids=
for worker in 1 2 3 4 5 6 7 8; do
    timeout "${ACE_REXXMAST_TEST_TIMEOUT:-30}s" \
        "$build_dir/rexxmast-result-test" &
    result_pids="$result_pids $!"
done
for result_pid in $result_pids; do
    wait "$result_pid"
done

timeout "${ACE_REXXMAST_TEST_TIMEOUT:-30}s" \
    "$build_dir/rexxmast-func-test"
timeout "${ACE_REXXMAST_TEST_TIMEOUT:-30}s" \
    "$build_dir/rexxmast-failure-test"

# ADDRESS REXX sends RXCOMM through the same public port. The quoted command
# is the Amiga convention that tells RexxMast to execute an instore program.
printf '%s\n' "ADDRESS REXX \"'say hello from RexxMast'\"" |
    ACE_SESSION=phase1-rexxmast \
    timeout "${ACE_REXXMAST_TEST_TIMEOUT:-30}s" "$build_dir/rexx" /dev/stdin
if ! grep -q '^HELLO FROM REXXMAST$' "$mast_output"; then
    cat "$mast_output" >&2
    exit 1
fi

timeout "${ACE_REXXMAST_TEST_TIMEOUT:-30}s" \
    "$build_dir/rexxmast-close-test"
closed=0
for attempt in $(seq 1 200); do
    if ! "$build_dir/ace-brokerctl" status 2>/dev/null |
            grep -q "^port[[:space:]]REXX[[:space:]]"; then
        closed=1
        break
    fi
    sleep 0.05
done
[ "$closed" -eq 1 ]
wait "$mast_pid"
mast_pid=""

echo "rexxmast phase 1: ok"
