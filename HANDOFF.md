# ACE handoff

## Where it is installed

Two hosts are set up, both building and passing the full test suite:

* a Raspberry Pi (aarch64, Debian, labwc) at `~/repo/ace`, run from `build/`;
* `blackberry` (x86_64, Debian 13) at `~/repo/ace`.

`make install` installs into `~/.local/bin` and needs no privileges. A
system-wide install is a `PREFIX` override --
`sudo make PREFIX=/usr/local AROS_ROOT="$HOME/aros" install` -- with
`AROS_ROOT` passed explicitly because `sudo` resets `$HOME` and the Makefile
defaults `AROS_ROOT` to `$HOME/aros`; without it the build looks for AROS
under `/root` and the install fails before it copies anything.

**One install per machine.** ACE programs find their companions beside their
own executable, so an install is a set that stays together, and two sets on
one `PATH` drift apart in the quietest possible way: the older set keeps
working, just old, and the newer one is simply never reached. This happened
here -- a `~/.local/bin` install shadowed `/usr/local/bin`, so a fresh
`sudo make install` left the panel icon starting the previous build, with no
symptom beyond a missing command that had only been installed to the root
nothing was reading. Hence the default prefix, the absolute path in the
launcher, and the warning `make install` prints when `PATH` will not select
what it just installed.

## Current state

ACE is committed on `main` and pushed to `https://github.com/harbin-ctrl/ace`.
The current tree runs the real AROS `Shell.c` and AROS shell support code on a
Linux host. The host seam provides DOS state, broker-backed current directory
and variables, direct command loading, the console-device bridge, and the
Wayland/GTK window.

The real AROS `EndCLI.c` is included. Its state change is carried across the
child command process and stops the parent shell. Ordinary host commands are
not searched through `PATH`; `LNX` is the explicit direct Linux executable
escape hatch.

The real AROS BOOPSI implementation is built from unmodified AROS source in
`rom/intuition` and `compiler/alib`, on the ACE seam in
`src/aros_boopsi_runtime.c`. This is the first step of moving the display seam
down from `console.device` to Intuition: AROS implements `console.device` as
BOOPSI classes, so nothing above that boundary can run without it.

The real AROS console.device classes that touch pixels -- `stdconclass.c` and
`consoleclass.c` from `rom/devs/console` -- are compiled unmodified against a
real `graphics.library`, `src/aros_graphics_runtime.c`. Unlike every other
seam in ACE, this one is authored rather than compiled from AROS source:
graphics.library is the actual hardware boundary, the point past which "real
AROS code" would mean a HIDD driver stack ACE does not want. It renders
through cairo/fontconfig onto an ACE-owned `RastPort`/`BitMap`, with glyphs
from a host TrueType font rather than a bitmap font ACE would have to ship --
see the file header for what AROS's console classes do and do not require of
a font.

### How graphics.library draws, and why it is shaped that way

The seam started out routing every drawing call through cairo, one character
cell at a time. Measured on this Raspberry Pi with a 900x576 console, that
cost 10.8 ms for each line of output once the console had filled up and
started scrolling -- five and a half seconds for five hundred lines -- and a
typeface or palette change took six seconds. Three things were responsible,
and the file is now organised around avoiding them.

**Scrolling does not copy the console.** `ScrollRaster()` used to snapshot the
whole scroll box into a second cairo surface, fill the box, and blit the
snapshot back: four passes over the console plus a two-megabyte allocation,
for every single line. It was 86% of the profile. A console almost always
scrolls the same shape -- everything from the top-left corner, up by one text
line -- so the surface is allocated taller than the console and that case
moves the *viewing origin* down inside it instead, leaving the pixels alone
and filling only the one line the scroll uncovers. The origin is folded back
to the top of the allocation when the headroom runs out, which is the only
time a scroll copies anything. `ace_gfx_rastport_surface()`'s callers
therefore have to read the console's rows from
`ace_gfx_rastport_origin_y()` rather than from row zero, which is the one
externally visible consequence.

The scroll box does not always reach the surface's right and bottom edges --
a window whose pixel size is not a whole number of cells leaves a margin --
and moving the origin moves that margin too. `scroll_by_origin()` checks that
the margin already holds the colour the scroll would fill with rather than
assuming it, and hands back to the copying path if it does not.

**Rectangles are filled, moved and inverted directly.** `RectFill()`,
`ScrollRaster()` and the COMPLEMENT cursor are rectangle copies and solid
fills; going through cairo meant a pixman composite for work a `memmove`
already does. They write the image surface's pixels between a
`cairo_surface_flush()` / `cairo_surface_mark_dirty_rectangle()` pair, which
is cairo's documented contract for exactly this and costs nothing on an image
surface. Cairo still draws every glyph, which is the part that needs it.

