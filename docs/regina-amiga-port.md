# Regina REXX on ACE

## Artifacts inspected

Two different things are commonly called “Regina for Amiga”:

* `Regina.lha` from Aminet is Regina 0.08i, release 7, dated 2000-09-20.
  It contains only a 68k AmigaOS executable (`rexx`), its icon, and a man
  page. It is an AmigaOS `LoadSeg()` binary, not a Linux executable and not
  source that ACE can link.
* `regina-rexx-3.9.7.tar.gz` is the current source distribution. It contains
  `makefile.amiga.gcc`, `makefile.amiga.sas`, the Regina static-library/SAA
  API, and the ARexx-related built-in functions.

The audit downloads were kept outside the repository at
`/tmp/ace-regina-audit/`, and did not survive a reboot -- neither is needed to
build, and the checksums below are recorded only so the artifacts can be
re-identified if they are fetched again:

```text
Regina.lha                         SHA256 05a338680783309180c10faee6a712c48d62905fdc702b75f7bdd9540f2a8d4a
regina-rexx-3.9.7.tar.gz           SHA256 f13701ebd542e74d0fc83b2a7876a812b07d21e43400275ed65b1ac860204bd4
```

The AROS source snapshot is from the AROS `contrib` repository at commit
`ec3f6b50cd9af84ea6bd3e581d93d0e874a6affb`; its Regina subtree is
`regina/`. This is the source snapshot to reproduce for the ACE port, and it
is checked out on this host -- see the next section.

