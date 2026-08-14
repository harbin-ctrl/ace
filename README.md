# ACE — AROS Command Environment

ACE is a Linux-hosted command environment built from a focused subset of AROS
shell and DOS behavior.
It intentionally does not build Workbench or the AROS kernel.  Original AROS
command sources are compiled against the compatibility headers in
`compat/include` and the broker-backed DOS implementation in `src`.

## Build and run

```sh
make
./build/ace-broker
```

The broker takes an optional socket path; with none it uses one socket per
user, `$XDG_RUNTIME_DIR/ace-broker.sock` (or `/tmp/ace-broker-<uid>.sock`
where there is no runtime directory). One broker serves all of a user's ACE
processes: a second one started on the same socket reports that one is
already running and exits rather than displacing it.

Install the built commands and console runtime into `/usr/local/bin`:

```sh
make install
```

Use `PREFIX` for a different installation root, for example
`make PREFIX="$HOME/.local" install`. All installed ACE programs share one
directory so they can locate the companion shell, console, broker, and AROS
commands at runtime.

For testing, the project also provides quiet lifecycle commands:

```sh
source ./broker-start
source ./broker-stop
```

When sourced, `broker-start` also prepends `build/` to `PATH`, and
`broker-stop` removes that exact entry. This is necessary because an executed
child script cannot modify its parent shell's environment. Both commands also
work as ordinary executables for broker lifecycle control, but only the
source form changes `PATH`.

Both commands honor `ACE_BROKER_SOCKET`; an optional
`ACE_BROKER_PIDFILE` selects the PID-file location. If the socket
variable is unset, they use the same default as the DOS client.

In another terminal:

```sh
export ACE_SESSION=my-shell
./build/CD .
./build/ace-brokerctl assign WORK: /tmp
./build/ace-brokerctl doslist
./build/MakeDir WORK:ace-test-one WORK:ace-test-two ALL
./build/Echo hello from AROS TO WORK:ace-shell-test
./build/PathPart FILE Work:dir/file.txt
./build/PathPart DIR Work:dir/file.txt
./build/PathPart ADD Work: dir2 file.txt
./build/Fault 205 212
printf 'y\n' | ./build/Ask Continue?
```

The native commands also support AmigaDOS template mode. A standalone `?`
prints the command's argument template and reads the actual arguments from
the next input line:

```sh
printf 'FILE Work:dir/file.txt\n' | ./build/PathPart ?
# DIR/K,FILE/K,ADD/K/M: file.txt
```

Entering another `?` redisplays the template, as in AmigaDOS.

`MakeDir` is the first filesystem-mutating command ported from the original
AROS source. It supports multiple directory names and the `ALL` switch for
creating intermediate directories. Its DOS argument layer now supports
multi-valued `/M` arguments and explicit `FreeArgs()` cleanup.

Every ACE command now parses its arguments with AROS's own `ReadArgs()`.
Commands declare their arguments either by calling it directly or with the
`AROS_SHn` macros, and both routes are AROS's: the macros are expanded by
AROS's own `compiler/include/aros/shcommands.h`, which is where the
`ReadArgs()`/`FreeArgs()` pair around a command's body lives. ACE supplies only
the host process entry point that header assumes, in
`compat/include/ace_shcommand_host.h`.

AmigaDOS leaves a command's argument line in its input stream for `ReadArgs()`
to read, and ACE does the same: the shell puts the line it parsed there, and a
command started straight from a Linux shell gets its `argv` put back together
into one, quoted the way `readitem.c` will take it apart again.

`Delete` and `Protect` are the first destructive commands, both built from
their original AROS sources. `Delete` uses the real pattern matcher, deletes
directory trees with `ALL`, and refuses an object whose delete bit is
withdrawn unless `FORCE` is given; `Protect` is what withdraws it:

```sh
./build/Delete WORK:build/#?.o
./build/Delete WORK:scratch ALL
./build/Protect WORK:keep.txt d SUB
./build/Delete WORK:keep.txt          # refused
./build/Delete WORK:keep.txt FORCE    # clears protection, then deletes
```