**Glyphs are looked up once.** `Text()` used to call `cairo_set_font_face()`,
`cairo_set_font_size()` and `cairo_show_text()` per character, each of which
re-resolved the scaled font and redid a FreeType character-map lookup. There
is now one `cairo_scaled_font_t` per style built at font load, a cached glyph
index per byte, and one `cairo_show_glyphs()` per run of text with every
glyph positioned explicitly on the console's own integer cell pitch. Two real
bugs fell out of that rewrite: the old code rendered at `tf_YSize` (the line
height) while `tf_XSize` had been measured at the requested pixel size, so
glyphs were wider than the cells they were laid out on; and `INVERSVID`,
which `setabpen()` in `stdconclass.c` sets for reverse video, was ignored
rather than swapping the two pens.

Together these take a line of output from 10.8 ms to 0.34 ms, a resize step
from 7.2 ms to effectively nothing, and a typeface or palette change from six
seconds to about 0.3 -- and, because the retained stream is now bounded, that
last figure no longer grows with how long the shell has been running. Driving
the real window end to end, twenty thousand lines render in six seconds where
they would previously have taken more than three minutes.

The cost is memory: the surface carries scroll headroom (a 6 MB budget,
clamped to between 256 and 2048 rows) and 128 columns of width slack, so it
is roughly twice the console's own size. A resize that still fits inside that
allocation does not reallocate at all, which is what makes a live drag cheap.

`ace_gfx_take_damage()` reports the bounding box of everything drawn since
the last call, so `amiga_console.c` repaints only what changed instead of
handing the compositor a whole window per frame.

The real ANSI/CSI parser, `rom/devs/console/support.c`'s `writeToConsole()`,
is compiled the same way and is now what the live `ace-console` window
actually calls: `src/console_device_bridge.c` builds one real `ConUnit` per
window (the class pair, a font, an `ace_gfx_create_rastport()`-backed
`RastPort`, a real `struct Window`) and `src/amiga_console.c`'s socket output
path calls `writeToConsole()` on it directly, the same call `console.c`'s real
`beginio()`/`CMD_WRITE` would make. `draw_console()` blits the RastPort's
cairo surface straight onto the window. `src/console_terminal.c` -- ACE's own
hand-written ANSI parser and character-cell renderer, the thing this whole
seam exists to retire -- is deleted, along with its test.

`console_device_bridge.c` exists because AROS's headers and GTK/glib's
cannot be included in the same translation unit (both define `struct
timeval`; `console_gcc.h`'s `MAX`/`MIN` collide with glib's) -- the same
reason `aros_graphics_runtime.h` forward-declares `RastPort`/`BitMap`/
`TextFont` rather than including their real headers. `amiga_console.c` only
sees the bridge's opaque `struct ace_console_device`; every real AROS type is
confined to `console_device_bridge.c`.

`make test-graphics` constructs a real `ConUnit` through `NewObjectA()` and
drives it two ways: AROS's own `Console_DoCommand()` macro directly (proving
class construction and plain-text dispatch), and `writeToConsole()` with a
raw CSI byte sequence (proving the escape-sequence *parser* itself -- the
part `Console_DoCommand()` alone never exercises, since that macro dispatches
an already-parsed command). The CSI test drives `CSI n C` (cursor forward),
confirmed present in `support.c`'s real command table. ANSI SGR color and
text rendition are handled by `consoleclass.c` in this AROS checkout and are
covered by the live rendering path.

`Delete` and `Protect` are the first commands that change the filesystem
beyond creating a directory, and every command in the tree now parses its
arguments with AROS's own `ReadArgs()`; both are described in their own
sections below.

The first read-only filesystem-facing command is now real AROS `Dir.c`, built
with the original DOS `patternmatching`, `MatchFirst`/`MatchNext`/`MatchEnd`,
and `ExAll` implementations. `src/native_dos.c` supplies the host-backed
`Lock`/`Examine`/`ExNext`/`DupLock`/`CurrentDir` seam, Unix metadata conversion,
and the RawDoFmt-compatible `VPrintf` path that `Dir` uses for its formatted
columns. The command has been exercised against regular and nested host
directories, `#?` patterns, `ALL`, `DIRS`, `FILES`, and missing paths. The
compatibility layer intentionally leaves the DOS root's optional `*` wildcard
flag disabled, matching the configured AmigaDOS pattern behavior.

