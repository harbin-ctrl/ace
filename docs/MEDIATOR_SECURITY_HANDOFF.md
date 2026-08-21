# ACE security mediator redesign handoff

This document is a handoff for a future AI or engineer continuing ACE work.
It records the architectural decision reached in discussion with the user and
the implementation method expected for the next phase. It deliberately
supersedes the older visible root/user and mountview/deviceview model, but it
does not erase the useful implementation work already present in the tree.

Read this document together with the existing `HANDOFF.md`, `README.md`, the
current `Makefile`, and the actual source tree. The source tree is authoritative
for what exists today; this document is authoritative for the intended security
direction.

## Core product idea

ACE is an Amiga-like user interface for a Linux machine. The person using ACE
should not have to understand Linux ownership, root as a login identity, or
mountpoint topology in order to use normal AmigaDOS commands. ACE should feel
like an Amiga environment, while Linux remains visible through the explicit
`LNX` escape hatch.

The shell and broker are user-facing programs and must run as the logged-in
user. They must use that user's desktop session, D-Bus session bus, Wayland or
X11 connection, `HOME`, terminal settings, fonts, colors, and configuration.

ACE must not solve privilege by running the shell, console, or broker as UID 0.
`sudo ace-shell`, `ace-shell --root` in the old sense, and a root-owned GUI are
the wrong architecture. A root process has a different Unix identity and
therefore different desktop/session state; copying the user's environment into
it does not make it the user's D-Bus peer or configuration owner.

The user may still ask ACE to perform operations that require administrator
power. That power is delegated to a privileged mediator and is requested only
when needed.

## The agreed process model

The conceptual model is one mediator service with two isolated security
personalities underneath it:

```text
  ace-shell / ace-console       ordinary user
           |
           v
      ace-broker                ordinary user, owns Amiga/DOS state
           |
           | one authenticated control channel
           v
      ace-mediator supervisor   privileged, no GUI, no Amiga session state
           |
           +-- volume worker     mount/device-view personality
           |
           +-- access workers    one protected DOS operation at a time
```

This is intentionally neither two unrelated public daemons nor one large root
program. There is one launch, authentication, lifecycle, and broker protocol,
but the two dangerous responsibilities are separated internally. The first
implementation may use one root process with strict modules and operation
namespaces, but the protocol and code structure must allow the workers to be
separate processes. Splitting the workers is preferred once the protocol is
stable.

### User shell and console

`ace-shell`, `ace-console`, and all normal ACE commands remain user processes.
The console exports menus and uses the user's session bus. It never contacts a
root D-Bus session and never reads root's configuration.

The shell talks to the broker. It does not need a privileged socket of its own.
This keeps one semantic authority and one privilege ingress.

### User broker

The broker always runs as the logged-in user. It owns only user-local and
Amiga-semantic state, including:

* sessions and connected clients;
* current directories and current devices;
* assigns and assign lists;
* environment variables and temporary files;
* AmigaDOS path parsing and translation;
* command/task/port state that is intentionally broker-backed;
* pending operation IDs and cancellation state.

The broker must not become a root process merely because a client asks for
privileged assistance. It sends a typed request to the mediator and returns the
mediator's result to the DOS compatibility layer.

The broker may retain descriptors, request IDs, and reconnect metadata. “No
state in the mediator” does not mean no state anywhere; Amiga session state
belongs in the broker, while mount/resource state belongs in the volume worker.

### Mediator supervisor

The mediator is a privileged implementation service, not a shell and not a
general root command runner. It has no `HOME`, current directory, user config,
GUI, D-Bus menu, or arbitrary command-string interface.

It exposes a versioned, typed RPC protocol with bounded messages. It must never
accept a request equivalent to “execute this shell command as root.” In
particular, it must not provide a generic root `execve()` operation to ACE
scripts, ARexx, or malformed commands.

The mediator is launched only for a session that has opted into privileged
requests. It may be launched lazily on the first operation requiring it.

### Volume worker

The volume worker owns the privileged device/view responsibilities:

* discover supported block-backed filesystems;
* build and maintain the ACE private mount namespace;
* create ACE-managed bindroots;
* mount supported filesystems on demand;
* unmount/detach them on request and orderly shutdown;
* clean up automatically when the namespace's last holder disappears;
* return stable logical device identifiers and carefully controlled root
  handles.

Its interface must not contain arbitrary file-write, delete, or command-exec
operations. A malformed file operation must not be able to ask it to mount an
arbitrary host path.

The volume worker necessarily holds kernel/resource state: a mount namespace,
mounts, bindroots, open descriptors, and worker lifetime. That is acceptable.
It must not hold Amiga current directories, assigns, environment variables, or
other semantic session state.