AmigaDOS's delete bit has no separate Unix permission -- on Unix it is the
containing directory that governs removal -- so it shares the owner write bit
with the write bit, which is the pairing ACE's `Examine()` already used in the
read direction. `Protect`'s other flags follow the same rule: the bits ACE can
express are the ones it maps, and the archive/pure/script trio is left
alone rather than guessed at.

`Dir` is now built from the original AROS `workbench/c/Dir.c` together with
the real AROS DOS pattern-matching and `ExAll()` sources. It exercises the
host filesystem seam through `Lock()`, `Examine()`, `ExNext()`, `DupLock()`,
and `CurrentDir()`, including recursive `ALL` listings and the `DIRS` and
`FILES` filters:

```sh
./build/Dir .
./build/Dir . ALL
./build/Dir src/#?.c FILES
```

AmigaDOS `#?` is the wildcard spelling used by the real AROS matcher. The
host seam deliberately leaves the optional `*`-as-wildcard root flag disabled,
so a bare `*` remains a literal pattern character, matching the configured
AmigaDOS behavior.

The broker owns per-session current directories and Assigns.  Its protocol is
deliberately small and binary; the DOS shim now has the first read-only host
filesystem lock/enumeration seam, while richer volume, mount, and device
semantics remain future work.

LNX is the explicit Linux escape hatch. It executes the named Linux program
directly with execv() and an explicit PATH search, passing the remaining arguments unchanged; it never
invokes a shell. Its inherited standard input, output, and error streams are
the ACE Amiga console streams:

    LNX /usr/bin/uname -a
    LNX printf hello

There is no automatic host-command fallback. An unrecognized command is an
AmigaDOS command failure. LNX is the deliberate mechanism for running a Linux
command.

The broker now performs a read-only discovery pass over host block devices
when it starts. Filesystem-bearing devices are registered in the initial ACE
DOS device list with their kernel name, filesystem type, filesystem UUID,
label when valid, and `/dev` path:

```sh
./build/ace-brokerctl doslist
```

The reverse translation can be inspected directly with `name`:

```sh
./build/ace-brokerctl name /home/erik/repo/ace
```

The kernel name, UUID, and valid filesystem label are treated as
case-insensitive DOS aliases for the same filesystem volume. Labels may
contain spaces when quoted, but may not contain the DOS separator or host path
separators. Unformatted partitions are retained as device entries so they can
be named and inspected even though they cannot be mounted by the DOS volume
resolver. Swap, encrypted containers, RAID members, and other explicitly
non-filesystem media are not registered as file volumes.

The first filesystem handler supports VFAT and ext2, ext3, and ext4. The
broker also enumerates the live Linux mount table. Block-backed mounts are
attached to their existing DOS volume entries, while filesystems without a
block device receive synthetic names (for example `RAM:` for tmpfs). On the
first use of an unmounted supported alias, the broker finds an existing mount
or asks Linux to mount it on demand. The host mountpoint is an
implementation detail: sda2:etc/hosts means etc/hosts below the root of the
filesystem on sda2, even if Linux happens to mount that filesystem at /. When
an AROS lock is converted back into a name, ACE chooses the longest matching
mountpoint and strips it, so nested mounts remain independent. ACE releases
mounts it created when the broker stops.

The first local Exec compatibility layer is now in `src/exec_compat.[ch]`.
It provides host-backed memory, registered tasks, Amiga signal-bit masks,
message ports, and case-insensitive library/device registries. Signals are
implemented with mutexes and condition variables rather than Unix signal
numbers. The focused test is run with:

```sh
make test-exec-compat
```

This layer is intentionally local to the runtime; the broker remains the
shared AmigaDOS/session authority. The AROS Shell will use this layer for its
Exec calls and the broker-backed DOS shim for DOS state.

`PathPart` is currently filesystem-independent.  It uses the original AROS
command source and native AmigaDOS-style `FilePart`, `PathPart`, and `AddPart`
implementations, so the paths it prints do not need to exist on Linux.

