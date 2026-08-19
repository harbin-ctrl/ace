# Regina and ARexx on ACE: where it stands, and what to do next

Read `docs/regina-amiga-port.md` first. It explains which Regina is the right
one and why, and its "Required ACE work" section is still the shape of the
job. What it says about *what compiles* is out of date; this file supersedes
that part and carries the plan forward.

## Orientation

Read in this order:

1. `docs/regina-amiga-port.md` -- the source base, the three things called
   "Regina for Amiga", and why only `contrib/regina` is the right one.
2. `README.md`, "Build and run" -- ACE installs into `$HOME/.local`, every ACE
   program finds its companions beside its own executable, and the broker is
   one per user.
3. `src/broker_protocol.h` -- the wire format, and `AMIGA_BROKER_TASK_ATTACH`
   in particular. The task subsystem is the working model for anything that
   needs the broker to push to a process rather than answer it.
4. `git log --oneline a0a4286..` -- the Regina work, most recent last. Each
   commit message explains a decision that is not obvious from the diff.

## Facts you will not find in those documents

**The Regina source is vendored at `third_party/regina`,** imported unmodified
from contrib commit `ec3f6b50cd9af84ea6bd3e581d93d0e874a6affb`. It used to be
an external sparse checkout at `$HOME/stash/aros-contrib`, and that path is
still honoured -- `REGINA_SRC` or `AROS_CONTRIB_ROOT` picks it -- but the
in-repo tree is what `make regina` builds by default.

Moving it in-repo does not change the rule it was out-of-repo to enforce:
**keep `git status` clean under `third_party/`.** Anything Regina needs belongs
in ACE, or -- only if genuinely unavoidable -- in a reviewable patch under
`patches/`. The point of the exercise is that ACE implements the contract, not
that Regina was cut down until it fitted, and a diff against upstream is a bug
report about ACE. The difference now is that nothing external stops an edit,
so the discipline has to be deliberate: `git diff third_party/` is the check
that used to be free. `third_party/PROVENANCE.md` records the origin and
commit.

**`grep -r` silently skips `amifuncs.c`.** It is ISO-8859 text (a `©` in its
copyright header), so grep decides it is binary and says nothing. You will
conclude a symbol is absent when it is not. Use `grep -a` on that tree. This
cost real time.

**`src/aros_shell_runtime.c` is dead code.** It has a build rule and nothing
links it -- `AROS_SHELL_NAMES` in the Makefile does not include `runtime`. The
live Shell is AROS's own `Shell.c` via `build/aros-real-shell.o`. The dead file
reads exactly like the live path, including a `checkLine()` and an
`executeLine()`. Editing it changes nothing.

**Build paths are absolute.** `$(BUILD)` is `$(CURDIR)/build`, so
`make build/foo.o` fails with "No rule to make target". Use
`make $PWD/build/foo.o`.

**The broker's socket name is keyed to the protocol version, which is a hash
of `broker_protocol.h`.** So a change confined to `broker.c` does *not* get a
new socket: the next client finds the broker that is already running -- an
older binary, possibly days old -- and talks to it happily. A test then
exercises the old broker and reports whatever that one does. This passed a
deliberately broken `drop_connection_port_channels()` before it was noticed.
Any test that asserts on broker behaviour must run against a broker of its
own, via `ACE_BROKER_SOCKET`; `tests/broker_port_channel_test.sh` is the
shape, and `tests/filesystem_translation_test.sh` is the older example.

**Make's built-in rules will rewrite the Regina source in place.** Regina ships
a generated `yaccsrc.c` beside its `yaccsrc.y`, and `lexsrc.c` beside
`lexsrc.l`. Make's implicit `%.c: %.y` rule considers the `.y` newer, runs
`yacc`, and moves the result *over the checked-in `.c`, in the source
directory* -- so the tree that has to stay clean quietly stops being clean,
and the regenerated file does not even compile. The Makefile cancels both
implicit rules. Do not remove that cancellation. This mattered when the source
was an external checkout and matters more now that it is `third_party/regina`,
where the damage lands in this repository's own `git status`.

**A rule's prerequisites are expanded where the rule is written, not when it
runs.** `DOS_RUNTIME_OBJ` is defined two-thirds of the way down the Makefile;
a link rule written above that point sees it as empty and fails with a wall of
undefined references from a file that is, in fact, in the link. That is why
the Regina link rules sit near `ace-user-shell` rather than with the other
Regina variables.

**`-w` does not silence gcc 14.** Implicit function declarations, implicit
`int`, and the pointer/integer conversions are *errors* by default now, not
warnings, and `-w` does not demote an error. Regina is 2009 C and trips all of
them, so `REGINA_CFLAGS` names each one as an explicit `-Wno-`.