### Access workers

An access worker handles a narrowly scoped privileged DOS operation. It may be
one-shot or short-lived. Examples:

* open one exact protected source file for reading;
* open/create one exact protected destination file for writing;
* enumerate one exact protected directory;
* delete one exact path;
* rename one exact pair of paths;
* create one exact directory;
* perform one explicitly supported metadata operation.

For file I/O, prefer opening the object in the access worker and passing the
resulting descriptor back over the broker/mediator channel. The ACE command can
then copy bytes as the ordinary user without receiving general root power.
For unlink, rename, mkdir, and similar operations that cannot be represented by
an fd, the access worker performs exactly that typed operation and returns a
precise status.

The access worker should drop mount-management capabilities. It must not be
able to issue volume-worker operations merely because it is privileged. If the
worker needs to operate in the volume worker's private namespace, arrange this
with a controlled namespace/dirfd handoff or launch the worker in that namespace
with only the capabilities needed for the file operation.

## Meaning of `--root`

`--root` is no longer a request to run ACE as UID 0. It means:

> This ACE session is allowed to request privileged assistance from the
> mediator when ordinary user access is insufficient.

Without `--root`, ACE performs ordinary user operations and reports the
permission failure immediately. With `--root`, the shared DOS layer may retry a
denied operation through the access mediator. Authorization is lazy: starting
ACE with `--root` does not make the shell root and should not necessarily prompt
immediately.

Example:

```text
Copy SYS:foo TO /root/foo
```

The command remains a user process. It copies ordinary files normally. When
opening the protected destination fails with `EACCES` or `EPERM`, the DOS
compatibility layer reports a structured `NEEDS_PRIVILEGE` condition to the
broker. The mediator requests administrator authorization for that operation,
opens the exact object as needed, and the command continues.

The user-facing explanation should be “ACE requests administrator access for
this operation,” not “ACE has switched to root mode.” There is no root/user
title-bar mode.

The old `--user`, `--deviceview`, and `--mountview` switches should not remain
as the normal product model. They may temporarily survive as compatibility or
developer diagnostics while the migration is staged, but they must not define
the final user experience. `--root` is an authorization policy, not a view
selector.

Running the shell or broker while already UID 0 should fail clearly by default:

```text
ACE must be started as a normal user; privileged operations are provided by
the ACE mediator.
```

Do not try to repair `sudo ace-shell` by copying `HOME`, D-Bus, or Wayland
environment variables into the root process. Unix session-bus credentials and
configuration ownership are identity-bound.

## Automatic granular elevation

The intended behavior is automatic at the shared API layer, not in every
command implementation.

Most AROS/Amiga commands use common DOS APIs such as:

* `Open`, `Read`, `Write`, and `Close`;
* `Lock`, `UnLock`, `Examine`, and `ExNext`;
* `DeleteFile`, `Rename`, and `CreateDir`;
* protection/metadata functions;
* the ACE POSIX wrapper used by embedded Vim and related components.

The common layer should attempt the operation as the user first. Only
`EACCES`/`EPERM`-type results should trigger a mediator request. `ENOENT`,
`EROFS`, invalid names, I/O errors, and other ordinary failures must not be
silently converted into root requests.

This lets commands remain unchanged:

```text
command -> DOS compatibility API -> user syscall
                                  -> if denied: broker -> mediator
```

The implementation work is concentrated in the shared DOS/native seam and the
broker protocol. It must not require rewriting every command.

Current ACE source is not perfectly centralized. The migration must audit and
route the direct host calls in `src/native_dos.c`, `src/ace_amiga_posix.c`,
broker-side file operations, and any other shared wrappers. `LNX` is explicitly
outside this model: it is an experimental Linux escape hatch, and a Linux
program launched by `LNX` remains a Linux user process. A user who wants
`sudo bash` may use `LNX sudo bash` once the LNX PTY integration is complete.

Unmodified third-party code that bypasses ACE's DOS/POSIX seam is also outside
automatic per-object elevation. That is desirable; do not add a dangerous
global syscall interception layer just to cover it.

## Authorization and powers

The mediator should use polkit or an equivalent action-based authorization
mechanism for production. `sudo`/`pkexec` may be useful for development or
bootstrap experiments, but they are poor foundations for a descriptor-sensitive
long-lived GUI protocol because they rewrite environments and may not preserve
the required file descriptors.

Use distinct authorization concepts even if `--root` enables both:

* manage/discover/mount ACE device filesystems;
* read protected files;
* modify protected files;
* alter protected metadata, if supported.