The current source release is listed by the Regina project as 3.9.7, and its
documentation identifies AmigaOS as a static `libregina.a` / `rexx` target
with no dynamic library. The old binary is available from
[Aminet's Regina entry](https://aminet.net/dev/lang/Regina.lha); current
source and releases are listed in the
[Regina SourceForge files](https://sourceforge.net/projects/regina-rexx/files/).

## Where the source is, and how to get it again

The AROS Regina port lives in a **separate repository** from the AROS main
tree: `aros-development-team/contrib`, at the top-level path `regina/`. This
is the detail that is easy to lose -- neither `$HOME/aros` nor the
`ace-amiga/aros` copy in `$HOME/stash` contains a `contrib/` directory, so
searching an AROS checkout for Regina finds only `rexxsyslib`, `rexxsupport`,
and `rexxc`, never the interpreter.

It is checked out on this host at `$HOME/stash/aros-contrib`, pristine and
git-tracked, as a blobless sparse clone limited to `regina/`:

```sh
cd "$HOME/stash"
git clone --filter=blob:none --sparse \
    https://github.com/aros-development-team/contrib.git aros-contrib
cd aros-contrib
git sparse-checkout set regina
```

That is 403 files, about 6 MB of working tree plus 14 MB of git metadata.
The checkout is at contrib commit `ec3f6b50cd9af84ea6bd3e581d93d0e874a6affb`,
which is `master` at the time of writing and the same commit this document
first described.

**Keep it clean.** `git status` in `$HOME/stash/aros-contrib` should stay
empty. Any change Regina needs in order to build belongs in ACE, not in
Regina -- the point of the exercise is that ACE can run Regina, not that
Regina was altered until it ran. Where a Regina-side change is genuinely
unavoidable, follow the pattern ACE already uses for the AROS main tree: a
minimal patch under `patches/`, applied to the external checkout by hand and
reviewable as a diff, never an edit committed silently into the vendor tree.

Three named things are all called "Regina for Amiga"; only the third is the
one this port uses:

| Artifact | What it is | Use here |
| --- | --- | --- |
| `Regina.lha` (Aminet) | 0.08i 68k AmigaOS binary, 2000-09-20 | historical reference only |
| `regina-rexx-3.9.7.tar.gz` | current generic source, Unix/`os_other.c` | language-engine reference only |
| `contrib/regina` | **Regina 3.5, the AROS port, 31 Dec 2009** | **the source base** |

### What the AROS build description actually says

`regina/mmakefile.src` is the authority on how this is meant to be compiled,
and it is worth reading before trusting any generic Regina makefile. It
builds three targets, and `regina/regina.ver` pins the version at 3.5,
`31 Dec 2009`:

* **`rexx`** -- the command. Object list is the shared `OFILES` plus `rexx`,
  `nosaa`, and `mt_notmt`. Note that `OFILES` ends in `arxfuncs amifuncs
  os_amiga` -- the Amiga OS adapter, *not* `os_other`.
* **`regina.library`** -- the module, built from `OFILES` plus `rexxsaa`,
  `rexx`, `client`, `mt_amigalib`, `isreginamsg`, and `regina_init`, with
  `-DRXLIB -DINCL_REXXSAA -DAPIENTRY= -DNO_EXTERNAL_QUEUES -Dlint`. The
  `-DAPIENTRY=` here is the same definition the ACE header probe had to
  supply by hand.
* **`regina`** -- a thin client linking the shared library.

Two further details fall out of that file. `%copy_includes includes=rexxsaa.h`
is where `rexxsaa.h` comes from, which is why ACE's `proto/regina.h` cannot
compile standalone yet: that header ships with the Regina source rather than
with the AROS SDK. And the acceptance scripts are already written --
`regina/arexx_test/{addsupport,typepkt,forbid1,forbid2,ptrarith,ados}.rexx`.

## Correct source base: the AROS port

The current generic Regina tarball is not the source base for this job. The
AROS `contrib` tree contains the actual AROS/Amiga port (Regina 3.5, dated
31 Dec 2009) under `regina/`. It includes the pieces missing from the generic
tarball:

* `os_amiga.c`, `amifuncs.c`, and `mt_amigalib.c`, which select Amiga/AROS
  DOS, Exec, memory-pool, task, and signal semantics;
* the AROS build description in `mmakefile.src`, which builds the `rexx`
  command, the `regina.library` module, and `RexxMast`;
* `rexxmast/RexxMast.c`, which creates the public `REXX` port, receives
  `RexxMsg` commands, starts scripts, and handles `RXADDLIB`, `RXADDCON`,
  `RXCLOSE`, and the Regina-private message actions;
* `rexxmast/listen4msg.c` and `sendrexxmsg.c`, which are small, useful IPC
  acceptance tests.

This is the code we need to port into ACE. The 68k `Regina.lha` executable is
only a historical binary reference; the generic 3.9.7 tree is useful for
language-engine changes, but neither is the AROS integration source.

AROS documentation describes `regina.library` as a per-task library linked
with `arosc.library`; that is important for ACE because the library must use
ACE's task-local state and DOS/Exec shims rather than a Unix libc process
model. The AROS project also explicitly built Regina as the base for its
ARexx implementation and added message handling support. See the
[AROS build notes](https://en.wikibooks.org/wiki/Aros/Developer/BuildSystem)
and [AROS Rexx history](https://aros.sourceforge.io/news/archive/2002.html).

## What compiles today

With the source checked out, the probe is no longer hypothetical. Compiling
individual translation units with `-fsyntax-only`, AROS's own headers, and
the defines `mmakefile.src` uses (`-D__AROS__ -D_GNU_SOURCE
-DNO_EXTERNAL_QUEUES -DAPIENTRY=` plus the four `REGINA_VERSION_*` strings):

| Source | Result |
| --- | --- |
| `strings.c`, `error.c`, `convert.c`, `arxfuncs.c` | clean |
| `amifuncs.c` | clean |
| `os_amiga.c` | 13 diagnostics, all one cluster |

**Include order matters, and it is not obvious.** ACE has two compatibility
header trees, and Regina needs `compat/aros-real/include` *ahead of*
`compat/include`. With `compat/include` first, `amifuncs.c` produces 26
diagnostics -- `struct ExecBase has no member named PortList`, and implicit
declarations of `CreateMsgPort`, `FindPort`, `PutMsg`, `WaitPort`,
`CreateTask`, `SetTaskPri`, `FindName`, `GetHead`, `GetSucc` -- because
`compat/include/exec/` shadows the `aros-real` `execbase.h` and `lists.h`
with versions that predate this work. Reverse the two and the same file is
clean. Whether the two trees should be merged, or Regina should simply be
built against `aros-real` first the way the other real-AROS objects are, is
an open question worth settling before the build rules are written.

Everything left in `os_amiga.c` is one thing: **ACE cannot start a process
the AmigaDOS way yet.** The diagnostics are `CreateNewProcTags` and
`SystemTags`, the `NP_*` tags they take (`NP_Entry`, `NP_Cli`,
`NP_Synchronous`, `NP_Input`, `NP_Output`, `NP_Error`, `NP_CloseInput`,
`NP_CloseOutput`, `NP_CloseError`), and `StrDup`. ACE's
`compat/include/dos/dostags.h` carries the `SYS_*` tags in full but only
`NP_Dummy` and `NP_StackSize` of the `NP_*` set.

That makes the next milestone concrete and squarely an ACE one, which is the
point of the exercise: ACE should be able to run Regina because ACE
implements the contract, not because Regina was cut down to fit. In order:

1. Complete `NP_*` in `compat/include/dos/dostags.h`.
2. Implement `CreateNewProcTags`/`CreateNewProc` on ACE's process and broker
   model, honouring `NP_Input`/`NP_Output`/`NP_Error` and the matching
   `NP_Close*` ownership rules, `NP_Cli`, and `NP_Synchronous`.
3. Implement `SystemTags` in terms of the same launcher ACE already uses for
   `RunCommand`, so `ADDRESS COMMAND` inherits the CLI properly (see below).
4. Add `StrDup`.

Only then is a whole-file compile of the engine the right test, and after
that the link, which is where the Exec and DOS surfaces get exercised for
real rather than syntactically.

## Required ACE work

### Why the previous build attempt was the wrong test

The earlier compilation used the generic 3.9.7 `makefile.amiga.gcc`. That
makefile selects `os_other.c` and the generic non-Amiga thread layer; it is not
the AROS build in `contrib/regina/mmakefile.src`. Its Unix-looking linker
symbols therefore describe the wrong source/configuration selection, not
missing ACE functionality. We will not satisfy those symbols with Unix libc
or continue that build path.

The correct build test is the AROS source set with AROS headers and the ACE
equivalents of `exec`, `dos`, `rexxsyslib`, and `rexxsupport`. The first
milestone is the AROS Regina command plus its static/shared engine; the next
is `RexxMast` and the named `REXX` message port.

### AROS build attempt

I tried the actual AROS build path, rather than invoking the generic Regina
makefile:

1. Running `make -f mmakefile.src` directly fails with `missing separator`.
   That file is a MetaMake input, not a GNU Makefile, so it must be invoked
   by an AROS-configured `mmake` build.
2. I unpacked the AROS source tree, configured `pc-x86_64`, and built its host
   tools. This successfully built AROS `mmake` and the target-tool wrapper.
3. `make contrib-regina-rexx` then fails before compiling Regina. The target
   wrapper has no AROS compiler command because no AROS cross-toolchain or
   sysroot is installed; its generated command is effectively
   `exec -specs=...`, and `config/config.log` reports “C compiler cannot create
   executables.”

This is an environment/toolchain blocker, not a Regina source error. There is
no `x86_64-aros-gcc` (or AROS SDK/sysroot) installed in this workspace. The
next valid build step is to install or point the AROS build at that toolchain;
using the host compiler or satisfying the missing symbols with Unix libraries
would not produce an AROS build and is explicitly out of scope.

### ACE-host build probe

Because ACE deliberately builds AROS sources as host-native code, I also ran a
syntax-only probe of the AROS Regina `amifuncs.c` with the same compatibility
include paths used by ACE's existing AROS objects. After supplying the normal
`APIENTRY` definition, the first ACE-specific failures are:

* `proto/rexxsyslib.h` does not exist in ACE; and
* `struct Library` is incomplete for AROS's `rexx/rxslib.h`.

Those are the actual next porting seams. The headers are not proprietary or
lost: AROS generates them from its library description files. The open-source
AROS SDK archive `murks_i386_aros.zip` contains the generated
`Development/include/proto/rexxsyslib.h`, `clib/rexxsyslib_protos.h`, and
`proto/regina.h` files. The current AROS source tree contains the canonical
`rexx/storage.h`, `rexx/rxslib.h`, `rexx/errors.h`, and `rexx/rexxcall.h`
definitions, but not the generated `proto/` directory.

ACE now carries host-ABI compatibility copies of the Rexx storage/base/error
headers and the direct-call form of `proto/rexxsyslib.h` and its C prototypes.
A standalone header probe passes; `proto/regina.h` is the one that does not
compile alone, because it includes `rexxsaa.h`, which ships with the Regina
source rather than with the AROS SDK. That Exec message-port/task ABI surface
has since been supplied, and `amifuncs.c` now compiles clean -- see "What
compiles today". These headers are an interface step, not a claim that Regina is
already linked or runnable in ACE.

The probe also exposed a real Exec detail rather than a Regina-specific issue:
AROS declares `WaitPort()` as returning the first queued `Message *` while
leaving it on the port. ACE's runtime declaration and implementation now match
that contract, so the AROS `listen4msg` and `sendrexxmsg` sources compile
without changing their native message flow.

### 1. Import and compile the AROS source set

Bring the AROS `contrib/regina` sources into an ACE-owned vendor/build area,
preserving their LGPL notices and the AROS-specific files. Add the AROS
headers that the sources require (`rexx/storage.h`, `rexx/rxslib.h`,
`rexx/errors.h`, `rexx/rexxcall.h`, and the `proto/*` declarations), then
compile the engine with `__AROS__` and `RXLIB` as the AROS makefile does.

Do not substitute `os_other.c`, `mt_notmt.c`, or a Unix `RexxMast` equivalent.
The ACE implementation should provide the AROS entry points those files call.

### 2. Route every pathname through ACE DOS

Regina's generic file code uses `fopen`, `fread`, `fwrite`, `chdir`, and host
path strings. For ACE these must be translated through `Lock`/`Open`/`Read`/
`Write`/`Seek`/`Close`, or through a single adapter that calls those routines.
The adapter must preserve:

* Amiga `:` volume names and ACE's `/` parent traversal;
* assignments and current directory;
* softlinks and dangling-softlink behavior;
* case-fold mappings, including the new `^` spelling for Linux collisions;
* ACE RAM: and other broker-backed devices.

No Regina code should bypass the broker with a raw Linux path once it is
running as an ACE command.

### 3. Make `ADDRESS` Amiga-correct

Regina's ordinary `ADDRESS SYSTEM`, `ADDRESS PATH`, and `ADDRESS COMMAND`
paths assume a host process launcher. In ACE they should invoke
`RunCommand`/the broker and inherit the CLI's input, output, error, current
directory, return code, and break signals. `CTRL-C` must stop interpretation
at a Rexx statement/command boundary and return to the shell in the same way
as other ACE commands.

### 4. Add the AROS Rexx libraries and public message ports

ARexx is not just a language feature: applications expose named public ports,
and scripts send `RexxMsg` messages to those ports. The AROS source already
defines the exact `RexxMsg` layout and actions; ACE currently has task
registration and process-local message-port primitives, but not the complete
cross-process public-port contract.

The broker needs a named-port service with:

* `CreateMsgPort`, `DeleteMsgPort`, `AddPort`, `RemPort`, `FindPort`;
* cross-process `PutMsg`, `GetMsg`, `WaitPort`, and `ReplyMsg`;
* message ownership, reply-port lifetime, and task-exit cleanup;
* a wire representation for `RexxMsg` fields and argument strings;
* deterministic failure when a target port disappears.

Only after this exists should Regina's ARexx address environment be wired to
ACE. This also gives the spatial file manager, editors, and demos a useful
inter-application API independent of Regina.

### 5. Port `RexxMast` from the AROS source

Port the AROS `rexxmast/RexxMast.c` behavior to an ACE service or
broker-owned dispatcher that:

* creates the standard Rexx system port and handles queue requests;
* resolves `ADDRESS <port>` names through `FindPort`;
* sends commands and waits for replies with `RexxMsg` result codes;
* implements public and private Rexx queues with `RXQUEUE` semantics;
* provides `RESULT`, `RC`, `SIGL`, and command failure behavior correctly;
* lets applications register their own ports and command vocabularies.

Regina's SAA API can remain the in-process interpreter interface. The ACE
port layer is the missing bridge between that interpreter and other ACE
processes.

### 6. Package and test it as an ACE command

Install the interpreter and support files as:

```text
SYS:C/REXX
SYS:C/REXXMAST       (when the dispatcher exists)
SYS:Libs/            (only for deliberately supported extensions)
SYS:Prefs/           (language/error-message configuration if needed)
```

The first acceptance tests should cover:

1. a pure Rexx arithmetic/string script;
2. `ADDRESS COMMAND` invoking `Echo`, `Dir`, and `Status`;
3. ACE volume paths, assignments, RAM:, softlinks, and case-collision names;
4. `CTRL-C`, `CTRL-D`, and task cleanup while a script is running;
5. two processes communicating over a named Rexx port;
6. queue FIFO/LIFO, wait, timeout, delete, and broker restart behavior;
7. an application registering a port and receiving a command from Rexx.

## Recommended implementation order

1. Compile the AROS `contrib/regina` engine and `rexx` command against ACE's
   AROS headers and DOS/Exec shims.
2. Run the AROS `arexx_test` scripts through ACE.
3. Finish broker public ports and cross-process message ownership.
4. Port `RexxMast`, then run `sendrexxmsg`/`listen4msg` as ACE tests.
5. Add application-facing port registration to the spatial file manager and a
   small demo.

The old 0.08i binary is valuable as a historical compatibility reference. The
AROS `contrib/regina` source is the correct base for ACE; its language engine,
Amiga OS adapter, `regina.library`, and `RexxMast` provide the behavior we need
to preserve while replacing only the OS calls with ACE's runtime.