## Both argument parsers are now AROS's

ACE used to have two. `compat/include/aros/shcommands.h` was a 170-line
restatement of AROS's macro header whose `AROS_SHn` expansion called
`native_parse_args()` -- 357 lines of hand-rolled template parsing in
`src/native_args.c` -- while five commands that call `ReadArgs()` themselves
went to the real thing. Nineteen commands against five, decided by nothing but
how each command's author happened to declare its arguments.

Both files are gone. AROS's own `compiler/include/aros/shcommands.h` is
compiled instead, and what it needs from the host is small: `AROS_PROCH` in
`compat/include/aros/asmcall.h` (built from the `AROS_UFH3` already there), a
three-macro `compat/include/aros/symbolsets.h` -- ACE has no link-time library
sets to walk, so opening them vacuously succeeds -- and
`compat/include/ace_shcommand_host.h`, which supplies the `main()` that stands
where `CreateProc()` stands on a real AROS.

Three things had to be true underneath, and only one of them was.

**A command's argument line reaches `ReadArgs()` through the input stream.**
That is AmigaDOS's own mechanism -- the shell leaves the line in the stream and
`ReadArgs()` reads it -- and ACE already had it, as `ACE_COMMAND_ARGUMENTS`
carried across `execv()` by `src/native_command.c` and injected by
`native_load_input_prefix()`. What it did not have was the case with no shell:
a command run straight from a Linux shell never saw its own `argv`, so
`./build/MakeDir WORK:x ALL` -- an invocation README documents -- answered
`required argument missing`. `src/native_shcommand.c` puts `argv` back together
into one AmigaDOS line, quoted the way `readitem.c` will take it apart, and
sets it only if the shell has not already set one, so the shell's own parse
stays authoritative. An invocation with no arguments at all sets an *empty*
line rather than nothing, which is the difference between `ReadArgs()`
reporting a missing `/A` argument and `ReadArgs()` reading on into the next
command in the shell's own input.

**`IoErr()` has to be the process's `pr_Result2`.** ACE kept it in a variable
beside the process. `rom/dos/readargs.c` ends with `me->pr_Result2 = error;`
and never calls `SetIoErr()`, so with two stores every `ReadArgs()` failure
reported no reason at all: the command's `PrintFault(IoErr(), name)` printed
nothing and exited 20. `IoErr()` is now that field. This had been true since
`readargs.c` was first compiled in and was invisible while only five commands
used it; it is the same hazard as the `ErrorReport()` polarity bug in TODO.md,
and worth remembering as the shape rather than the instance.

**A command may own `SysBase` and `DOSBase`.** `Dir.c` sets
`SH_GLOBAL_SYSBASE` and `SH_GLOBAL_DOSBASE` before including the header, which
makes the macro expansion define both at file scope -- on a real AROS build
they are the whole program's only copy. ACE's runtime is linked in beside the
command, so the two collided at link time. ACE's definitions are now weak and
yield to the command's, and `ace_shcommand_start()` passes the entry point a
base from `native_exec_base_pointer()` rather than reading back a symbol the
command has not filled in yet.

Verified by exercising each command with a real template case -- `/S`, `/K`,
`/M`, `/N`, `/A` missing, and the `?` help convention including a repeated `?`
-- and by checking that `nm build/<command>` no longer resolves
`native_parse_args` anywhere.

## Delete and Protect, and the volume aliases they turned up

`Delete` and `Protect` are the first destructive commands. Both were already
on the real parser by construction, and both needed one new seam call,
`SetProtection()` -- the `chmod()` inverse of the `native_protection_from_stat()`
mapping `Examine()` already used, so the bits `Protect` writes are the bits
`Delete` reads back to decide whether it may remove something. `DeleteFile()`
also had to learn that one AmigaDOS call removes either kind of object where
Unix splits `unlink()` from `rmdir()`.

`Protect` additionally wanted `IsDosEntryA()`, so AROS's own
`compiler/arossupport/isdosentrya.c` is compiled too, on an
`AttemptLockDosList()` that is ACE's `LockDosList()` -- nothing else can hold
a list this process builds for itself.

Adding those tests turned up a bug that had nothing to do with them: on this
Raspberry Pi the whole filesystem suite was already failing at HEAD, because
`src/assign_compat.c` registered only a block device's *kernel* name in the
DOS list AROS's `GetDeviceProc()` searches. The broker treats the kernel name,
the filesystem UUID and the filesystem label as interchangeable, and its
host-to-AmigaDOS translation hands back the label when there is one -- so
`rootfs:home/...` failed to lock with ERROR_DEVICE_NOT_MOUNTED while
`sda2:home/...`, the same filesystem, worked. All three spellings are now
registered. This is exactly the Amiga distinction it looks like: the kernel
name is the device, the label is the volume.

