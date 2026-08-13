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

The first read-only filesystem-facing command is now real AROS `Dir.c`, built
with the original DOS `patternmatching`, `MatchFirst`/`MatchNext`/`MatchEnd`,
and `ExAll` implementations. `src/native_dos.c` supplies the host-backed
`Lock`/`Examine`/`ExNext`/`DupLock`/`CurrentDir` seam, Unix metadata conversion,
and the RawDoFmt-compatible `VPrintf` path that `Dir` uses for its formatted
columns. The command has been exercised against regular and nested host
directories, `#?` patterns, `ALL`, `DIRS`, `FILES`, and missing paths. The
compatibility layer intentionally leaves the DOS root's optional `*` wildcard
flag disabled, matching the configured AmigaDOS pattern behavior.

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
itself (cursor movement, backspace, history) already runs on real AROS
code, via `process_input()` from Phase 1 -- only the *encoding* of GDK key
events into the bytes it expects is ACE's, and stays that way.

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

**Except when AROS clears the console itself.** `console_newwindowsize()`
clamps the cursor into the new grid, and `stdcon_newwindowsize()` responds to
a cursor that moved by clearing the whole console and redrawing the cursor --
a character-cell renderer with no retained character map has no way to tidy
up the cursor it left at the old position without wiping what it cannot
redraw. That is real AROS code doing what it has always done, and it is why
shrinking a window made the text vanish once resize stopped replaying: the
old replay-everything path had been repainting what AROS had just erased,
without anyone noticing AROS was erasing it.

`ace_console_device_resize()` therefore reads `cu_XCP`/`cu_YCP` around the
`Console_NewWindowSize()` call -- the same two fields `stdconclass.c` itself
tests -- and repaints the retained stream when they moved, sending a real
form feed first because the replay has to start at the top left and AROS has
just left the cursor wherever it clamped it to. Clamping is only ever
downwards, so enlarging a window never triggers it and stays free; shrinking
costs a repaint on the steps that cross a cell boundary, about 80 ms, and
nothing on the steps in between.

The bridge test checks this by looking for ink in the console's *upper half*
after a shrink. A whole-frame ink check passes on a console that has been
completely wiped, because the cursor block is itself non-background -- which
is exactly why the original resize assertion did not catch this. GTK
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

The next work is expanding the DOS filesystem/device seam beyond this first
read-only command and porting more AROS commands while keeping the AROS source
behavior intact. `Dir` is a useful baseline for those ports: it already proves
the real AROS pattern engine and directory enumeration can run on the broker's
host-path model.
