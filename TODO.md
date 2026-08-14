# ACE todo

Work that is known to be needed and is not done. HANDOFF.md describes what ACE
is and how it is built; this file is only for outstanding items, with enough
evidence attached that whoever picks one up does not have to rediscover why it
matters.

## Put the AROS_SHn commands back on the AROS argument parser

**Status:** not started. This is the direct continuation of "Port AROS argument
parsing and Type/Rename" (9705c1b), and it is easy to believe that commit
finished the job. It did not.

### What is already done

9705c1b replaced ACE's hand-written argument parser with the real thing:
AROS's `rom/dos/readargs.c`, `readitem.c`, `freeargs.c`, `findarg.c` and
`strtolong.c` are compiled under private names via `-DReadArgs=ace_aros_ReadArgs`
and friends, and `ReadArgs()` in `src/native_dos.c` is now a one-line forwarder
to `ace_aros_ReadArgs()`. That work is correct and is not in question.

### What is still wrong

Most commands never reach it.

AROS commands declare their arguments with the `AROS_SHn`/`AROS_SHA` macros.
In real AROS those macros expand into a `ReadArgs()` call --
`compiler/include/aros/shcommands_notembedded.h:94`:

    __rda = ReadArgs(templ, __shargs, __rda2);

with `AllocDosObject(DOS_RDARGS, NULL)` at line 85 and `FreeArgs(__rda)` at
line 108 around it.

ACE does not use that header. `compat/include/aros/shcommands.h` is a 170-line
reimplementation of it whose macros expand to `native_parse_args()` --
`src/native_args.c`, 357 lines of hand-rolled template parsing that predates
9705c1b and is still the live parser for most of the fleet.

So there are two argument parsers in the tree, and which one a command gets
depends only on how its AROS source happens to declare its arguments:

| Path | Commands |
| --- | --- |
| `AROS_SHn` -> ACE `native_parse_args` | Alias, Ask, CD, Dir, Echo, EndCLI, FailAt, Fault, Get, Getenv, PathPart, Prompt, Set, Unalias, Unset, Why |
| direct `ReadArgs()` -> real AROS | Assign, MakeDir, NewCLI, Rename, Type |

Sixteen commands against five. `Dir` is on the hand-rolled side, which is easy
to get wrong when reasoning about this from the commit message alone.

### Why it matters

This is the largest single reimplementation left in the tree measured against
the project's goal of having AROS do everything AROS can do. It is also the
cheapest to reason about: one header is the entire reason sixteen commands
parse their arguments with ACE code instead of Commodore-equivalent code.

The behavioural risk is not hypothetical. AmigaDOS templates carry a lot of
detail -- `/S`, `/K`, `/N`, `/M`, `/A`, `/F`, keyword-equals forms, quoting,
multiple `/M` interaction, and the `?` help convention that re-prompts on the
input stream. `native_args.c` implements a subset. Anywhere the subset and the
real parser disagree, sixteen commands are wrong in a way no test currently
looks for.

### The work

Replace `compat/include/aros/shcommands.h` with AROS's real macro header, the
same way every other AROS source in the tree is used: compile it, do not
restate it.

The pieces the macro expansion needs are already exported by ACE --
`AllocDosObject`, `FreeDosObject`, `ReadArgs`, `FreeArgs`, `IoErr`,
`PrintFault` are all in `src/native_dos.c`. That makes this plausible rather
than speculative, but it has not been attempted, so treat the following as the
expected shape and not as a verified plan:

* point the include at `$(AROS_ROOT)/compiler/include/aros/shcommands.h`, which
  redirects to `shcommands_notembedded.h` unless `USE_EMBEDDED_COMMANDS` is set;
* satisfy whatever that header pulls in that the compat tree does not yet have;
* check the `__shargs` array plumbing and `RDArgs` lifetime against ACE's
  `AllocDosObject(DOS_RDARGS)` implementation, which is the most likely place
  for this to not simply work;
* delete `src/native_args.c` and remove `$(BUILD)/native_args.o` from the
  twenty link and dependency lines in the Makefile that name it, plus its own
  build rule at Makefile:164. It becomes genuinely dead
  only after this change -- **it is live code today**, and deleting it before
  the header is replaced breaks the build.

### Done when

* `compat/include/aros/shcommands.h` no longer exists, or contains nothing but
  an include of the AROS header;
* `src/native_args.c` is deleted and unreferenced;
* `nm build/Echo | grep native_parse_args` finds nothing;
* the full test suite passes, and each of the sixteen commands above has been
  exercised with a real template case -- at minimum a `/S` switch, a `/K`
  keyword, an `/M` multi-argument, and `?` help -- because the current tests do
  not distinguish the two parsers.

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

**No regression test for that hang.** Nothing in the suite would catch a
reintroduction. `tests/filesystem_translation_test.sh` is the natural home: a
`CD A:` case asserting a prompt non-zero exit under a timeout.

**`Assign FOO: NOSUCH:` exits 0.** Assigning to a target that does not exist
reports success. Observed from a standalone binary with no `ACE_SESSION` set,
so this may just be session scoping rather than a real defect -- it has not
been investigated.

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
small by comparison and useless without it.