One test had `sda2:` written into it, which is this host's root device; it now
takes the expected spelling from the broker.

## Filenote, and the two things it found

`Filenote` needed one new call, `SetComment()`. An AmigaDOS file comment is
the one piece of Amiga file metadata with no Unix field to map onto -- unlike
protection bits, which are permissions -- so it goes in an extended attribute,
`user.comment`. That puts it on the inode, which is why it survives a rename
without ACE doing anything. The name is deliberately generic rather than
ACE's own; the cost is that a foreign writer is not bound by AmigaDOS's
79-character limit, so `native_fill_fib_comment()` truncates what it reads
instead of trusting it. Writing longer than that is refused rather than
truncated, since a `FileInfoBlock` could not carry it back. A filesystem with
no extended attributes reports ERROR_ACTION_NOT_KNOWN -- AmigaDOS's answer for
a handler that does not implement an action, and not hypothetical, since the
broker registers this Pi's VFAT bootfs as a volume.

`Examine()` and `ExNext()` fill `fib_Comment`, at one `getxattr()` per
directory entry. Nothing ported yet displays a comment -- `List` is the
AmigaDOS command that does -- so `tests/dos_comment_test.c` is the reader that
keeps the read half honest until it lands.

Two things turned up underneath.

**`LockDosList()` returned NULL.** ACE's own `NextDosEntry()` read that as
"start at the beginning", and every AROS caller that assigns the result and
then walks it -- `Assign.c`, `Delete.c` -- survived. AROS's own
`compiler/arossupport/isdosentrya.c` does not: it reads NULL as "the list
could not be locked" and reports that nothing matched. So `Protect` had been
silently accepting a whole volume since it was ported an hour earlier, and
would have chmod'd the volume root. There is now a list header to hand back,
as real AmigaDOS has.

**ACE has two BSTR conventions and the compat macros only matched one.** The
CLI's `cli_Prompt`, `cli_SetName` and `cli_CommandFile` are plain C strings
that AROS's real `Shell.c` and `cliPrompt.c` read through `AROS_BSTR_ADDR`,
so `<dos/dosextens.h>`'s definitions have to leave them alone. A DosList
entry's `dol_Name` is a real length-prefixed BSTR, because `Assign.c` reads
that byte back through a macro of its own that ACE cannot redefine. The
compat macros are now the guarded default and `src/assign_compat.h` overrides
them for the DosList world, which is the only place that disagrees.

Both were invisible until an AROS source read a DosList the way AROS writes
them, which is the same shape as the `IoErr()` storage bug above: an ACE stub
that is merely *wrong* stays harmless until upstream code that depends on it
gets compiled in.

## Child-side console input and raw mode

The GUI now sends each key sequence directly to the child socket. The AROS
console editor is no longer owned by `ace-console`: `native_dos.c` enables it
only for the GUI-launched session, feeds complete CSI sequences to AROS's
`process_input()`, and writes its echo through the child process's normal
`Output()` stream. This keeps editing, history, and echo in the process that
owns `Input()`, and means a child can take over the stream without a second
IPC path.

`SetMode(Input(), 1)` disables cooked editing and makes `Read()` expose raw
bytes. `WaitForChar()` is backed by `select()` with AmigaDOS's microsecond
timeout, which is the input contract Vim's Amiga backend uses. The focused
`test-native-input` target checks cooked history, complete CSI handling, raw
reads, and readiness. The editor/runtime objects are part of the DOS runtime;
`native-list-compat.c` supplies the external Exec list symbols expected by
the imported handler while ACE's own list callers keep their inline helpers.

The GUI sets `ACE_CONSOLE_INTERACTIVE=1`; ordinary pipes and scripts do not,
so their output remains byte-for-byte stream behavior rather than acquiring
interactive echo sequences.

The first Vim milestone is also complete: a fresh Vim checkout's
`src/os_amiga.c` compiles against ACE's compatibility headers with `ccache`
under `-DAMIGA -D__AROS__`. The compile uses a temporary `devices/conunit.h`
forward declaration because Vim includes that header unconditionally while its
AROS shell-size path does not access `struct ConUnit`; this should become part
of the eventual Vim build wrapper rather than the core ACE runtime. The
current Vim source has one unrelated GCC error in `get_fib()` (a bare return
from a pointer-returning function); the validation corrected that line only in
the temporary checkout. No Vim source or executable is in the ACE tree yet.