**Test objects only build under their own test targets.** `make all` will not
catch a test that no longer compiles. After changing any API a test calls, run
the tests, not just the build.

**`ace-console` links `aros-exec-runtime.o` but deliberately not
`broker_client.o`** -- it is a GUI process with no DOS session. That is why the
broker port calls in `aros_exec_runtime.c` are `__attribute__((weak))` and
checked for NULL before use. Do not "fix" this by adding `broker_client.o` to
the console link.

**A built `rexx` must sit beside `ace-user-shell`.** `ADDRESS COMMAND` goes
through `SystemTags()` -> `launch_command()`, which finds the shell next to the
running executable. Run `rexx` from anywhere else and `ADDRESS COMMAND`
silently does nothing while returning 0.

**`compat/aros-real/include/proto/exec.h` is hand-written and partly wrong.**
Three return types disagreed with AROS and with ACE's own definitions --
`FindTask`, `Signal`, `SetTaskPri` -- each found only when something finally
included both compat trees at once. Assume more are wrong. Check against
`$HOME/aros/rom/exec/*.c`, which is the authority.

**Include order for Regina is `compat/aros-real/include` before
`compat/include`,** plus `-include ace_regina_compat.h` from
`compat/regina/include`. That header exists because the aros-real tree's own
thin `proto/dos.h` and `proto/alib.h` win the lookup and hide `StrDup()` and
`SystemTags()`, which ACE does implement. Do not add a third `proto/` tree; it
would add a third candidate to the same ambiguous lookup.

## Building it

```sh
make regina
```

That is the whole thing. It compiles all 40 sources of the AROS `rexx` target
and links them against ACE, producing `build/rexx` beside `ace-user-shell` --
which is where it has to be, or `ADDRESS COMMAND` silently does nothing. The
file list is `regina/mmakefile.src`'s `rexx` target verbatim and the version
comes from `regina/regina.ver`, so neither can drift from upstream.

`regina` is deliberately not part of `all`. It builds from `third_party/regina`
by default; `REGINA_SRC=/path/to/aros-contrib/regina` builds against a working
checkout instead, which is how the vendored tree gets refreshed.
`make install-regina` installs `rexx` beside `ace-user-shell` and symlinks it
into `SYS:C`, building first if it needs to. `make clean-regina` removes the
Regina objects and `rexx` and nothing else -- not the vendored source, and not
the ACE objects `rexx` links against.

`-Uunix -U__unix__ -U__unix` is not optional. `mt_notmt.c` picks its
`OS_Dep_funcs` from an `#if/#elif` chain in which the unix branch comes before
the Amiga one, and gcc on a Linux host predefines all three. Without them
Regina links against `__regina_OS_Unx` and you have quietly built the wrong
port -- it compiles, links, and runs. The Makefile no longer trusts the flags
to stay put: the `regina-mt_notmt.o` rule asserts that `__regina_OS_Amiga` is
referenced and `__regina_OS_Unx` is not, and deletes the object if not.

After touching any of it:

```sh
make regina && build/rexx -v   # REXX-Regina_3.5 5.00 31 Dec 2009
git status --short third_party # must be empty
```

## What works today

* `make regina` builds it: all 40 sources of the AROS `rexx` target compile
  and link against ACE, from the Makefile rather than by hand.
* Regina runs: `rexx -v` reports `REXX-Regina_3.5 5.00 31 Dec 2009`, and
  scripts execute -- arithmetic, strings, stems, `parse`.
* `ADDRESS COMMAND 'Echo ...'` works end to end: Regina ->
  `CreateNewProcTags(NP_Entry)` -> host thread -> `StartCommand` -> signal
  handshake -> `SystemTags` -> fork/exec `ace-user-shell` -> output back
  through inherited handles.
* `Status` inside a Rexx script lists `rexx` as a registered ACE process.
* `RC` reports a real command's return code (`Delete` of a missing file gives
  5).
* The whole ARexx message round trip works **inside one process**:
  `FindPort`, `CreateRexxMsg`, `CreateArgstring`, `PutMsg`, `WaitPort`,
  `GetMsg`, `IsRexxMsg`, `ReplyMsg`, and a result argstring back, with the
  reply arriving as the same message that was sent.
* `FindPort` crosses processes: a port one process registers is found by
  another, through the broker's port registry.
* `sendrexxmsg.c` and `listen4msg.c` from the AROS tree compile and link
  **unmodified**. `sendrexxmsg` now gets past `FindPort("REXX")` and blocks at
  `WaitPort`, because delivery is not implemented.
