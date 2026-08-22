#!/bin/sh
# Navigation, checked the way a person checks it: by typing at a shell.
#
# This test exists in this form because an earlier version of it did not do
# that.  It drove CD as a bare process against a broker nobody had attached
# to, which is a state no real session is ever in -- the privileged services
# start when the first --root shell *attaches*, so a broker with no shell on
# it has no device view, and a whole class of names resolves differently
# there than it does in front of a user.  It passed, and the first thing
# typed at a real prompt broke.  Everything below runs through ace-user-shell.
#
# The shape of what is being checked:
#
#   a directory has several names -- VOLUME:path, :path, a relative path from
#   the volume root, and each of those typed as a bare command instead of as
#   an argument to CD -- and every one of them must arrive at the same place
#   and report the same name on the way out.  A name that is not there must
#   fail in every form, say so, and leave the current directory alone.
#
# Cases are generated rather than written out, over every volume and drawer
# the machine actually has plus a random tree with random ownership.  The
# count is printed at the end and the test fails if it drops below 100: a
# navigation test that checks a dozen things is how the last one missed.
#
# Every run prints its seed.  ACE_NAV_SEED=<n> replays a failure exactly.
set -u

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
owner_uid=$(id -u)
tab=$(printf '\t')

if [ "$owner_uid" -eq 0 ]; then
    printf 'ACE navigation test skipped (must run as an ordinary user)\n'
    exit 0
fi
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n /usr/bin/true 2>/dev/null; then
    printf 'ACE navigation test skipped (no noninteractive root helper)\n'
    exit 0
fi

seed=${ACE_NAV_SEED:-$(( ($(date +%s) + $$) % 100000 ))}
printf 'ACE navigation test: seed %s\n' "$seed"

# The work directory has to be on the root filesystem rather than in /tmp: the
# shell reaches its startup script by ACE name, and a script under /tmp is on
# a different volume from the one that name would be relative to.
work=$(mktemp -d "$repo_dir/.ace-nav.XXXXXX")
# Deliberately not a hidden name.  A line beginning with "." is a Shell dot
# command (.bra, .key, .def, .dot ...), so a tree under /tmp/.something would
# make every bare-command case in this file test the dot parser instead of
# navigation.  That is pinned separately, below.
tree_root=$(mktemp -d /tmp/ace-nav-tree.XXXXXX)
sys_dir="$work/sys"
socket_path="$work/broker.sock"
broker_pid=