The follow-up link probe compiled Vim's normal-feature core and linked it with
ACE's DOS path, broker, process, and pattern-matching objects. The remaining
undefined symbols are exactly `Delay`, `SetWindowTitles`,
`mch_get_cmd_output_direct`, `vim_fsync`, and `xdl_diff`. The first two are
small ACE/Amiga platform seams; the next two are Vim platform helpers; and
`xdl_diff` is resolved by adding Vim's six xdiff objects, which are otherwise
independent of ACE. Contrary to the earlier guess, this current Vim source did
not require `Info`, `DateStamp`, or `Rename` at the link boundary.

One trap worth knowing, since it wasted time twice in a row: the Makefile is
not a prerequisite of anything it builds, so changing a rule's recipe does not
rebuild the object. Both times the symptom was a fix that appeared not to
work. Making it a prerequisite is not a one-line change -- roughly twenty
recipes pass `$^` straight to the compiler or linker and would need the
Makefile filtered back out -- so it is still a trap. Delete the object by hand
after editing its rule.

## Build on another host

The ACE Makefile uses `AROS_ROOT`, defaulting to `$HOME/aros`, and selects the
AROS CPU include directory from the host architecture:

```sh
git clone https://github.com/aros-development-team/AROS.git "$HOME/aros"
cd "$HOME/aros"
git checkout 53af6e419b
cd "$HOME/repo/ace"
patch -p1 -d "$HOME/aros" < patches/aros-console-seam.patch
make -j2 all
make test-aros-console-editor test-aros-exec-runtime \
     test-console-device test-exec-compat test-boopsi test-graphics
```

`test-graphics` additionally needs `cairo` and `fontconfig` development files,
and at least one complete monospace family on the host (regular, bold, italic
and bold-italic faces of the same family) -- `Liberation Mono`, `DejaVu Sans
Mono`, or the `monospace` fontconfig alias are tried in that order.

For an unusual host, override both variables explicitly, for example
`make AROS_ROOT=/opt/aros AROS_CPU_ARCH=aarch64-all -j2 all`.

Built and tested on two hosts: a Raspberry Pi (aarch64, Debian, labwc) and an
x86_64 Debian 13 machine. The second one turned up a portability bug worth
knowing about, because it is the kind this project will keep meeting. Real
AROS declares `STRPTR` as `UBYTE *`; ACE's `compat/include/exec/types.h`
declares it as plain `char *`, which is **unsigned on ARM and signed on
x86**. A parsed AmigaDOS pattern stores its tokens as `P_ANY` and friends,
`0x80..0x88` in `dos/dosasl.h`, so on a signed-char host every one of
`patternmatching.c`'s `case P_ANY:` labels becomes unreachable and `#?` quietly
stops matching anything. gcc rejects it outright as
`-Werror=switch-outside-range`, which is how it surfaced. The AROS DOS
pattern sources and `Dir` are compiled with `-funsigned-char`
(`AROS_DOSPAT_CFLAGS`) to give plain `char` the signedness AROS wrote them
against. Anywhere else ACE hands a real AROS source a `char` that AROS
declared `UBYTE` is a candidate for the same bug, and it will not always be
loud.

The patch is required for the AROS console handler to compile without
GadTools, Workbench AppWindow, and completion support. It leaves the original
console editing and history code in place. Apply it once to a clean checkout;
`patch` will report that it is already applied if repeated. It touches only
`rom/filesys/console_handler`; the BOOPSI sources need no patch.

GTK 3 development files, `blkid` development files, a C compiler, `make`,
`git`, and a Wayland session are required for the complete build/window.

## Launch

From the ACE checkout:

```sh
./build/ace-broker
ACE_SESSION=main-shell ./build/ace-shell
```

The real shell starts a sibling broker automatically when the configured
socket is unavailable. `NewCLI` opens another ACE console. `EndCLI` ends the
current AROS shell.

## Known boundary

The AROS source tree remains an external source dependency. ACE currently
compiles selected AROS commands, the AROS shell, the AROS console handler,
AROS BOOPSI, and the whole pixel-facing half of AROS's console.device
(`stdconclass.c`, `consoleclass.c`, `support.c`) against the compatibility
headers in `compat/`.