* A process has a **message-delivery channel** to the broker
  (`AMIGA_BROKER_PORT_ATTACH`): a second push connection with its own reader
  thread, opened lazily on first port use and released when the process goes.
  `ace-brokerctl status` reports `ports` and `port-channels` alongside `tasks`.
* **A message crosses processes and the reply comes back**
  (`AMIGA_BROKER_PORT_PUT`, `AMIGA_BROKER_PORT_REPLY`), carrying arbitrary
  bytes intact. What is not built yet is `PutMsg()` using any of it (1.6), the
  sender's streams travelling with the message (1.5), and the broker answering
  for a receiver that dies (1.7) -- until that last one, killing a receiver
  mid-message strands its sender for good.

## The two semantics questions, answered from AmigaOS

Model everything on real ARexx. Both of these were settled by reading the
sources rather than choosing.

**There is no timeout, and there must not be one.** `amifuncs.c:535-545`
wraps the send in `Forbid()`/`Permit()`. That is the Amiga answer to a port
disappearing between the lookup and the send: not a timer, but making the pair
atomic. `WaitPort()` then blocks indefinitely, because the protocol guarantees
a reply -- `RexxMast.c` calls `ReplyMsg()` on every path it takes, including
its error path.

For ACE this means: **one atomic broker operation that finds and sends**, not a
lookup followed by a send, and **the broker guarantees a reply** when the
owning process dies. Those two together replace `Forbid()`, which cannot mean
anything across Linux processes. Do not add timeouts.

**`rm_Stdin` and `rm_Stdout` are the sender's own streams, and they matter.**
The client sets `msg->rm_Stdin = Input(); msg->rm_Stdout = Output();`
(`amifuncs.c:633-634` and `825-826`), and `RexxMast.c:257-260` adopts them as
the script's input and output when they are not `BNULL`. A script sent to
another process therefore runs on *the sender's* console -- which is how
`listen4msg.c` can `Write(msg->rm_Stdin, "Hello\n", 6)` and have it appear on
the sender's screen. On AmigaOS this is free: one address space, so a `BPTR`
FileHandle is valid in any process.

It is not free here, and leaving them `BNULL` is not an option: it is the
mechanism by which `ADDRESS <port>` output reaches the user. Pass the real
descriptors over the broker's unix socket with `SCM_RIGHTS`, which is how the
receiving process gets genuine handles onto the sender's streams.

## Plan

### 1. Deliver and reply across processes

The next thing to build, and the only thing between here and a working
`RexxMast`.

1.1 **Done.** `AMIGA_BROKER_PORT_ATTACH` is a per-process push connection with
its own reader thread, separate from the task channel: `port_fd`,
`port_record_reader()` and `native_broker_port_attach()` in `broker_client.c`,
`attach_port_channel()` and the `port_channels` table in `broker.c`. Points
worth knowing before building on it:

* The channel is keyed by **pid**, not by port. Routing a delivery is
  therefore two hops -- port name to owning pid, pid to channel -- and it is
  the second hop that lets a reply reach a sender which owns no port at all.
* `struct amiga_broker_port_record` is a *header*, with `payload_length`
  bytes following it. The reader drains the payload even for a record it
  cannot use, because the stream is framed by length and a skipped payload
  would be misread as the next header. An oversized length is the one case it
  cannot drain, so it closes the channel.
* `native_broker_port_attach()` is idempotent by design: whichever port call
  is first in a process opens the channel and the rest join it. A second call
  naming a *different* handler is `EBUSY` -- two owners of one stream is a
  bug, not a configuration.
* `native_broker_reset_after_fork()` drops the channel, so a child cannot
  receive messages addressed to its parent.
* A second attach from the same pid replaces the first rather than being
  refused, for the `exec()` case where the old connection's close has not
  been noticed yet.

Tested by `make test-broker-port-channel`, which covers the lifetime: one
channel per process, agreement between the calls that race for it, a channel
each for two processes, and the broker letting go when a process exits. The
reader thread's framing is not yet exercised end to end -- nothing pushes
until 1.2 -- so treat the first PORT_PUT as also being the first real test of
`port_record_reader()`.

1.2 **Done.** `AMIGA_BROKER_PORT_PUT` takes the port **name**, so finding the
owner and handing it the message are one indivisible step -- the `Forbid()`
equivalent, and the reason not to reuse `PORT_FIND`. The broker assigns a
message id, records who sent it, and pushes it to the owner's channel.
`native_broker_port_put()` reports **ESRCH and only ESRCH** when nothing owns
the name, because per 2.5 nobody starts RexxMast automatically and "no such
port" is the ordinary first experience rather than an exceptional one.

