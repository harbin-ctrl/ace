#!/bin/sh
# Drives one Dir run on a real Amiga against WORK:cmptest and prints what it
# said. Companion to tools/amiga-edit-drive.sh, using the same chassis
# (tools/amiga-edit-chassis) and the same WORK:go/WORK:done handshake, but
# Dir's target path cannot be threaded through an AmigaDOS script as a
# variable, so the chassis fixes it to WORK:cmptest and the caller populates
# that drawer before calling this.
#
#   WORK=/path/to/shared/drawer tools/amiga-dir-drive.sh dirall
#
# verb is one of: dirall dirplain dirfiles dirdirs dirpat dirc
# (dirc lists C: instead of WORK:cmptest, and ignores it).
set -eu
: "${WORK:?set WORK to the drawer the Amiga sees as WORK:}"
verb=$1

rm -f "$WORK/done" "$WORK/out"
touch "$WORK/v$verb"
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
cat "$WORK/out" 2>/dev/null || true