`Fault` and `Ask` are also built from their original AROS command sources.
`Fault` uses Amiga `/N/M` numeric-list argument handling, and `Ask` uses the
native console stream plus the `utility.library` compatibility surface.

The broker's first CLI-state draft provides session-local and broker-global
variables through `GetVar`, `SetVar`, and `DeleteVar`, plus a per-session
return-code/result2 record. `ace-brokerctl` can exercise these directly:

```sh
./build/ace-brokerctl setvar LOCAL value
./build/ace-brokerctl setgvar GLOBAL value
./build/ace-brokerctl getvar LOCAL
./build/ace-brokerctl setresult 10 205
./build/ace-brokerctl result
```

The original AROS `Set`, `Unset`, `Alias`, and `Unalias` commands are now
also built. Their local-variable lists are backed by the broker, and aliases
are kept separate from ordinary variables, as they are in AmigaDOS:

```sh
Set SCORE 5
Set
Alias HI Echo []
Alias
Get SCORE
Unalias HI
Unset SCORE
```

Alias expansion by the future shell command dispatcher is not implemented
yet; these commands currently provide the AmigaDOS alias state and its
inspection/update behavior.

The first CLI-lifecycle commands are also available. `FailAt` and `Prompt`
update broker-owned CLI state, while `Why` reads the previous command's
secondary result:

```sh
FailAt
FailAt 5
Prompt 'AMIGA> '
Get MISSING
Why
./build/ace-brokerctl cli
```

The diagnostic `cli` output is four lines: return code, `Result2`, fail
level, and prompt. A real interactive AmigaDOS-compatible command dispatcher
will consume this state in the next shell layer.

The first classic-console shell slice is now available. It uses a GTK drawing
surface as the Linux console, a full-duplex Unix stream for the
child CLI, and a cloned broker session. The surface starts with the classic
eight-pen palette and renders a first classic terminal subset: Amiga/ANSI CSI cursor movement,
colors, text attributes, erasing, tabs, scrolling, and local line editing.

The live window now feeds keyboard bytes into the real AROS console-handler
editing path from `rom/filesys/console_handler/support.c`. A small ACE bridge
opens the host-backed `console.device`, calls the original `process_input()` and
`history_walk()` code, and renders the original handler's `do_write()` output.
The GTK surface no longer owns the current line, cursor movement, deletion, or
history. It is only the host window and character-cell renderer.

`src/console_device.[ch]` remains the smaller standalone console-device test
seam. The real-handler bridge is in `src/aros_console_editor.[ch]`; it is the
staging point for the remaining DOS packet and task integration. AROS
Workbench integration, clipboard, and packet/task ABI remain deliberately outside
this profile.

```sh
source ./broker-start
export ACE_SESSION=main-shell
./build/ace-shell
```

Running `ace-shell` opens a separate console window and returns to
the launching terminal. The window runs the original AROS `Shell.c` through
the installed `ace-user-shell` binary.

The real shell starts the sibling `ace-broker` only when the configured
`ACE_BROKER_SOCKET` is unreachable, waits for it to accept connections, and
reuses an existing broker. The startup lock prevents concurrent shells from
starting duplicates. Set `ACE_BROKER_BINARY` only when the broker is not
beside `ace-user-shell`.

Inside that shell, command parsing, prompting, redirection, aliases, and
command errors are handled by the original AROS Shell code. At its command
loading boundary, ACE implements AROS `LoadSeg()`/`RunCommand()` with direct
`fork()`/`exec()` for ACE/AROS commands; it does not search the host `PATH`.
There is no automatic host-command fallback. `EndCLI` is compiled from the original
AROS command source and terminates the current real AROS shell, including a shell
running in an ACE window. `NewCLI` opens a separate window and starts
another real AROS shell with a cloned initial session:

```text
AMIGA> SET FOO parent
AMIGA> NEWCLI
```

`build/EndCLI` is compiled from the original AROS `EndCLI.c`; its CLI state change
is carried across the host process boundary by the ACE DOS bridge. `build/NewCLI`
is compiled from the original AROS `NewCLI.c` (which includes
`NewShell.c`).  Its unchanged `Open("CON:...")` and `SystemTagList()` calls
are currently backed by the host compatibility layer; the compatibility
layer launches the ACE console and clones the broker session.