1.3 **Done.** `AMIGA_BROKER_PORT_REPLY` takes the message id and routes the
results to the recorded sender's channel.

Two things about the pair worth knowing before building on them:

* **The broker does not believe a caller about who it is.** The sender's and
  replier's pids come from `SO_PEERCRED` on the connection, not from the
  request. `PORT_ADD` and `TASK_ATTACH` do take a pid as text, and that is
  fine -- a process registering itself only harms itself by lying. A message
  is different: the pid decides where a reply is routed, so a caller free to
  name any pid could have another process's replies delivered to it. Only the
  process a message was delivered to may answer it; anything else is `EPERM`.
* **Both ends must have attached a channel first,** senders included, or the
  send is refused with `ENOTCONN` rather than accepted into a wait that could
  never end.

Tested by `make test-broker-port-message`: two processes, a message out and a
reply back, with NUL bytes and bytes above 0x7f in both payloads. That is the
point of the test rather than decoration -- it is what would have failed
before the broker stopped measuring payloads with `strlen()`. It also covers
`ENOTCONN`, `ESRCH` for an unowned name and an unknown id, and `EPERM` for a
sender trying to answer its own message. This is also the first thing to
travel down the channel from 1.1, so it is the first real exercise of that
reader thread.

1.4 Serialise the message as **counted binary, not text**. This step used to
say hex-encode everything, because `broker_request()` measured payloads with
`strlen()`. That has been fixed rather than worked around: the wire format was
counted all along, and both ends now have counted entry points
(`broker_request_bytes()`, `send_response_bytes()`). Exec's `PutMsg()` takes a
Message with an `mn_Length` and never looks inside it; the broker now behaves
the same way. So: no hex, no doubling, and argstrings keep their embedded NULs
natively. Carry `rm_Action`, `rm_Result1`, `rm_Result2`, `rm_Args[0..15]`,
`rm_CommAddr`, `rm_FileExt`. Budget is `AMIGA_BROKER_MAX_PAYLOAD`, 16 KB, now
all of it usable.

Note before raising that cap: `broker_exchange_bytes_locked()` declares two
`char ignored[AMIGA_BROKER_MAX_PAYLOAD]` buffers **on the stack** for draining
unwanted replies. 16 KB each is already substantial; 64 KB would not be
sensible without moving them off the stack first.

1.5 Pass `rm_Stdin`/`rm_Stdout` as descriptors with `SCM_RIGHTS` alongside the
text payload. The receiver turns them into real handles for the script.

1.6 On the sending side, `PutMsg()` recognises a stand-in port via
`ace_aros_runtime_remote_port_id()` and forwards. **The reply must be the same
`struct RexxMsg` the caller sent** -- `sendrexxmsg.c` asserts
`reply == msg` -- so the sender keeps its original, the correlation id brings
the results back, and ACE writes them into that original before putting it on
the reply port. `rm_Result2` arrives as an argstring the sender will
`DeleteArgstring()`, so recreate it locally with `CreateArgstring()`; never
hand back a pointer into broker memory.

1.7 The broker replies on the sender's behalf when the owning process dies,
with a failure result. This is the doc's "deterministic failure when a target
port disappears", and it is what makes 1.4's indefinite wait safe.

1.8 Acceptance: run `listen4msg` and `sendrexxmsg`, unmodified, as two
processes. `sendrexxmsg` must print `Result1:`, get its own message back, and
`listen4msg`'s `Write(msg->rm_Stdin, ...)` must appear on `sendrexxmsg`'s
console. Add it as `tests/rexx_port_test.sh`.

### 2. Port RexxMast

`third_party/regina/rexxmast/RexxMast.c`. Only useful once 1 is
done.

2.1 Create the public `REXX` port and serve it. `RexxMast.c:93` is
`CreatePort("REXX", 0)`, which already works.

2.2 Handle `RXCOMM` with `RXFF_RESULT`: run the script or command string in
`rm_Args[0]`, set `rm_Result1`/`rm_Result2`, and `ReplyMsg()`. Reply on every
path, including failure -- senders wait forever by design.

2.3 Adopt `rm_Stdin`/`rm_Stdout` as the script's streams
(`RexxMast.c:257-260`), which is what 1.5 delivers.

2.4 Then `RXADDLIB`, `RXADDCON`, `RXCLOSE`, and the Regina-private actions.

