# ACE todo

Work that is known to be needed and is not done. HANDOFF.md describes what ACE
is and how it is built; this file is only for outstanding items, with enough
evidence attached that whoever picks one up does not have to rediscover why it
matters.

## Port List

`List` is the AmigaDOS command that displays a file's comment, and the first
consumer of the `fib_Comment` read path `Filenote` added -- until it lands,
`tests/dos_comment_test.c` is the only thing that reads a comment back. It is
1223 lines of AROS and wants `DateStamp()`, `DateToStr()` and `StrToDate()`,
which ACE does not have yet; `Copy` wants the date calls too.

## Also open

Smaller items found alongside the above, in rough order of value.

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

**Port `Path`, and give the CLI a command directory list.** `loadCommand()`
searches `cli_CommandDir` between the current directory and `C:`, and ACE
leaves that list empty, so `C:` is doing the whole job alone. `Path` is on the
wrong side of the line for an unmodified port -- it writes the shell's own
CLI, which an ACE command cannot do from its own process, the same wall
`Execute` hit -- so it wants the list kept in the broker and rebuilt into
`cli_CommandDir` by `Cli()`, plus an ACE-written `Path` that edits it there.
Until then `Path` is missing from the Startup-Sequence, which is the one line
of AROS's own that ACE cannot yet run.

**Port `Copy`.** The Startup-Sequence's `Copy ENVARC: ENV: ALL` is done by the
broker instead, which is the right effect at the wrong layer -- it is a script
step, and ACE's script layer cannot express it. `Copy` is 2673 lines of AROS
and wants `DateStamp()`, `DateToStr()` and `StrToDate()`, the same three
`List` is waiting on.

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

**`ace-brokerctl assign` always fails.** `ace-brokerctl assign WORK: /tmp` --
the form README documents -- exits 1 and says nothing, on this checkout and at
62c485d before it, so it is not a regression from the parser work. The `Assign`
command itself works and is covered by the test suite, so this is the control
tool's own path into `native_broker_assign()`. Not investigated.

**`Assign FOO: NOSUCH:` exits 0.** Assigning to a target that does not exist
reports success. Observed from a standalone binary with no `ACE_SESSION` set,
so this may just be session scoping rather than a real defect -- it has not
been investigated.

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
