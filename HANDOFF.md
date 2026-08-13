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
     test-console-device test-exec-compat test-boopsi
```

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
compiles selected AROS commands, the AROS shell, the AROS console handler and
AROS BOOPSI against the compatibility headers in `compat/`.

The display seam is the current focus. It presently sits one layer too high:
ACE substitutes its own `console.device` and does the ANSI/CSI interpretation
and glyph rendering itself, in `src/console_terminal.c` and
`src/amiga_console.c`. AROS therefore does none of the display work. Nothing
from `rom/devs/console` is compiled yet.

Moving the seam down to Intuition is bounded work. Every graphics and
Intuition call in the whole of `rom/devs/console` is: `SetAPen`, `RectFill`,
`SetDrMd`, `ScrollRaster`, `Text`, `Move`, `SetSoftStyle`, `SetBPen`,
`RefreshWindowFrame` and `RawKeyConvert`. The remaining steps are:

1. A `graphics.library` seam over an ACE-owned `struct RastPort` and
   `struct BitMap` implementing those ten calls, with a real `struct TextFont`.
   This is the one part that must be written rather than compiled, because it
   is a genuine hardware boundary. An embedded 8x8 bitmap font makes `Text()`
   a blit and keeps the seam testable headless.
2. Compiling `rom/devs/console/{console,consoleclass,stdconclass,support}.c`
   against it, and retiring `src/console_terminal.c`.
3. A minimal Intuition `Window`/`Screen`/`DrawInfo` over the GTK surface, then
   compiling `cdinputhandler.c` and `rawkeyconvert.c` and feeding rawkey codes
   and qualifiers instead of the pre-cooked CSI sequences `key_press()` in
   `src/amiga_console.c` currently synthesises.
4. Reducing `src/amiga_console.c` to window, font, blit and input delivery.

One collision to resolve at step 2: `src/aros_console_editor.c` and
`src/aros_boopsi_runtime.c` both define `AllocMem`, `FreeMem`, `AddTail` and
`Remove`. They are currently in separate link targets. Both use the same
`exec/lists.h` macros, so unifying them into one Exec seam should be
mechanical.

After the display seam, the next work is expanding the DOS filesystem/device
seam and porting more AROS commands while keeping the AROS source behavior
intact.