### Authorization lifetime: decided, session-scoped

The explicit product decision this section previously asked for has been made:
`--root` is the permission, one authentication turns it into a running
mediator, and that mediator serves the session until the session ends, the
user gives the privilege up through `DROP_PRIVILEGE`, or an optional timeout
expires. The timeout is off by default and exists because it was asked for,
not because ACE imposes it.

Per-object authorization was considered and rejected. A `Copy` of two hundred
files would raise two hundred prompts, and a person shown the same dialog two
hundred times is not making two hundred decisions: they are making one and
then reflexing one hundred and ninety-nine times. Prompt fatigue converts a
security control into a formality, and a formality the user has been trained
to dismiss unread is worse than none, because it also launders the result as
consent. It is equally wrong for the machine being emulated. An Amiga has no
privilege model because the person at the keyboard owns the computer, and an
interface that repeatedly asks that person for permission to use their own
disk feels like a guest account, not an Amiga.

The consequence is recorded in `src/ace_mediator_protocol.h` and must not be
forgotten: with session-scoped authorization the mediator's opcode surface is
the entire security boundary, not one layer of several. Nothing downstream
asks again. That makes the narrow typed interface more load-bearing, not less.
A generic operation added to the mediator would not be a weakness in depth --
in a `--root` session it would be a root shell reachable from any ARexx
script.

### File ownership: decided, and emergent rather than configured

Files created through the mediator are owned by root, as `sudo cp` would leave
them. ACE does not invent an ownership rule, and AmigaDOS has no owner model
to borrow one from.

This is safe only because authorization is not execution as root. The shell,
the commands, and the broker remain the user's own processes; the shared seam
attempts every operation as the user first and reaches the mediator only on a
genuine `EACCES`/`EPERM`. So the session-long authorization does not make
everything the user touches root-owned -- which is precisely what the current
`ace_mode_elevate_if_needed()` re-exec does today, including in the user's own
home. The two decisions depend on each other and must not be separated.

The set of places where root-owned files can therefore appear is exactly the
set of places the user could not have written anyway: `/root`, `/etc`, `/usr`.
There, root-owned is the correct outcome and a user-owned file would be the
anomaly -- and a genuine escalation, since a user-owned file in a system tree
outlives the session as permanent write access obtained from a temporary
lease.

**Do not ever add a list of protected paths.** The boundary is defined by the
kernel's answer to the attempt, not by anything ACE maintains. A configured
list would be redundant when correct and wrong when it drifted, and it would
elevate in places that did not need it. The rule is emergent by design: try as
the user, and let the failure decide.

Commands are talky, so a file that does land root-owned says so when it is
created rather than leaving the user to discover it later when they cannot
edit it.

### Ownerless filesystems: the Amiga model made exact

The one case where root ownership would intrude on the Amiga world proper is
removable media: a stick or card mounted root-owned turns `Copy` into `DH1:`
into root-owned files on the user's own floppy.

FAT, exFAT, and NTFS do not carry ownership either. The kernel invents it at
mount time from `uid=`/`gid=`. The volume worker therefore mounts those
filesystems with `uid=` set to the ACE user, which makes the Amiga model
exactly true rather than approximately true: an ownerless filesystem presented
to a single user who owns all of it is what a floppy was. No mediator
involvement and no ownership concept anywhere in sight.

Filesystems that genuinely carry ownership, and the system directories, keep
Linux semantics and ACE reports them honestly. The illusion is perfect where
the Amiga actually lived and honest where it did not.

## IPC and authentication requirements

Use an inherited Unix `SOCK_SEQPACKET` socketpair or an equally private,
descriptor-based channel whenever possible. Do not use a predictable global
socket as the trust boundary.

The mediator handshake should include:

* protocol magic and version;
* per-launch random instance identifier;
* expected broker PID and process-start identity where available;
* peer credentials (`SO_PEERCRED`/`SCM_CREDENTIALS`);
* bounded maximum message and descriptor counts;
* explicit feature negotiation.

Every request needs a monotonically tracked request ID, an opcode, flags, and a
bounded payload length. Replies must return the request ID, a success/failure
status, a stable ACE error number, and a Linux errno only as supplementary
diagnostic information.

Do not trust a filesystem path merely because it came from the broker. Validate
logical device IDs and relative paths. Reject `..`, unexpected absolute paths,
magic links, and symlink escapes according to the chosen DOS semantics. Use
`openat2()`-style beneath/no-magic-link constraints where available, and use
dirfds/open handles rather than repeatedly resolving untrusted strings.

The mediator must never parse or invoke a shell command. It must never accept an
arbitrary executable path from an ACE script.

