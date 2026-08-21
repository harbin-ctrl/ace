# ACE todo

Work that is known to be needed and is not done. HANDOFF.md describes what ACE
is and how it is built; this file is only for outstanding items, with enough
evidence attached that whoever picks one up does not have to rediscover why it
matters.

The FMM policy pass is complete: `--root` is authorization-only, the
legacy mode/view switches are retired, and the installed polkit action is
`org.ace.Ace.FMM`. Remaining FMM gaps are listed below where they
affect path resolution, command reporting, or unsupported DOS calls.

## Also open

Smaller items found alongside the above, in rough order of value.

**Implement the Amiga clipboard stack.** The staged design is documented in
[`docs/clipboard-system.md`](docs/clipboard-system.md). Stage 1 adds the
public `clipboard.device` and `iffparse.library` compatibility contract;
later stages will implement the device, IFF parser, `CLIPS:`, Linux bridge,
and client migration. Preserve raw IFF and translate text at the host bridge
so non-text clipboard data is not discarded.

**Continue the current-console channel roadmap.** Stages 1 through 4 are now
implemented. Stage 1 is in
`src/console_channel.[ch]`: ACE's current GUI stream has an explicit channel
boundary, geometry state, raw-mode state, and opt-in raw byte tracing through
`ACE_DBGCON`/`ACE_DBGCON_INPUT`; ET obtains its size through the public
`CSI 0 q` query. Stage 2 binds DOS `SetMode()` and current aliases to the
shared native channel; Stage 3 gives each `CON:` window specification its own
channel; Stage 4 separates the launcher, Shell, handler, and packet-facing
console endpoint. Stage 5 is implemented: the fresh AmigaOS 3.1 ED 2.00
probe is the golden source for missing-file initialization, structural line
insertion, and current-line undo, while `tests/tine_screen_trace_test.py`
covers ET's startup, raw events, public size query, redraw, resize, mode
switching, and shutdown bytes at the ACE trace boundary. Keep the Amiga
`tools/amiga-debugcon` trace format byte-for-byte compatible with the ACE
output trace for future direct 68K captures.

**Make the Wayward beep transport backward-compatible.** ACE's new `Beep`
command validates only that `LABWC_PID` names a labwc process, then sends
`SIGUSR1`. New onscreen labwc installs a private handler, but Raspberry Pi's
older labwc does not, so the default `SIGUSR1` action terminates the
compositor. Replace the signal-only transport with a capability-safe channel,
preferably a per-instance Unix `SOCK_SEQPACKET` endpoint under
`XDG_RUNTIME_DIR`; an old compositor then has no endpoint and the command
quietly does nothing.

**Seven `rom/dos` functions ACE still hand-writes.** `AddPart`, `PathPart`,
`FilePart`, `SplitName`, `SetVar`, `GetVar`, `DeleteVar` all exist as AROS
sources and are reimplemented in `src/native_dos.c`. The path-manipulation
three have fiddly AmigaDOS edge cases -- trailing colons, `/` as parent
reference -- that upstream already gets right. Same macro-rename technique as
the `readargs.c` rules in the Makefile.

**Audit the remaining ACE stubs' return polarities.** `ErrorReport()` returned
`DOSFALSE` from the initial commit onward. `DOSFALSE` means "the user pressed
Retry", so once AROS's `getdeviceproc.c` was compiled in, `CD A:` spun at full
CPU forever instead of failing; fixed in 08edd19. The general hazard is that
every ACE stub is a contract with upstream code nobody on this side wrote, and
a stub that is merely *wrong* stays harmless until the AROS code that depends
on it gets compiled in. Worth going through the stubs deliberately rather than
finding the rest of them as hangs.

The `IoErr()` storage bug found while porting the argument parser is the same
shape and is worth reading as a warning: ACE kept `IoErr()` in a variable of
its own while `rom/dos/readargs.c` ends with `me->pr_Result2 = error;` and
never calls `SetIoErr()`, so every `ReadArgs()` failure reported no reason at
all. It was invisible for as long as only five commands used the real parser.

**No regression test for that hang.** Nothing in the suite would catch a
reintroduction. `tests/filesystem_translation_test.sh` is the natural home: a
`CD A:` case asserting a prompt non-zero exit under a timeout.

**Nothing owns `LIBS:`, `DEVS:`, `L:` or `FONTS:`.** They are established, and
they all resolve to `SYS:` because ACE has nothing to put in them -- which is
exactly what AROS does for a system missing those drawers, so it is correct
rather than pending. Worth revisiting the moment ACE has a loadable anything.

**A missing file reports "Error 2" from some commands.** `Type
rootfs:home/pi/nosuchfile` prints `Error 2` -- the raw Linux `ENOENT` -- and
`Why` repeats it, where the Shell reports the same class of failure as
`object not found`. So `PrintFault()` renders 205 correctly and the code
reaching it is genuinely 2. `Open()` maps `ENOENT` to
`ERROR_OBJECT_NOT_FOUND` on every path that returns NULL, and capturing errno
at the point of failure rather than reading it later did not change the
symptom, so the guess that an intervening call was clobbering it was wrong.
Whatever sets it is somewhere else. `Type` reads `IoErr()` immediately after
`Open()` returns (`workbench/c/Type.c:388`), so the value is ACE's, and the
directory case through the same path reports `object is not of required
type` correctly -- it is specifically the not-found code that arrives raw.

**The Makefile is not a prerequisite of what it builds.** Change a rule's
recipe and the object is not rebuilt, so the change appears not to work. This
cost time twice while porting Delete/Protect/Filenote, both times looking like
a fix that did nothing. It is not a one-line fix: roughly twenty recipes pass
`$^` straight to the compiler or linker, and adding the Makefile as a
prerequisite puts it in `$^` too, which fails with "file format not
recognized". Doing it properly means filtering it back out of every one of
those recipes. Until then, delete the object by hand after editing its rule.

**Build Amiga Vim and finish the remaining DOS calls.** The raw-console seam is
now in place. `src/amiga_console.c` forwards key bytes, `native_dos.c` owns
cooked editing for the GUI session, and `SetMode()`/`WaitForChar()` expose the
raw contract. `tests/native_input_test.c` covers the seam. Vim's untouched
normal-feature core now builds through `make vim VIM_SRC=...`; ACE supplies the
Amiga/runtime seams, path translation wrappers, runtime packaging, and xdiff
linkage in `tools/build-vim-ace.sh`. Both pseudo-terminal and live
`ace-console` smoke tests have launched Vim, edited and saved a file, and
exited cleanly. The external Vim checkout stays unmodified. Remaining work is
broader Vim integration: shell-command behavior, diff mode, and expanding the
live regression coverage. `SetProtection` is done.
