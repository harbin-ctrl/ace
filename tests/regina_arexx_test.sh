#!/bin/sh
# Run Regina's six Amiga ARexx acceptance scripts against one RexxMast.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
mast_pid=
timeout_seconds=${REGINA_AREXX_TIMEOUT:-30}

cleanup()
{
    if [ -n "$mast_pid" ]; then
        kill "$mast_pid" 2>/dev/null || true
        wait "$mast_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

fail()
{
    echo "regina ARexx acceptance: $*" >&2
    exit 1
}

"$root/build/rexxmast" >"$tmp/rexxmast.log" 2>&1 &
mast_pid=$!
sleep 1
kill -0 "$mast_pid" 2>/dev/null || fail "RexxMast exited during startup"

run_script()
{
    name=$1
    output="$tmp/$name.out"
    error="$tmp/$name.err"

    if ! (cd "$tmp" && timeout "$timeout_seconds" \
        "$root/build/rexx" "$root/third_party/regina/arexx_test/$name.rexx" \
        >"$output" 2>"$error"); then
        echo "--- $name stdout ---" >&2
        sed -n '1,120p' "$output" >&2 || true
        echo "--- $name stderr ---" >&2
        sed -n '1,120p' "$error" >&2 || true
        fail "$name failed"
    fi
    if grep -Eiq 'segmentation fault|object not found|alias: error|speaks protocol' \
        "$output" "$error"; then
        echo "--- $name stdout ---" >&2
        sed -n '1,120p' "$output" >&2 || true
        echo "--- $name stderr ---" >&2
        sed -n '1,120p' "$error" >&2 || true
        fail "$name reported a runtime error"
    fi
}

same_output()
{
    name=$1
    shift
    printf '%s\n' "$@" | cmp -s - "$tmp/$name.out" || {
        echo "--- expected $name ---" >&2
        printf '%s\n' "$@" >&2
        echo "--- actual $name ---" >&2
        sed -n '1,120p' "$tmp/$name.out" >&2 || true
        fail "$name output mismatch"
    }
}

# The resource is registered by one Regina process and consumed by fresh
# processes below. This exercises RexxMast's resource replay as well as the
# library dispatch itself.
run_script addsupport
same_output addsupport 1

run_script typepkt
typepkt_lines=$(wc -l <"$tmp/typepkt.out")
[ "$typepkt_lines" -eq 8 ] || fail "typepkt output has $typepkt_lines lines"
sed -n '1p' "$tmp/typepkt.out" | grep -Eq '^[0-9A-Fa-f]{16}$' || fail "typepkt allocation is not a pointer"
sed -n '2p' "$tmp/typepkt.out" | grep -Eq '^[0-9A-Fa-f]{8}$' || fail "typepkt action result is not a ULONG"
if sed -n '3,5p' "$tmp/typepkt.out" | grep -Evq '^(0|1|-1|-2)$'; then
    fail "typepkt invalid-action results are wrong"
fi
sed -n '6p' "$tmp/typepkt.out" | grep -Eq '^[0-9A-Fa-f]{16}$' || fail "typepkt offset is not a pointer"
sed -n '7p' "$tmp/typepkt.out" | grep -Eq '^[0-9A-Fa-f]{8}$' || fail "IMPORT result is not a byte string"
[ "$(sed -n '8p' "$tmp/typepkt.out")" = TEST ] || fail "typepkt did not finish"

run_script forbid1
same_output forbid1 0 1 0 -1 0 -1 -2 -1

run_script forbid2
same_output forbid2 -2 -3 -2 -1 -2 -1 0 -1

run_script ptrarith
same_output ptrarith 0000000000000000 0400000000000000 0400000000000000

run_script ados
same_output ados 1 1 0 1 0