**What "console.c itself is not compiled" actually means.** `console.c` is
the device's task/`BeginIO`/message-port machinery -- the generic AmigaOS
device-I/O protocol for a program that calls `OpenDevice()`/`DoIO()` and
waits. ACE's shell-in-a-window architecture never uses that protocol for
rendering: the shell child process's stdout is a plain Unix pipe read
directly by `amiga_console.c`, and the console handler's own self-echo
(`rom/filesys/console_handler/support.c`, already real, Phase 1) writes
through a synthetic `console.device` in `src/aros_exec_runtime.c` that exists
only to give `process_input()` a `struct ConUnit`-shaped context to edit
against -- a second, separate ConUnit from the one this phase added for
rendering. Both byte streams converge on one function call
(`ace_console_device_write()`), which is where the real `writeToConsole()`
now sits. Confirmed by tracing the actual data flow, not assumed; see the
git history for the trace. `console.c`'s task/BeginIO layer would only start
mattering if ACE needed a general `OpenDevice()`/`DoIO()`-driven program to
talk to a console window, which nothing in the current architecture does.

**Keyboard mapping is deliberately not AROS's.** `key_press()` in
`src/amiga_console.c` translates GDK keyvals to CSI byte sequences directly,
rather than going through AROS's real `rom/devs/console/cdinputhandler.c`
(Intuition-level input-event handling) and `rawkeyconvert.c` (a thin wrapper
over keymap.library's `MapRawKey()`, which needs a real default `KeyMap`
table -- no self-contained one turned up anywhere in `rom/keymap`; real
AmigaOS loads it from a `DEVS:Keymaps/` file ACE has no filesystem for).
This was assessed and then deliberately ruled out, not merely deferred:
keyboard layout is a physical-device concern, the same category as the
window surface itself, and GDK/Wayland already does it correctly for
whatever keyboard and layout the host has -- reimplementing an Amiga keymap
table on top would be strictly worse, not more authentic. `key_press()`'s
translation is the permanent design here, on the same footing as
`graphics.library` being authored rather than compiled: this is where the
seam sits, not a stopgap for AROS code to eventually take over. Editing
itself (cursor movement, backspace, history) runs in the child on real AROS
code, via `process_input()`; only the *encoding* of GDK key events into the
bytes it expects is ACE's, and stays that way. Raw-mode programs receive those
same bytes after `SetMode()` disables the child-side editor.

Font and palette selection is host-side: the ACE Shell GTK menu offers a
monospace typeface chooser and eight color slots, validated through the same
`ace_gfx_load_font()` path as startup. The bridge retains the raw console
stream, rebuilds the real console unit and RastPort for a typeface or palette
change, and replays the stream so the visible contents are repainted
immediately with the new cell metrics and eight pens.

The retained stream is bounded, at several screenfuls sized from the console's
own grid and dropped from the front at a line boundary. It exists only to
repaint after a typeface or palette change, so it needs to hold what is still
on screen, not the whole session; unbounded, it made those two operations cost
time proportional to how long the shell had been running, since the repaint had
to re-render and re-scroll past every line ever written.

A window resize usually does not go through that rebuild at all.
`ace_gfx_resize_rastport()` changes the surface's dimensions with the pixels
intact and the bridge then invokes AROS's real `Console_NewWindowSize()`
geometry path, so a live drag costs a geometry update rather than a re-render
of the stream -- which is what it used to cost, on every intermediate size GTK
delivered. Keeping the pixels is sound because a window resize moves no
character cell: the font is unchanged, so every cell keeps its pixel position
and only the number of rows and columns changes.

**Except when the character grid itself changes size.** The text on screen
was laid out against the old column count and the old bottom row, so it has
to be re-wrapped against the new ones. `ace_console_device_resize()` reads
`cu_XMax`/`cu_YMax` around the `Console_NewWindowSize()` call and repaints
the retained stream when either moved, clearing and homing first with a real
form feed because the replay has to start at the top left. Most steps of a
drag are smaller than one cell and change neither, so they stay free.

Shrinking would need the repaint in any case, for a second reason that is
real AROS code rather than ACE's. `console_newwindowsize()` clamps the cursor
into the new grid, and `stdcon_newwindowsize()` responds to a cursor that
moved by clearing the whole console and redrawing the cursor -- a
character-cell renderer with no retained character map has no way to tidy up
the cursor it left at the old position without wiping what it cannot redraw.
That is why shrinking made the text vanish once resize stopped replaying: the
old replay-everything path had been repainting what AROS had just erased,
without anyone noticing AROS was erasing it. Clamping only ever goes
downwards, so enlarging never hits that clear -- which is why the trigger
here is the grid and not the cursor. Testing the cursor made growing keep the
old narrow layout, with the text squashed into the top left of the wider
window.