2.5 Install as `SYS:C/REXXMAST`. **Decided: a user starts it, not the broker.**
On AmigaOS it is started once and lives in the background, so ACE does the
same -- a `Run >NIL: REXXMAST` line in `S/Startup-Sequence`, and no
process-spawning added to the broker. The one departure from silence: a send
to a `REXX` port that nothing owns must come back as a distinct error saying
so, rather than an anonymous `ESRCH`, because "ARexx does nothing and does not
say why" is the worst failure this design can produce. See 1.2.

### 3. Wire ARexx into Regina and the shell

3.1 `ADDRESS <port>` in Regina resolves through `FindPort` and sends a
`RexxMsg` -- the engine code is already there in `amifuncs.c`, and this is
mostly a matter of it now working.

3.2 `ADDRESS COMMAND` already works; check `RC` after it once command-not-found
is fixed (below).

3.3 Run the acceptance scripts the AROS tree ships:
`regina/arexx_test/{addsupport,typepkt,forbid1,forbid2,ptrarith,ados}.rexx`.

### 4. Loose ends worth closing

4.1 **`RC` is 0 for a command that was not found.** A command that runs
publishes its own result and `RC` is right; one that is not found prints
"object not found", records nothing, and reports success. The Shell's local
write to `cli` does not survive the next `Cli()` refill from the broker. Fix it
where the live path detects the failure -- **not** in
`src/aros_shell_runtime.c`, which is dead.

4.2 **Audit `compat/aros-real/include/proto/exec.h`** against
`$HOME/aros/rom/exec/*.c`. Three wrong return types so far, found one at a
time.

4.3 **Done.** `make regina` and `make install-regina`; see "Building it" above.
The build carries the include order, the `-U` flags with an assertion behind
them, and the version defines read out of `regina.ver`. The one thing it does
not do is build the `regina` shared-library target -- only the standalone
`rexx`. `docs/regina-amiga-port.md`'s open question about merging the two
compat trees is still open; the Regina header remains the current answer, not
necessarily the final one.

4.4 `struct ace_gfx_font_choice` is built field by field by its callers, so
adding a member leaves it uninitialised in any caller that misses one. This
already broke `graphics_test.c` once. The same hazard applies to any struct
here that callers fill in by hand.

## Where to stop and test

The numbered steps are not all separable. 1.2 through 1.6 are one mechanism:
a half-built delivery cannot be tested, because a message that goes out and
never comes back just hangs -- by design, since there are no timeouts. These
are the points where something observable actually changes, and each is worth
a commit.

* **After 1.1.** Done. `make test-broker-port-channel`.
* **After 1.2 and 1.3.** Done. `make test-broker-port-message`.
* **After 1.4**, before any I/O: serialise a `RexxMsg` and parse it back in
  one process. Argstrings with embedded NULs and high bytes, all sixteen
  `rm_Args` slots, and one oversized message that must fail cleanly against
  the 16 KB cap rather than truncate. A pure unit test, and the cheapest
  bug-catching in the whole step.
* **After 1.2 + 1.3 + 1.6, deferring 1.5.** Delivery and reply working with
  `rm_Stdin`/`rm_Stdout` left `BNULL`. Test with a purpose-built pair of
  processes rather than `sendrexxmsg`/`listen4msg`: one registers a port and
  replies with a known result, the other checks it got back *the same
  `struct RexxMsg` pointer* with `rm_Result1` set. Prove that pointer identity
  here, because everything downstream assumes it.
* **After 1.5.** Narrowly: the receiver writes a fixed string to `rm_Stdin`
  and it appears on the sender's console. Kept separate from the step above so
  that a failure names the descriptor passing rather than the protocol.
* **After 1.7.** Its own checkpoint, because it is the failure path that makes
  the indefinite wait safe. `SIGKILL` the owner mid-message -- not a clean
  exit -- and confirm the sender's `WaitPort` returns a failure result instead
  of hanging. Then kill it between the find and the send, which is the case
  `Forbid()` existed to prevent.
* **1.8 is the acceptance gate, not a step.** By the time unmodified
  `sendrexxmsg` and `listen4msg` run, it should already work; if it does not,
  the checkpoints above were under-tested.
* **After 2.1 + 2.2.** `RXCOMM` with `RXFF_RESULT` only, `rm_Stdin` still
  unused. Deliberately fail a script and confirm a reply still arrives -- that
  path is easy to leave unwritten and it deadlocks senders forever.
* **After 2.3.** A script sent to `REXX` writes to the sender's console.
* **After 2.5, from the installed location.** Re-run the two checkpoints above
  against `SYS:C/REXXMAST` rather than the build tree. The trap here is the
  same one that makes `rexx` need to sit beside `ace-user-shell`: run it from
  the wrong place and it fails silently while reporting success. Decide
  broker-starts-on-demand versus user-starts *before* installing, because it
  changes what the test has to do.
