#!/bin/sh
# Runs the port-channel test against a broker of its own.
#
# Not a convenience. The broker's socket name is keyed to the protocol
# version, so a change confined to broker.c reuses whatever broker is already
# running for this user -- the test then exercises the old binary and passes
# whatever the new one does. It also counts what the broker is holding, which
# a live shell session in the background would perturb.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d)
socket_path="$test_dir/broker.sock"

# The broker is spawned by the test's first request, not by this script, so
# its pid is looked up here rather than remembered from a start command --
# and looked up inside cleanup, so that a failing test still takes it down.
cleanup()
{
    broker_pid=$(pgrep -f "ace-broker $socket_path" 2>/dev/null || true)
    if [ -n "$broker_pid" ]; then
        kill -TERM $broker_pid 2>/dev/null || true
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

# Deliberately no broker started here: the first request must bring up the
# companion broker through native_broker_ensure(), which is also how the
# child process in the test reaches the same one.
ACE_BROKER_SOCKET="$socket_path" "$repo_dir/build/broker-port-channel-test"