## Signals, cancellation, and lifetime

The root workers must not be placed in the terminal's foreground process group.
Signals should be translated into protocol messages:

```text
terminal SIGINT/SIGBREAK
    -> ACE shell
    -> broker BREAK/CANCEL(request_id)
    -> mediator operation worker
```

The broker must not rely on `kill()` to control a root mediator. The mediator
should watch its private channel for EOF/HUP and, where available, monitor a
broker `pidfd`. An orderly shutdown is an authenticated `SHUTDOWN` request.

If the broker dies, the mediator must clean up and exit. If the volume worker
dies, its mount namespace must eventually disappear; the broker reports the
device-view service as unavailable and can request a clean restart. If an
access worker dies, only that operation fails.

Cancellation must be operation-specific. Atomic operations such as rename and
unlink either happen or do not happen. Multi-file copy needs defined behavior
for a partial destination and cancellation.

## Migration plan

The plan below replaces an earlier nine-phase sequence that contained an
ordering error. That sequence placed "make the broker permanently user-owned"
(its Phase 2) before "implement the volume worker" (its Phase 4). The broker's
root-ness is load-bearing for exactly one thing: it creates the private mount
namespace at `broker.c:3928`, mounts the device view into it through
`ace_dos_devices_prepare_device_view()`, and clients `setns()` into *it* from
`broker_client.c`. The broker cannot stop being root while it still owns the
namespace, so de-rooting must follow the volume worker, not precede it. Doing
it the other way round means writing a compatibility path that is discarded
unread.

Two other merges follow from reading the code rather than the design: the
mediator channel and the de-rooting are one change once the volume worker
exists, and the shared-wrapper work and the access opcodes are a single design
because the wrappers and the operations that serve them have to be shaped
together.

Five chunks, in dependency order.

### Chunk A: freeze the contract

Write the protocol and the error semantics before implementing any privilege.

`src/ace_mediator_protocol.h` exists and is the authority: opcodes carrying
their capability class in the high byte so the check is a mask rather than a
table that can drift from the enum beside it; the handshake, nonce, and peer
validation; bounded payloads and descriptor counts; a stable ACE status space
with the host `errno` alongside as diagnostic detail only; and no operation
that executes anything.

Decisions recorded above: session-scoped authorization, root-owned creates in
root-owned places with no configured path list, ownerless filesystems mounted
as the user, `DROP_PRIVILEGE` for deliberate reduction.

Provisional and reversible: the mediator is launched with `pkexec` and
connects *back* to a nonce-named socket under `XDG_RUNTIME_DIR`, because
`pkexec` rewrites the environment and will not carry an inherited socketpair
across the exec. Polkit action names can replace the authorization step later
without changing the protocol.

No behaviour change in this chunk.

### Chunk B: the mediator process and its channel

A privileged `ace-mediator` and the broker-side client. Launch, handshake,
`SO_PEERCRED` and nonce validation in both directions, request ids,
`PING`/`CAPS`/`CANCEL`/`DROP_PRIVILEGE`/`SHUTDOWN`. No privileged operation is
served yet, which is the point: the channel and its lifetime get tested before
anything dangerous rides on them.

Lifetime: the mediator exits on channel EOF, because a broker that crashed
cannot send `SHUTDOWN`. The broker never uses `kill()` on the mediator -- a
user process holding a signal lever on a privileged one is the arrangement
this design exists to avoid.

### Chunk C: the volume worker, then de-root the broker

Move namespace creation, `prepare_device_view()`, and the mount and unmount
lifecycle out of the broker and into the mediator's volume class. Mount
ownerless filesystems with `uid=` per the decision above. Clients `setns()`
into the mediator rather than the broker; the existing peer-uid-0 check and
the `unshare(CLONE_FS)` workaround transfer unchanged, and the reason for that
workaround is recorded below.

Only once the namespace has moved does the broker become an ordinary user
process, and a root broker or shell begin to fail by default.

`make test-device-view` is the regression gate for this chunk and must keep
passing throughout it, not merely at the end.

### Chunk D: access workers and the privileged-aware DOS seam

Shared wrappers for `open`, `stat`, directory enumeration, `unlink`, `rename`,
`mkdir`, and protection changes. Attempt as the user; fall back to the
mediator only on `EACCES`/`EPERM`. `ENOENT`, `EROFS`, invalid names, and I/O
errors must never become privilege requests.

Prefer opening in the worker and passing the descriptor back over the existing
`SCM_RIGHTS` machinery the broker protocol already has, so the privileged part
is the single `open()` that needed it and what returns is a capability to one
object rather than power over others.