A repaint replays a few screenfuls of the tail of the retained stream rather
than all of it (`replay_start()`). Anything older only scrolls off the top
again, and re-rendering it was most of what a repaint cost.

The bridge test covers both directions, and both checks are shaped around
things that hid the bug the first time. After a shrink it looks for ink in
the console's *upper half*: a whole-frame ink check passes on a console that
has been completely wiped, because the cursor block is itself non-background,
which is exactly why the original resize assertion did not catch the clear.
After a grow it writes lines long enough to wrap in a narrow console but not
a wide one, then looks for ink in the columns the console has just gained --
text squashed into the top left is precisely the absence of ink out there.
GTK
`size-allocate` coalesces those to about one frame, and `draw_console()` fills
whatever the window has gained with the console's background pen until the
console catches up. The drawing area is no longer fixed at the initial
`CONSOLE_WIDTH`/`CONSOLE_HEIGHT`. The selected font family, font size, and all
eight palette entries are written immediately to `$HOME/.config/ace.conf` and
loaded before the first RastPort is created. Also not implemented:
cursor blink (real console.device does not appear to
drive this from a timer either -- `stdcon_drawcursor()` only fires on
explicit `RenderCursor`/`UnRenderCursor` calls tied to command processing and
window-active state), and DSR cursor-position-report replies
(`con_inject()` in `console_device_bridge.c` is a documented no-op: a
program that sends `ESC[6n` gets no reply, since answering it needs the
task/message-port layer described above).

### Two ways ACE could take unmodified AROS code somewhere it cannot go

Both of these are the same shape: AROS source that is correct on the system
it was written for, reached by ACE with an input a real Amiga could not have
produced. Neither is fixable by changing AROS, and neither should be.

**A console smaller than one character cell hangs.** `consoleclass.c`
computes `cu_XMax`/`cu_YMax` as `(pixels / cell) - 1`, so a console narrower
or shorter than a single cell gets `-1` -- a grid with no columns or no rows
-- and the class chain then spins forever placing a character into it.
Confirmed by attaching to a hung process: the stack sits in AROS's own
`dispatch_consoleclass()`. `clamp_to_cell()` in `console_device_bridge.c`
guards every path that sizes a console. `replace_render_state()` had always
clamped; the point-resize path added for live resizing had not, which is
where this came in.

