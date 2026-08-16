#!/bin/sh
# Drives one EDIT run on a real Amiga and prints what it said.
#
# tools/amiga-edit-chassis runs in an AmigaShell and polls a drawer the
# emulator shares with this host. Here we write the file to edit and the
# commands to run into that drawer, raise the "go" flag, and wait for the
# chassis to leave a VER log behind. One run per call, a second or two each,
# which is enough to check the clone against the real program without anyone
# typing transcripts.
#
#   WORK=/path/to/shared/drawer tools/amiga-edit-drive.sh input.txt 'M3;?'
#
# The chassis stops when a file named "stop" appears in the drawer.
set -eu
: "${WORK:?set WORK to the drawer the Amiga sees as WORK:}"
input=$1
commands=$2

cp "$input" "$WORK/in"
printf '%s\nSTOP\n' "$commands" > "$WORK/cmd"
rm -f "$WORK/done" "$WORK/ver"
echo go > "$WORK/go"

waited=0
while [ ! -f "$WORK/done" ]; do
    waited=$((waited + 1))
    if [ "$waited" -gt 60 ]; then
        echo "the Amiga did not answer; is the chassis running?" >&2
        exit 1
    fi
    sleep 1
done
cat "$WORK/ver" 2>/dev/null || true
