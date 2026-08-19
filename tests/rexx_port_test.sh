#!/bin/sh
# The acceptance test for ARexx message ports: sendrexxmsg.c and listen4msg.c
# from the AROS tree, built and run unmodified.
#
# The point is that nothing was done to them. They are vendored under
# third_party/regina/rexxmast/ exactly as upstream wrote them, and if ACE
# implements the contract they compile, link and work. Anything that has to
# be adjusted in them is a bug in ACE.
#
# The two do not talk to each other: sendrexxmsg sends to "REXX" and
# listen4msg serves "TEST", because on a real Amiga the thing behind
# sendrexxmsg is RexxMast. So each runs against arexx-demo-peer, which is the
# counterpart written for this test rather than a modified demo.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$repo_dir/build"
work_dir=$(mktemp -d)
socket_path="$work_dir/broker.sock"
failures=0
# Everything here waits on a reply that has no timeout behind it, by design,
# so a regression does not fail -- it hangs. Bound each program instead.
run_timeout=${ACE_AREXX_TEST_TIMEOUT:-30}

cleanup()
{
    for pid in ${background_pids:-}; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    broker_pid=$(pgrep -f "ace-broker $socket_path" 2>/dev/null || true)
    if [ -n "$broker_pid" ]; then
        kill -TERM $broker_pid 2>/dev/null || true
    fi
    rm -rf "$work_dir"
}
trap cleanup EXIT HUP INT TERM

background_pids=""
export ACE_BROKER_SOCKET="$socket_path"

fail()
{
    echo "FAIL: $1" >&2
    failures=$((failures + 1))
}

# A port is registered when the broker says so. Polling that rather than
# sleeping, because a fixed sleep is either flaky or slow and usually both.
wait_for_port()
{
    tries=0
    while [ "$tries" -lt 200 ]; do
        if "$build_dir/ace-brokerctl" status 2>/dev/null |
                grep -q "^port	$1	"; then
            return 0
        fi
        tries=$((tries + 1))
        sleep 0.05
    done
    return 1
}

# --- listen4msg: receives a message and writes on the sender's stream -------
#
# listen4msg does Write(msg->rm_Stdin, "Hello\n", 6) -- to the *input* stream,
# which is correct on AmigaOS because rm_Stdin is a handle on the sender's
# console and a CON: handle is read/write. The sender's stdin here is a file
# opened read/write with <>, which is what makes that faithful.

sender_console="$work_dir/sender-console"
: > "$sender_console"
listen_output="$work_dir/listen4msg.out"

"$build_dir/listen4msg" > "$listen_output" 2>&1 &
background_pids="$background_pids $!"

if wait_for_port TEST; then
    if timeout "$run_timeout" "$build_dir/arexx-demo-peer" send TEST \
            "a message for listen4msg" \
            > "$work_dir/peer-send.out" 2>&1 <> "$sender_console"; then
        wait $! 2>/dev/null || true
        grep -q "Received RexxMsg" "$listen_output" ||
            fail "listen4msg did not recognise the message as a RexxMsg"
        grep -q "a message for listen4msg" "$listen_output" ||
            fail "listen4msg did not print the argument it was sent"
        grep -q "^Hello$" "$sender_console" ||
            fail "listen4msg's Write(rm_Stdin) did not reach the sender"
    else
        fail "sending to listen4msg failed"
        cat "$work_dir/peer-send.out" >&2 || true
    fi
else
    fail "listen4msg never registered its TEST port"
fi
background_pids=""

# --- sendrexxmsg: sends, gets its own message back, prints the result -------

serve_output="$work_dir/peer-serve.out"
"$build_dir/arexx-demo-peer" serve REXX > "$serve_output" 2>&1 &
background_pids="$background_pids $!"

send_output="$work_dir/sendrexxmsg.out"
if wait_for_port REXX; then
    if timeout "$run_timeout" "$build_dir/sendrexxmsg" \
            > "$send_output" 2>&1; then
        grep -q "^Result1: 0$" "$send_output" ||
            fail "sendrexxmsg did not report Result1: 0"
        grep -q "^Result2: hello everybody$" "$send_output" ||
            fail "sendrexxmsg did not report the result string"
        # It prints this only after checking reply == msg itself.
        grep -q "^All OK$" "$send_output" ||
            fail "sendrexxmsg did not get its own message back"
        grep -q "say hello everybody" "$serve_output" ||
            fail "the command sendrexxmsg sent did not arrive intact"
    else
        fail "sendrexxmsg exited non-zero"
        cat "$send_output" >&2 || true
    fi
else
    fail "the REXX port was never registered"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed" >&2
    exit 1
fi
echo "arexx demos: ok"