The ACE Shell window has a GTK menu with typeface and eight-pen palette
dialogs. The console retains a bounded tail of its output stream, so applying
a new typeface or palette rebuilds the AROS console and repaints it
immediately. The drawing-area size allocation follows window resizes and
updates the real AROS window geometry. A resize smaller than one character
cell keeps the pixels already on screen rather than re-rendering anything,
and the console's background pen fills whatever the window has gained until
the console catches up. A resize that does change the character grid repaints
the retained stream, re-wrapping the text to the new width and putting the
last line back on the last row, in both directions. When the
installed appmenu GTK module and compositor support it, ACE exports the menu
model and actions over D-Bus and advertises that address to the compositor's
Wayland appmenu interface for the desktop/right-click menu; the local menu bar
is hidden while that advertisement is live and returns if it is lost.
Otherwise the same menu remains below the title bar.
AROS Workbench and clipboard extensions remain outside this profile.

## BOOPSI

The real AROS BOOPSI implementation is now built from unmodified AROS source:
`rom/intuition/{rootclass,makeclass,freeclass,addclass,removeclass,findclass,
newobjecta,disposeobject,setattrsa,getattr,nextobject}.c` and the amiga.lib
method dispatchers in `compiler/alib/{domethod,dosupermethod,coercemethod,
alib_util}.c`. That is roughly 1700 lines of AROS against 510 lines of ACE
seam, and unlike the console handler it needs no patch at all — the AROS
working tree is untouched by this build.

BOOPSI is Commodore's, introduced with AmigaOS 2.0 and documented in the ROM
Kernel Reference Manual: Libraries. AROS additionally uses it as the internal
architecture of `console.device`, so the console classes ACE is working toward
require it. It is also what `gadgetclass`, `imageclass`, `windowclasses` and
`screenclass` are built from, should ACE ever grow a real Intuition layer.

ACE supplies only what a real AROS build would generate or configure:

* `compat/aros-real/include/aros/libcall.h` turns an AROS library entry point
  into a plain C function. On AmigaOS the arguments arrive in named registers
  and the library base arrives with the call; on the host they are ordinary
  parameters and the base is a file-scope object.
* `compat/aros-real/include/aros/asmcall.h` does the same for hook and user
  functions, and supplies the `AROS_UFC*` forms that `CALLHOOKPKT()` in AROS's
  own `utility/hooks.h` is built from. That macro is where every BOOPSI method
  dispatch crosses the calling-convention boundary.
* `compat/aros-real/include/aros/atomic.h` maps the class reference counts onto
  the compiler's atomic builtins.
* `compat/aros-real/include/ace_boopsi_intern.h` is force-included ahead of the
  AROS sources and claims the include guard of
  `rom/intuition/intuition_intern.h`, which is the private header of the entire
  Intuition library. BOOPSI needs three fields out of its 1500 lines: the class
  list, that list's lock, and the rootclass.
* `src/aros_boopsi_runtime.c` is the Exec seam — memory, memory pools,
  recursive semaphores and list handling — plus the one-time rootclass
  bootstrap that `rom/intuition/intuition_init.c` performs on a real AROS
  build.

Whether the varargs method calls read their arguments straight off the stack or
marshal them through `GetMsgFromStack()` is left to `AROS_SLOWSTACKMETHODS` in
AROS's own `arch/<cpu>/include/aros/cpu.h`. AROS sets it for exactly those
architectures whose ABI does not pass varargs contiguously, so aarch64 and
x86_64 hosts get the marshalling path and i386 does not.

The focused test builds a two-level class hierarchy through the real
`MakeClass()` and checks AROS's dispatch, its instance-data layout, its
reference counting and its teardown refusals:

```sh
make test-boopsi
```

## graphics.library

