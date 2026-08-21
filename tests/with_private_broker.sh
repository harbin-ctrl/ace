#!/bin/sh
# Runs a test binary against a broker of its own.
#
# Not a convenience. The broker's socket name is keyed to the protocol
# version, which is a hash of broker_protocol.h, so a change confined to
# broker.c reuses whatever broker is already running for this user -- the test
# then exercises the old binary and passes whatever that one does. A
# deliberately broken port-channel release passed this way before it was
# noticed. Tests that count what the broker is holding also need isolating
# from any live shell session in the background.
set -eu

if [ $# -lt 1 ]; then
    echo "use: $0 <test-binary> [args...]" >&2
    exit 2
fi

test_dir=$(mktemp -d)
socket_path="$test_dir/broker.sock"

# The broker is started by the test's first request, not here, so its pid is
# looked up rather than remembered -- and looked up inside cleanup, so a
# failing test still takes it down.
cleanup()
{
    broker_pid=
    if [ -r "$socket_path.lock" ]; then
        broker_pid=$(sed -n '1p' "$socket_path.lock" 2>/dev/null || true)
    fi
    if [ -z "$broker_pid" ]; then
        broker_pid=$(pgrep -f "ace-broker .*${socket_path}" 2>/dev/null || true)
    fi
    if [ -n "$broker_pid" ]; then
        kill -TERM $broker_pid 2>/dev/null || true
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

# Under timeout, because the thing these tests exercise is a wait with no
# timeout in it: a sender blocks in WaitPort() until its reply arrives, by
# design, so a regression here does not fail, it hangs. Better a clear
# non-zero exit than a run that never ends.
ACE_BROKER_TEST_TIMEOUT=${ACE_BROKER_TEST_TIMEOUT:-60}
timeout "$ACE_BROKER_TEST_TIMEOUT" env ACE_BROKER_SOCKET="$socket_path" "$@"
status=$?
if [ "$status" -eq 124 ]; then
    echo "$(basename "$1"): timed out after ${ACE_BROKER_TEST_TIMEOUT}s" >&2
fi
exit "$status"
