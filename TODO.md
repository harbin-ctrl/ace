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

**Raw console input, and Amiga Vim.** Scoped in conversation but never written
down beyond this note. The blocker is that line editing happens in the GUI
process: `src/amiga_console.c` `send_input()` feeds keystrokes to the AROS line
editor and writes to the child socket only once a whole line exists, so a child
can never see a keystroke. `amiga_con_handler_SetRaw()` in `src/con_handler.c`
already implements the raw/cooked switch and has no callers anywhere -- that
file is compiled only into `console-device-test`. The shape of the fix is to
move the line editor to the child side, which makes `SetMode()` an in-process
flag and needs no new IPC. Everything else Vim wants -- `SetMode`,
`WaitForChar`, `Delay`, `DateStamp`, `Rename`, `SetProtection`, `Info` -- is
small by comparison and useless without it. `SetProtection` is now done.
