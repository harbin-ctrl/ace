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
`consoleclass.c` from `rom/devs/console` -- are now compiled unmodified
against a real `graphics.library`, `src/aros_graphics_runtime.c`. Unlike every
other seam in ACE, this one is authored rather than compiled from AROS
source: graphics.library is the actual hardware boundary, the point past
which "real AROS code" would mean a HIDD driver stack ACE does not want. It
renders through cairo/fontconfig onto an ACE-owned `RastPort`/`BitMap`, with
glyphs from a host TrueType font rather than a bitmap font ACE would have to
ship -- see the file header for what AROS's console classes do and do not
require of a font. `make test-graphics` constructs a real `ConUnit` through
`NewObjectA()` and drives it with AROS's own `Console_DoCommand()` macro,
checking the resulting pixels.

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
AROS BOOPSI, and the pixel-facing half of AROS's console.device against the
compatibility headers in `compat/`.

The display seam still sits one layer too high in the *running shell*: the
live `ace-console` GTK app still uses ACE's own `console.device` substitute,
`src/console_terminal.c` and `src/amiga_console.c`, unchanged. What Phase 2
added is proof, not yet wiring -- `stdconclass.c` and `consoleclass.c` compile
and run correctly against a real `graphics.library`
(`src/aros_graphics_runtime.c`, `make test-graphics`), but nothing in the
live app calls them yet. `console.c`, the device's own `BeginIO`/task/message-
port entry points, is also not compiled: it needs real task creation and I/O
queueing that the ACE seam does not have yet, and was out of scope for
proving the graphics primitives correct.

The remaining steps to retire `console_terminal.c`/`amiga_console.c`:

1. Compile `console.c` and wire `aros_console_editor.c`'s
   `OpenDevice("console.device", ...)` call to the real device's `BeginIO`
   instead of the synthetic one in `src/aros_exec_runtime.c`, using
   `ace_gfx_create_rastport()` for the unit's `RastPort`/`BitMap`/`TextFont`.
   `console.c`'s own real dependencies (`NewCreateTask`, `BeginIO`, message
   ports, `Alert`, `AskKeyMapDefault`) are not yet part of any ACE seam.
2. A minimal Intuition `Window`/`Screen` over the GTK surface -- `RastPort`
   and `TextFont` already come from `aros_graphics_runtime.c` -- then compile
   `cdinputhandler.c`/`rawkeyconvert.c` and feed rawkey codes and qualifiers
   instead of the pre-cooked CSI sequences `key_press()` in
   `src/amiga_console.c` currently synthesises.
3. Reduce `src/amiga_console.c` to window, font choice, blit, and input
   delivery, and delete `src/console_terminal.c` and its test.

One collision to resolve at step 1: `src/aros_console_editor.c`,
`src/aros_boopsi_runtime.c`, and `src/aros_graphics_runtime.c` each define
overlapping small Exec calls (`AllocMem`/`FreeMem`/`AddTail`/`Remove`/`SetMem`
among them). They are currently in separate link targets. All use the same
real `exec/` struct shapes, so unifying them into one Exec seam should be
mechanical, just tedious.

Font and palette selection is meant to be entirely host-side: a GTK
preferences UI lets the user choose a family, size, and palette, persisted to
`$HOME/.config`, validated against `ace_gfx_font_family_complete()` so only
families with genuine regular/bold/italic/bold-italic faces are offered.
`aros_graphics_runtime.c`'s font/palette API already takes these as
parameters rather than hardcoding them, but the preferences UI and config
persistence do not exist yet -- `ace-console`'s eventual font choice is a
product decision for whoever wires step 2, not an engineering constraint from
this seam.

After the display seam, the next work is expanding the DOS filesystem/device
seam and porting more AROS commands while keeping the AROS source behavior
intact.