Every path is resolved with `openat2()` under `RESOLVE_BENEATH` and
`RESOLVE_NO_MAGICLINKS` against a dirfd the mediator already holds. `..`,
unexpected absolute paths, and symlinks leaving the tree are refused rather
than normalised. `RENAME` carries both paths in one request, because a rename
whose halves were resolved separately is two operations with a window between
them.

Adapt `src/native_dos.c`, `src/ace_amiga_posix.c`, and the broker path seam.
Do not edit commands one at a time.

### Chunk E: policy, hardening, install, documentation

`--root` becomes authorization only. Retire `--user`, `--deviceview`, and
`--mountview` from the normal product path. Test ordinary operations, denied
operations without `--root`, mediator death, broker death, stale channels,
malformed packets, path traversal, symlink escapes, cross-class requests, and
cancellation. Verify the console keeps the user's menu, colours, fonts, D-Bus
session, and configuration while protected operations succeed.

Run the full suite, `make install`, update `README.md`, `HANDOFF.md`, and
`TODO.md`. Do not leave a root GUI or root broker running as a test artifact.

## Existing tree and work discipline

This repository already contains substantial work for the superseded visible
root/device-view model, including `--root`, `--user`, `--deviceview`,
`--mountview`, private bindroots, broker identity fields, and root namespace
joining. Treat those changes as implementation material to refactor, not as the
final architecture.

The following tactical fixes were made during earlier work and may be useful
while migrating:

* multithreaded ACE clients needed `unshare(CLONE_FS)` before joining a mount
  namespace, because Linux otherwise rejected `setns(..., CLONE_NEWNS)` with
  `EINVAL`;
* `Assign` had a tab-parser bug that collapsed empty label fields and exposed
  `/dev/sda2` as an Amiga alias; preserving empty fields fixed it;
* installation dependencies were adjusted so `ace-modes.o` is linked into
  Tine/Vim-related targets.

Do not discard unrelated user work. The working tree has also contained Peek
work (`src/peek.c`, `tests/peek_test.sh`) and other pre-existing changes. Always
inspect `git status` and the relevant diff before editing. Use `apply_patch` for
source/document edits. Avoid destructive resets and broad deletion.

When validating changes:

1. build with the repository's normal Makefile and warning flags;
2. run focused tests for the touched seam;
3. run mode/device/filesystem translation tests during migration;
4. run the full suite before installation when practical;
5. use `make install` only after the build is clean;
6. verify installed binaries are the build just tested;
7. clean only diagnostic processes and files created by the test.

Useful existing focused tests include `test-modes`, `test-device-view`,
`test-filesystem-translation`, `test-system-assigns`, and the broader command
and shell tests listed in the Makefile. New mediator tests should use private
temporary broker/mediator sockets and must always clean up their processes and
mount resources.

## Decisions not to regress

The following user decisions remain in force unless explicitly revisited:

* ACE presents logical Amiga device names, not Linux mountpoint paths;
* Linux absolute symlinks retain Linux meaning and translate to the target ACE
  device when they cross filesystems;
* assigns are created from logical device locations, never raw Linux paths;
* block-backed volumes are the current device-view scope;
* btrfs is currently unsupported;
* procfs/sysfs do not yet have a final model;
* RAM is modeled as one RAM per tmpfs;
* commands are talky and report both success and failure with specific error
  numbers where possible;
* atomicity matters for filesystem operations;
* `LNX` is an explicit Linux escape hatch, not an implicit ACE privilege path;
* authorization is session-scoped: `--root` plus one authentication, until the
  session ends, `DROP_PRIVILEGE`, or an optional timeout that is off by
  default. ACE does not prompt per object, and must not be changed to;
* the shared seam always attempts an operation as the user first, and reaches
  the mediator only on `EACCES`/`EPERM`. This is what keeps session-scoped
  authorization from making everything the user touches root-owned;
* there is no configured list of protected paths, and none may be added. The
  boundary is whatever the kernel refuses;
* files created through the mediator are root-owned, and ACE says so when it
  creates them;
* ownerless filesystems (FAT, exFAT, NTFS) are mounted with `uid=` set to the
  ACE user, because a filesystem with no owners presented to a single user who
  owns all of it is exactly what an Amiga floppy was;
* the mediator has no operation that executes anything, and gaining one would
  turn a `--root` session into a root shell reachable from any ARexx script.

The central security/product decision is now:

> ACE's shell, console, and broker always run as the user. A user who starts
> ACE with `--root` permits the user-owned broker to request narrowly scoped
> privileged operations from a root mediator. The mediator has a volume-view
> personality and a separate protected-operation personality, but it is not a
> root shell and it does not own user state.