`stdconclass.c`, `consoleclass.c`, and `support.c` from `rom/devs/console` --
the real AROS BOOPSI classes that touch pixels for `console.device`, plus the
real ANSI/CSI escape-sequence parser (`writeToConsole()`) -- are compiled
unmodified against `src/aros_graphics_runtime.c`. Unlike every other seam in
ACE, this one is authored rather than compiled from AROS source:
graphics.library is the real hardware boundary, the point where AmigaOS
drawing calls become pixels on a display, and the alternative to writing it
is a HIDD driver stack ACE does not want.

This is now what actually renders the live `ace-console` window.
`src/console_device_bridge.c` builds one real `ConUnit` per window and
`src/amiga_console.c`'s output path calls `writeToConsole()` on it directly --
the same call `console.c`'s real `beginio()`/`CMD_WRITE` would make, taken
without console.c's task/message-port machinery because ACE's architecture
never routes rendering through `DoIO()` (see HANDOFF.md for the full trace).
`console_device_bridge.c` exists as a separate translation unit because
AROS's real headers and GTK/glib's cannot coexist in one file (both define
`struct timeval`, and `console_gcc.h`'s `MAX`/`MIN` collide with glib's);
`amiga_console.c` only ever sees its opaque `struct ace_console_device`.

What AROS's console classes actually require of a `RastPort`/`BitMap`/
`TextFont` is narrow, confirmed by reading every call site rather than
guessing: ten drawing calls (`Move`, `Text`, `SetAPen`, `SetBPen`, `SetDrMd`,
`SetABPenDrMd`, `RectFill`, `ScrollRaster`, `SetSoftStyle`, plus the
`AllocRaster`/`FreeRaster`/`InitTmpRas` scratch-buffer trio for the
character-cell cursor), and from a font, exactly three scalars --
`tf_XSize`, `tf_YSize`, `tf_Baseline` -- to lay out a fixed character-cell
grid ("For now one should use only non-proportional fonts", in
`consoleclass.c`'s own words). Nothing in this codepath ever reads a font's
glyph bitmap data.

That gap is where ACE improves on real Amiga hardware rather than emulating
it: glyphs are rendered from a host TrueType font via cairo/fontconfig
instead of a bitmap font ACE would have to draw and ship, and
`SetSoftStyle()`'s bold/italic requests select the font's own real bold/
italic faces rather than being synthesized by smearing or shearing a single
face the way real Amiga hardware did. `ace_gfx_font_family_complete()`
enforces that a chosen family actually has all four faces (regular, bold,
italic, bold-italic) before it can be used. Underline is drawn as a rule
below the baseline either way, since it is a soft-style flag on real
hardware too, never a font glyph.

The focused test builds a real `ConUnit` through `NewObjectA()` on the real
class chain and drives it two ways: AROS's own `Console_DoCommand()` macro
directly, and `writeToConsole()` with a raw CSI byte sequence -- the latter
is what actually exercises the escape-sequence *parser*, since
`Console_DoCommand()` alone dispatches an already-parsed command. Both read
the resulting pixels back to confirm:

```sh
make test-graphics test-console-device-bridge
```

The bridge test also writes retained output, changes all eight palette slots,
resizes the backing surface both smaller and larger, and changes the typeface,
checking that the repainted surface keeps both its content and its new
background. It scrolls far enough to exhaust the surface's scroll headroom
several times over and then scrolls that content back off the top, which
catches a scroll that fails to move what is on screen or fails to clear what
it uncovers, and it checks that the console reports the region it drew into.

This needs `cairo` and `fontconfig` development files, and at least one
complete monospace family on the host; `Liberation Mono`, `DejaVu Sans Mono`,
and the `monospace` fontconfig alias are tried in that order, both for the
test and for the live `ace-console` window. The ACE Shell menu offers a
monospace typeface chooser and eight color slots, validated through the same
font-loading path as startup. The selected font family, font size, and eight
palette entries are saved immediately to `$HOME/.config/ace.conf` and loaded
on the next start. See HANDOFF.md for what is still not real (keyboard input,
cursor blink).

Global variables currently last only for the lifetime of the broker. The
`SAVE`/`ENVARC:` persistence behavior is still reserved for a later draft.
Every command built through the native AROS shell-command wrapper now
publishes its return code and `IoErr()` as the session's result record.
