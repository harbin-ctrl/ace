# ACE handoff

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

The real ANSI/CSI parser, `rom/devs/console/support.c`'s `writeToConsole()`,
is compiled the same way and is now what the live `ace-console` window
actually calls: `src/console_device_bridge.c` builds one real `ConUnit` per
window (the class pair, a font, an `ace_gfx_create_rastport()`-backed
`RastPort`, a real `struct Window`) and `src/amiga_console.c`'s output path
(`apply_output()`, for the shell's own stdout, and `drain_editor_output()`,
for the console handler's self-echoed line-editing bytes) calls
`writeToConsole()` on it directly, the same call `console.c`'s real
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
confirmed present in `support.c`'s real command table; ANSI SGR color is not
-- this AROS checkout's `stdconclass.c` has no `C_SELECT_GRAPHIC_RENDITION`
case at all, confirmed by reading its dispatcher, not assumed.

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
./build/ace-broker /tmp/ace.sock
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

**What is still not real.** Keyboard input. `key_press()` in
`src/amiga_console.c` still hand-cooks CSI byte sequences for arrow keys/
Home/End instead of going through AROS's real `rom/devs/console/
cdinputhandler.c` (290 lines, real Intuition-level input-event handling) and
`rawkeyconvert.c` (54 lines, a thin wrapper over keymap.library's
`MapRawKey()`). This was assessed, not attempted: `MapRawKey()` needs a real
default `KeyMap` table (`AskKeyMapDefault()` returns
`KMBase(KeymapBase)->DefaultKeymap`, a field set at keymap.library init --
no self-contained default table turned up anywhere in `rom/keymap`, meaning
it is either loaded from a real `DEVS:Keymaps/` file ACE has no filesystem
for, or would need to be authored), and a GDK-keyval-to-Amiga-rawkey table
would need authoring from scratch either way. Materially larger and more
separable than the rendering work above; a later phase, not a loose end of
this one. Editing itself (cursor movement, backspace, history) already runs
on real AROS code, via `process_input()` from Phase 1 -- only the *encoding*
of GDK key events into the bytes `process_input()` expects is still ACE's.

Font and palette selection is meant to be entirely host-side: a GTK
preferences UI lets the user choose a family, size, and palette, persisted to
`$HOME/.config`, validated against `ace_gfx_font_family_complete()` so only
families with genuine regular/bold/italic/bold-italic faces are offered.
`aros_graphics_runtime.c`'s font/palette API already takes these as
parameters rather than hardcoding them (`src/console_device_bridge.c` and
`amiga_console.c` currently hardcode a candidate list -- `Liberation Mono`,
`DejaVu Sans Mono`, `monospace` -- as a placeholder), but the preferences UI
and config persistence do not exist yet. Also not implemented: window
resize (`NewWindowSize`, real AROS support exists in `consoleclass.c` but
nothing calls it, so the RastPort is fixed at `CONSOLE_WIDTH`/
`CONSOLE_HEIGHT`), cursor blink (real console.device does not appear to
drive this from a timer either -- `stdcon_drawcursor()` only fires on
explicit `RenderCursor`/`UnRenderCursor` calls tied to command processing and
window-active state), and DSR cursor-position-report replies
(`con_inject()` in `console_device_bridge.c` is a documented no-op: a
program that sends `ESC[6n` gets no reply, since answering it needs the
task/message-port layer described above).

After the display seam, the next work is expanding the DOS filesystem/device
seam and porting more AROS commands while keeping the AROS source behavior
intact.