**A shell whose `Cli()` returns NULL crashes.** Real AmigaDOS gives every
shell process a CLI, so `Shell.c` and `cliPrompt.c` dereference `Cli()`
without checking -- correct for that system. On ACE the CLI is broker-backed,
and the broker is a separate process with a finite session table, so it can
fail where a real Amiga's could not; ACE's `Cli()` returned NULL and unmodified
`Shell.c` segfaulted at line 716, with five more unchecked calls inside its
command loop. `Cli()` now never returns NULL: it keeps the last state it read
successfully, or falls back to the same defaults the broker gives a new
session, and reports once on stderr. A full session table is now a
diagnosable degradation ("broker CLI state unavailable ... continuing with
defaults", then real errors from the commands) instead of a window that
vanishes.

### One broker per user

The broker used to default to a single machine-wide `/tmp/ace-broker.sock`,
which was wrong in both directions at once. Two users on one machine collided
on the same path, and since the socket is created 0600 the second one simply
could not use it. Meanwhile anything that pointed `ACE_BROKER_SOCKET` at a
private path -- every isolated test run -- got a whole additional broker
process, and they piled up: a day of testing left drifts of them behind.

The default is now `$XDG_RUNTIME_DIR/ace-broker.sock`, the per-user directory
made for this, which the system clears when the user's last session ends so a
socket cannot outlive its login. Where there is no runtime directory the uid
goes in the filename instead. `amiga_broker_socket_path()` in
`broker_protocol.h` is the single definition, shared by client and server so
they cannot disagree. `ACE_BROKER_SOCKET` still overrides, for a deliberately
isolated broker; that just is not what happens by accident any more.

**A second broker no longer displaces the first.** `main()` used to
`unlink()` the socket path unconditionally before binding, so starting
another broker silently stole the path from the live one and stranded it --
still running, still holding every session, on a socket nothing could reach.
A broker now holds an exclusive `flock` on `<socket>.lock` for its whole life
and exits if it is already held, so "one per socket" is enforced by the
kernel rather than by convention; the socket is only unlinked once the lock
is ours, which means any socket still on disk belongs to a broker that is
gone. The broker writes its pid into that lock file, which is how
`broker-stop` finds it without guessing from command lines, and `broker-start`
uses `flock -n` to detect a running broker and leave it alone rather than
restarting it and discarding its sessions.

**Sessions are reclaimed rather than exhausted.** Nothing tells the broker
that the shell behind a session has exited, so slots cannot be freed when
their owner goes away, and they used to simply run out: past 64 distinct
sessions every new one got `ENOSPC` and, before the `Cli()` fix above, every
ACE window opened from then on died on the spot. A broker meant to live as
long as the login has to cope with that, so a full table now gives up its
coldest slot instead of refusing. Every request stamps its session with an
ordinal and a live session is stamped constantly -- the shell reads its CLI
state to draw each prompt -- so the slot longest without a request is the best
available guess at one whose shell is gone. It is a guess: losing it costs a
session its current directory, assigns and variables, which is recoverable and
logged. Real ownership tracking would be better and still needs designing.

### The greyed-out menu items were labwc's, not ACE's

Worth recording, because it cost a lot of time and the symptom points the
wrong way. On a host whose labwc predates the fix below, ACE's exported menu
appears with **Typeface and Palette greyed and Quit active**. That looks
exactly like the provider race fixed in "Fix ACE appmenu provider lifecycle",
and it is tempting to go back into ACE's D-Bus export. Don't; check the
compositor's version first.

ACE's export is verifiable from the shell, and was correct throughout:

```sh
busctl --user list | grep ace-console          # find the connection, e.g. :1.858
P=/org/appmenu/gtk/window/menus/menubar/ace_shell
gdbus call --session --dest :1.858 --object-path $P \
    --method org.gtk.Menus.Start "[uint32 0, uint32 1]"
gdbus call --session --dest :1.858 --object-path $P \
    --method org.gtk.Actions.DescribeAll
```

`DescribeAll` returns `{'quit': (true…), 'typeface': (true…), 'palette':
(true…)}` -- all three present, all three enabled -- and the model carries the
right labels and `app.` action names. Nothing on ACE's side is disabled.

The cause is in labwc's appmenu consumer, fixed by `4e782e8` ("Fix appmenu
lifecycle and action readiness") and `90348bc` ("Wait for every remote appmenu
action") in the onscreen-windows tree. Its own comment names the mechanism,
including why Quit is the item that keeps working:

> GDBusActionGroup handles DescribeAll asynchronously and emits one
> action-added signal per dictionary entry. Merely checking that the action
> list is non-empty races with that sequence: if "quit" is the first entry, a
> snapshot taken from its signal marks every later action disabled.

`quit` is the first entry in ACE's dictionary, so a compositor without those
commits snapshots on it and disables `typeface` and `palette`. The fix belongs
in labwc; the only thing ACE could do about it is rename its actions so that
the alphabetically-or-insertion-first one is not the one that matters, which
would be superstition rather than engineering.

### The window's own two seams

`read_console()` drains the shell's socket in one pass per main-loop turn and
repaints once, rather than allocating a buffer and posting a `g_idle_add()`
callback for each 4KB the socket happened to deliver. The drain is capped at
`OUTPUT_DRAIN_MAX` so a program that prints without pause cannot hold the main
loop -- and with it the keyboard and the window -- for as long as it keeps
printing.

`ace_appmenu_wayland.c` puts ACE's Wayland objects on their own
`wl_event_queue`. The default queue belongs to GDK, and the previous code
called `wl_display_roundtrip()` on it from inside a GTK timeout that fires
four times a second: that both blocks and dispatches GDK's own event handlers
re-entrantly from inside a GTK callback, which is a plausible source of the
compositor-level instability this window had. A private queue keeps ACE's
round trip to ACE's own objects, and the periodic check is now a non-blocking
`wl_display_dispatch_queue_pending()` on that queue. The address is still
re-sent for a settle window after the surface appears -- a compositor may not
have built its view when ACE first offers one -- and then left alone, since a
compositor restart replaces GDK's whole display (caught by `ensure_manager()`)
and a manager coming or going is reported on the registry listener.

The next work is porting more AROS commands while keeping the AROS source
behavior intact. Every command in the tree now parses its arguments with
AROS's own `ReadArgs()`, so a new port needs no argument work at all: `Dir`
proves the real pattern engine and directory enumeration run on the broker's
host-path model, and `Delete`/`Protect` prove the same for removal and for
protection bits in both directions. What a port costs now is whatever DOS
calls it makes that ACE has not implemented yet -- `Filenote` needs
`SetComment()` and `Copy` needs that plus `Write()` and `SetFileDate()`. See
TODO.md.