cleanup()
{
    if [ -n "$broker_pid" ] && kill -0 "$broker_pid" 2>/dev/null; then
        kill -TERM "$broker_pid" 2>/dev/null || true
        for _ in $(seq 1 200); do
            kill -0 "$broker_pid" 2>/dev/null || break
            sleep 0.01
        done
    fi
    sudo -n rm -rf "$tree_root" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

checks=0
failures=0
check_failed()
{
    failures=$((failures + 1))
    printf 'ACE navigation test: %s\n' "$1" >&2
}
fail()
{
    check_failed "$1"
    printf 'ACE navigation test: aborted (seed %s)\n' "$seed" >&2
    exit 1
}

mkdir -p "$sys_dir/C" "$sys_dir/S"
for command in Echo CD List Type Dir Which Assign MakeDir Delete FailAt; do
    [ -x "$repo_dir/build/$command" ] &&
        cp "$repo_dir/build/$command" "$sys_dir/C/"
done

# --------------------------------------------------------------------------
# The random tree.
# --------------------------------------------------------------------------
manifest="$work/manifest"
awk -v seed="$seed" 'BEGIN {
    srand(seed);
    n = 0;
    dirs[n++] = "";
    total = 0; attempts = 0;
    while (total < 10 && attempts++ < 500) {
        parent = dirs[int(rand() * n)];
        depth = (parent == "") ? 0 : gsub(/\//, "/", parent) + 1;
        if (depth > 3) continue;
        name = sprintf("%s%s%d", substr("dcabefgh", int(rand()*8)+1, 1),
                       substr("xyzw", int(rand()*4)+1, 1), int(rand()*90)+10);
        if (rand() < 0.2) name = name ".d";
        path = (parent == "") ? name : parent "/" name;
        if (path in seen) continue;
        seen[path] = 1;
        r = rand();
        if (r < 0.25)      { owner = "root"; mode = "700"; }
        else if (r < 0.40) { owner = "root"; mode = "755"; }
        else               { owner = "user"; mode = "755"; }
        printf "D %s %s %s\n", path, owner, mode;
        dirs[n++] = path;
        total++;
    }
    for (i = 0; i < 12; i++) {
        parent = dirs[int(rand() * n)];
        name = sprintf("f%d.txt", int(rand()*900)+100);
        path = (parent == "") ? name : parent "/" name;
        if (path in seen) continue;
        seen[path] = 1;
        r = rand();
        if (r < 0.30) { owner = "root"; mode = "600"; }
        else          { owner = "user"; mode = "644"; }
        printf "F %s %s %s\n", path, owner, mode;
    }
}' | LC_ALL=C sort -u > "$manifest"
# LC_ALL=C is load-bearing.  A locale-aware sort collates punctuation weakly
# and puts "D a/b root 700" ahead of "D a user 755" -- the child before its
# own parent -- so mkdir -p creates the parent as root, and the parent's own
# line then arrives to find a directory it does not own.

while read -r kind path owner mode; do
    full="$tree_root/$path"
    # Asked before trying rather than after failing: a refused redirection is
    # reported by the shell itself, where a command's own 2>/dev/null cannot
    # reach it, and the run fills with noise that is not a failure.
    if [ "$owner" = user ] && [ ! -w "$(dirname "$full")" ]; then
        owner=root
        [ "$kind" = F ] && mode=644
    fi
    if [ "$kind" = D ]; then
        if [ "$owner" = root ]; then
            sudo -n mkdir -p "$full" && sudo -n chmod "$mode" "$full" ||
                fail "could not create the root-owned directory $path"
        else
            mkdir -p "$full" && chmod "$mode" "$full" ||
                fail "could not create the directory $path"
        fi
    else
        if [ "$owner" = root ]; then
            sudo -n sh -c "printf '%s' '$path' > '$full'" &&
                sudo -n chmod "$mode" "$full" ||
                fail "could not create the root-owned file $path"
        else
            printf '%s' "$path" > "$full" && chmod "$mode" "$full" ||
                fail "could not create the file $path"
        fi
    fi
    printf '%s %s %s %s\n' "$kind" "$path" "$owner" "$mode"
done < "$manifest" > "$manifest.made"
mv "$manifest.made" "$manifest"

closed_dir=$(awk '$1=="D" && $3=="root" && $4=="700" {print $2; exit}' "$manifest")
secret_file=$(awk '$1=="F" && $3=="root" && $4=="600" {print $2; exit}' "$manifest")

# --------------------------------------------------------------------------
# Sessions.
# --------------------------------------------------------------------------
start_broker()
{
    if [ "${1-}" = --root ]; then
        ACE_SYS_DIR="$sys_dir" "$repo_dir/build/ace-broker" --root \
            "$socket_path" >"$work/broker.log" 2>&1 &
    else
        ACE_SYS_DIR="$sys_dir" "$repo_dir/build/ace-broker" \
            "$socket_path" >"$work/broker.log" 2>&1 &
    fi
    broker_pid=$!
    for _ in $(seq 1 400); do
        [ -S "$socket_path" ] && return 0
        kill -0 "$broker_pid" 2>/dev/null || return 1
        sleep 0.05
    done
    return 1
}

stop_broker()
{
    [ -n "$broker_pid" ] || return 0
    kill -TERM "$broker_pid" 2>/dev/null || true
    for _ in $(seq 1 400); do
        kill -0 "$broker_pid" 2>/dev/null || break
        sleep 0.01
    done
    broker_pid=
    rm -f "$socket_path" "$socket_path.lock"
}

privilege=root
view=device
ctl() {
    ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
        ACE_MODE_PRIVILEGE="$privilege" ACE_MODE_VIEW="$view" \
        ACE_MODE_OWNER_UID="$owner_uid" "$repo_dir/build/ace-brokerctl" "$@"
}

# One shell, one script, however many cases.  Through ace-user-shell rather
# than by invoking CD directly, because attaching a shell is what brings the
# privileged services up, and because the bare-command form of a name is
# something only the shell does.
run_shell()
{
    ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
        ACE_SESSION="nav-$1" ACE_MODE_PRIVILEGE="$privilege" \
        ACE_MODE_VIEW="$view" ACE_MODE_OWNER_UID="$owner_uid" \
        ACE_STARTUP_SCRIPT="$root_volume:${2#/}" \
        "$repo_dir/build/ace-user-shell" </dev/null 2>&1
}

# --------------------------------------------------------------------------
# The case table.  One line per case:
#
#     id <tab> setup <tab> command <tab> expected-name <tab> expect-error
#
# "setup" is a CD that puts the session somewhere known, so no case can be
# influenced by the one before it.  "expected-name" is what CD must report
# afterwards; for a case that is supposed to fail, that is the setup
# directory, unchanged.
# --------------------------------------------------------------------------
table="$work/cases"
: > "$table"
case_id=0
add_case()
{
    case_id=$((case_id + 1))
    printf 'c%s\t%s\t%s\t%s\t%s\n' "$case_id" "$1" "$2" "$3" "$4" >> "$table"
}

# Every way of naming one directory.  "elsewhere" is a volume root on some
# other volume, so the absolute forms are shown to be independent of wherever
# the session happens to be standing.
add_all_forms()
{
    volume=$1
    relative=$2
    elsewhere=$3
    expect_error=$4

    if [ -z "$relative" ]; then
        add_case "$elsewhere" "CD $volume:" "$volume:" "$expect_error"
        add_case "$elsewhere" "$volume:" "$volume:" "$expect_error"
        return
    fi
    target="$volume:$relative"
    if [ "$expect_error" = yes ]; then
        # A failing case must leave the session where it was, so what it is
        # expected to report is the setup directory rather than the target.
        add_case "$elsewhere" "CD $volume:$relative" "$elsewhere" yes
        add_case "$elsewhere" "$volume:$relative" "$elsewhere" yes
        add_case "$volume:" "CD :$relative" "$volume:" yes
        add_case "$volume:" ":$relative" "$volume:" yes
        add_case "$volume:" "CD $relative" "$volume:" yes
        add_case "$volume:" "$relative" "$volume:" yes
        return
    fi
    # 1. CD VOLUME:path, from another volume entirely.
    add_case "$elsewhere" "CD $volume:$relative" "$target" no
    # 2. The same, typed as a bare command: the shell's implicit CD.
    add_case "$elsewhere" "$volume:$relative" "$target" no
    # 3. CD :path, standing on the volume's root.
    add_case "$volume:" "CD :$relative" "$target" no
    # 4. :path as a bare command.
    add_case "$volume:" ":$relative" "$target" no
    # 5. A relative path from the volume root.
    add_case "$volume:" "CD $relative" "$target" no
    # 6. The relative path as a bare command.
    add_case "$volume:" "$relative" "$target" no
    # 7. :path from somewhere else on the same volume.  This is the case that
    #    tells "the current volume's root" apart from "the current
    #    directory": they are the same thing only at the root.
    add_case "$target" "CD :$relative" "$target" no
}

# --------------------------------------------------------------------------
# What this machine actually has.
# --------------------------------------------------------------------------
start_broker --root || fail 'the authorised broker did not start'

root_volume=$(ctl name / | sed 's/:.*//') ||
    fail 'the root filesystem has no ACE name'
[ -n "$root_volume" ] || fail 'the root filesystem named itself as nothing'

tree_name=$(ctl name "$tree_root") || fail 'the random tree has no ACE name'
tree_volume=${tree_name%%:*}
tree_prefix=${tree_name#*:}
[ -n "$tree_volume" ] || fail 'the random tree is on a nameless volume'
[ "$tree_volume" != "$root_volume" ] ||
    fail 'the random tree landed on the root volume, so nothing is cross-volume'

root_device=$(stat -c %d /)

# Ordinary drawers: on the root device, so the host's own mount shows them and
# no privilege is involved in reaching one.
ordinary=
for candidate in home usr etc var boot opt srv lib; do
    [ -d "/$candidate" ] || continue
    [ "$(stat -c %d "/$candidate")" = "$root_device" ] || continue
    ordinary="$ordinary $candidate"
done

# Covered drawers: a Linux mount sits on top, so the directory underneath is
# on the root device and visible only through the device view.  These need the
# privileged side, and they are what a lazily-started device view gets wrong.
covered=
for candidate in dev proc sys run tmp mnt media; do
    [ -d "/$candidate" ] || continue
    [ "$(stat -c %d "/$candidate")" = "$root_device" ] && continue
    covered="$covered $candidate"
done

# Drawers two levels down, so nothing here can pass by accident on a
# single-component path.
deep=
for candidate in usr/share usr/lib etc/default var/lib "home/$(id -un)"; do
    [ -d "/$candidate" ] || continue
    [ "$(stat -c %d "/$candidate")" = "$root_device" ] || continue
    deep="$deep $candidate"
done

# A hidden drawer, in the work directory rather than at a volume root, so
# nothing has to be created anywhere that outlives the test.  Its parent is
# the setup directory for the dot cases, which is what makes the bare form of
# its name begin a line with ".".
dot_drawer=hidden-drawer
mkdir -p "$work/.$dot_drawer" || fail 'could not make the hidden drawer'
work_here="$root_volume:${work#/}"

tree_dirs=$(awk '$1=="D" {print $2}' "$manifest")
tree_files=$(awk '$1=="F" {print $2}' "$manifest")

# --------------------------------------------------------------------------
# The authorised session's cases.
# --------------------------------------------------------------------------
add_all_forms "$root_volume" "" "$tree_volume:" no
add_all_forms "$tree_volume" "" "$root_volume:" no
for relative in $ordinary $deep $covered; do
    add_all_forms "$root_volume" "$relative" "$tree_volume:" no
done
for relative in $tree_dirs; do
    add_all_forms "$tree_volume" "$tree_prefix/$relative" "$root_volume:" no
done

# Names that are not there.  The commonest thing anyone types, and it has to
# fail the same way in every form and leave the session where it was.
for missing in nothing-here no/such/drawer usr/nothing-here; do
    add_all_forms "$root_volume" "$missing" "$tree_volume:" yes
done

# AmigaDOS parent traversal: "/" is the parent directory.  Pinned because it
# is the one place where an ACE name and a Linux path of the same spelling
# mean different things.
for relative in $deep; do
    add_case "$root_volume:$relative" "CD /" "$root_volume:$(dirname "$relative")" no
done
for relative in $ordinary; do
    add_case "$root_volume:$relative" "CD /" "$root_volume:" no
done

# CD with no argument changes nothing.  Not a case of its own: every block in
# this file ends with a bare CD and compares what it printed, so the no-argument
# form is already asserted by every other case here.

# A name beginning with "." is a Shell dot command -- .bra, .ket, .key, .def,
# .dol, .dot, .popis, .pushis, and ". " for a comment -- and anything else
# after the dot is "filesystem action type unknown".  That is the real
# AmigaDOS Shell, compiled from AROS source, and not something ACE chose; it
# is pinned here so that nobody reports it as a navigation bug twice, and so
# that a change to the dot parser cannot silently swallow ordinary names.
#
# CD reaches such a directory perfectly well.  Only the bare-command form is
# claimed by the dot parser, because only the bare-command form is a line.
add_case "$work_here" ".$dot_drawer" "$work_here" yes
add_case "$work_here" "CD .$dot_drawer" "$work_here/.$dot_drawer" no
add_case "$work_here" "CD ../$dot_drawer" "$work_here" yes

# --------------------------------------------------------------------------
# Run a table through one shell and check every case in it.
# --------------------------------------------------------------------------
run_table()
{
    label=$1
    script="$work/$label.script"
    # A script stops at the first command that fails, and half these cases are
    # supposed to fail.  Without this the run ends at the first missing name
    # and every case after it reports nothing, which reads like a hundred
    # failures caused by one.
    printf 'FailAt 100\n' > "$script"
    while IFS="$tab" read -r id setup command want_name want_error; do
        printf 'CD %s\n' "$setup" >> "$script"
        printf 'Echo "@@%s"\n' "$id" >> "$script"
        printf '%s\n' "$command" >> "$script"
        printf 'CD\n' >> "$script"
    done < "$table"

    run_shell "$label" "$script" > "$work/$label.out" 2>&1

    # One record per case: the last line it printed -- which is CD's answer,
    # since CD is always the last command in a block -- and whether anything
    # was printed before it, which is an error message.
    awk -v OFS="$tab" '
        /^@@/ {
            if (id != "") print id, last, (extra ? "yes" : "no");
            id = substr($0, 3); last = ""; extra = 0; next;
        }
        id == "" { next }
        /^Process [0-9]+ ending/ { next }
        /^[ \t]*$/ { next }
        { if (last != "") extra = 1; last = $0 }
        END { if (id != "") print id, last, (extra ? "yes" : "no") }
    ' "$work/$label.out" > "$work/$label.results"

    while IFS="$tab" read -r id setup command want_name want_error; do
        got=$(awk -F"$tab" -v want="$id" \
            '$1==want {print $2; found=1} END{if(!found) print "<no-answer>"}' \
            "$work/$label.results")
        got_error=$(awk -F"$tab" -v want="$id" \
            '$1==want {print $3; found=1} END{if(!found) print "?"}' \
            "$work/$label.results")

        checks=$((checks + 1))
        [ "$got" = "$want_name" ] ||
            check_failed "$label: [$command] from [$setup] reached [$got], wanted [$want_name]"

        checks=$((checks + 1))
        if [ "$got_error" != "$want_error" ]; then
            if [ "$want_error" = yes ]; then
                check_failed "$label: [$command] from [$setup] failed silently; a name that is not there must say so"
            else
                check_failed "$label: [$command] from [$setup] reported an error it should not have"
            fi
        fi
    done < "$table"
}

run_table root

# --------------------------------------------------------------------------
# Reading, in the same authorised session: every file in the random tree,
# including the ones only root can open.  The contents are the path, so a read
# that landed on the wrong object names where it went.
# --------------------------------------------------------------------------
read_script="$work/read.script"
printf 'FailAt 100\n' > "$read_script"
for relative in $tree_files; do
    printf 'Echo "@@r%s"\n' "$relative" >> "$read_script"
    printf 'Type %s:%s/%s\n' "$tree_volume" "$tree_prefix" "$relative" \
        >> "$read_script"
done
run_shell read-root "$read_script" > "$work/read.out" 2>&1
for relative in $tree_files; do
    got=$(awk -v want="@@r$relative" '
        $0 == want { grab = 1; next }
        /^@@/ { grab = 0 }
        grab && !/^Process [0-9]+ ending/ && NF { print; exit }
    ' "$work/read.out")
    checks=$((checks + 1))
    [ "$got" = "$relative" ] ||
        check_failed "root: $relative read back as [$got]"
done

# --------------------------------------------------------------------------
# What the authorised session leaves behind.
#
# This is the assertion that catches blanket escalation, and it is here rather
# than as a count of privileged processes because counting no longer says
# anything: a --root shell brings the fmm up when it attaches, so by the time
# anything is typed the count is already one whatever the seam does.
#
# Ownership still says it.  A session that tries as the user first leaves the
# user owning what they made in their own drawer; a session that reaches for
# privilege because of where a path pointed leaves root owning it, silently,
# and the user finds out later when they cannot edit their own file.
# --------------------------------------------------------------------------
own_dir=$(awk '$1=="D" && $3=="user" {print $2; exit}' "$manifest")
if [ -n "$own_dir" ]; then
    write_script="$work/write.script"
    printf 'FailAt 100\n' > "$write_script"
    printf 'MakeDir %s:%s/%s/made-drawer\n' \
        "$tree_volume" "$tree_prefix" "$own_dir" >> "$write_script"
    printf 'Echo "made" >%s:%s/%s/made-file\n' \
        "$tree_volume" "$tree_prefix" "$own_dir" >> "$write_script"
    run_shell write-root "$write_script" > "$work/write.out" 2>&1

    for made in made-drawer made-file; do
        checks=$((checks + 1))
        full="$tree_root/$own_dir/$made"
        if [ ! -e "$full" ]; then
            check_failed "root: $made was not created in the user own drawer"
            continue
        fi
        checks=$((checks + 1))
        [ "$(stat -c %u "$full")" = "$owner_uid" ] ||
            check_failed "root: $made came out owned by uid $(stat -c %u "$full"), not the user; something escalated work the user could do"
    done
fi

stop_broker

# --------------------------------------------------------------------------
# The same ground, in a session that never asked for --root.
#
# Everything on the root device behaves identically.  The covered drawers do
# not: what is underneath them lives in a namespace no user process is in, so
# they are root's to reach and this session is told they are not there.  That
# is worth pinning in both directions rather than assuming.
# --------------------------------------------------------------------------
privilege=user
view=mount
start_broker || fail 'the unprivileged broker did not start'

: > "$table"
case_id=0
add_all_forms "$root_volume" "" "$tree_volume:" no
for relative in $ordinary $deep; do
    add_all_forms "$root_volume" "$relative" "$tree_volume:" no
done
for relative in $covered; do
    add_all_forms "$root_volume" "$relative" "$tree_volume:" yes
done
for missing in nothing-here usr/nothing-here; do
    add_all_forms "$root_volume" "$missing" "$tree_volume:" yes
done
run_table user

# The protected objects, as the user: refused, and refused out loud.
user_run()
{
    ACE_SYS_DIR="$sys_dir" ACE_BROKER_SOCKET="$socket_path" \
        ACE_MODE_PRIVILEGE=user ACE_MODE_VIEW=mount \
        ACE_MODE_OWNER_UID="$owner_uid" "$@"
}
if [ -n "$secret_file" ]; then
    checks=$((checks + 1))
    # Compared, not matched.  The refusal names the file it refused, so a
    # substring test here reports a leak every time the refusal works.
    got=$(user_run "$repo_dir/build/Type" \
        "$tree_volume:$tree_prefix/$secret_file" 2>&1)
    [ "$got" != "$secret_file" ] ||
        check_failed "user: an unauthorised session read $secret_file"
fi
if [ -n "$closed_dir" ]; then
    inside=$(sudo -n ls -A "$tree_root/$closed_dir" 2>/dev/null | head -1)
    if [ -n "$inside" ]; then
        checks=$((checks + 1))
        # By column, because the header line names the drawer itself and an
        # Amiga name may contain spaces.
        got=$(user_run "$repo_dir/build/List" \
            "$tree_volume:$tree_prefix/$closed_dir" 2>&1 |
            grep -v '^Directory "' | grep -Ev '(blocks?|bytes?) used' |
            cut -c1-29 | sed 's/  *$//' | grep -v '^$')
        case "$got" in
            *"$inside"*)
                check_failed "user: an unauthorised session listed $closed_dir" ;;
        esac
    fi
fi

# Nothing privileged was started for any of it.  Still worth counting on this
# side: an unauthorised session has no reason to bring a root process up, and
# a seam that escalated on sight of a path would do it here too.
checks=$((checks + 1))
running=0
for pid in $(pgrep -u 0 -x ace-fmm 2>/dev/null); do
    set -- $(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null)
    [ "${1:-}" = "$repo_dir/build/ace-fmm" ] && running=$((running + 1))
done
[ "$running" -eq 0 ] ||
    check_failed "user: an unauthorised session started $running privileged process(es)"

stop_broker

# --------------------------------------------------------------------------
# The size of the net, checked like anything else.  A navigation test that has
# quietly shrunk to a handful of cases is one that will miss the next thing,
# and the way that happens is by nobody looking.
# --------------------------------------------------------------------------
if [ "$checks" -lt 100 ]; then
    fail "only $checks checks ran; this test is meant to be broad"
fi

if [ "$failures" -ne 0 ]; then
    printf 'ACE navigation test: %s of %s checks failed (seed %s)\n' \
        "$failures" "$checks" "$seed" >&2
    exit 1
fi
printf 'ACE navigation test: %s checks passed (seed %s)\n' "$checks" "$seed"
