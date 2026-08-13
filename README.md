# ACE — AROS Command Environment

ACE is a Linux-hosted command environment built from a focused subset of AROS
shell and DOS behavior.
It intentionally does not build Workbench or the AROS kernel.  Original AROS
command sources are compiled against the compatibility headers in
`compat/include` and the broker-backed DOS implementation in `src`.

## Build and run

```sh
make
./build/ace-broker /tmp/ace-broker.sock
```

Install the built commands and console runtime into `/usr/local/bin`:

```sh
make install
```

Use `PREFIX` for a different installation root, for example
`make PREFIX="$HOME/.local" install`. All installed ACE programs share one
directory so they can locate the companion shell, console, broker, and AROS
commands at runtime.

For testing, the project also provides quiet lifecycle commands:

```sh
source ./broker-start
source ./broker-stop
```

When sourced, `broker-start` also prepends `build/` to `PATH`, and
`broker-stop` removes that exact entry. This is necessary because an executed
child script cannot modify its parent shell's environment. Both commands also
work as ordinary executables for broker lifecycle control, but only the
source form changes `PATH`.

Both commands honor `ACE_BROKER_SOCKET`; an optional
`ACE_BROKER_PIDFILE` selects the PID-file location. If the socket
variable is unset, they use the same default as the DOS client.

In another terminal:

```sh
export ACE_BROKER_SOCKET=/tmp/ace-broker.sock
export ACE_SESSION=my-shell
./build/CD .
./build/ace-brokerctl assign WORK: /tmp
./build/MakeDir WORK:ace-test-one WORK:ace-test-two ALL
./build/Echo hello from AROS TO WORK:ace-shell-test
./build/PathPart FILE Work:dir/file.txt
./build/PathPart DIR Work:dir/file.txt
./build/PathPart ADD Work: dir2 file.txt
./build/Fault 205 212
printf 'y\n' | ./build/Ask Continue?
```

The native commands also support AmigaDOS template mode. A standalone `?`
prints the command's argument template and reads the actual arguments from
the next input line:

```sh
printf 'FILE Work:dir/file.txt\n' | ./build/PathPart ?
# DIR/K,FILE/K,ADD/K/M: file.txt
```

Entering another `?` redisplays the template, as in AmigaDOS.

`MakeDir` is the first filesystem-mutating command ported from the original
AROS source. It supports multiple directory names and the `ALL` switch for
creating intermediate directories. Its DOS argument layer now supports
multi-valued `/M` arguments and explicit `FreeArgs()` cleanup.

The broker currently owns per-session current directories and Assigns.  Its
protocol is deliberately small and binary; the DOS shim is the layer where
future path translation, volume labels, mounts, locks, and process state will
be added.

The first local Exec compatibility layer is now in `src/exec_compat.[ch]`.
It provides host-backed memory, registered tasks, Amiga signal-bit masks,
message ports, and case-insensitive library/device registries. Signals are
implemented with mutexes and condition variables rather than Unix signal
numbers. The focused test is run with:

```sh
make test-exec-compat
```

This layer is intentionally local to the runtime; the broker remains the
shared AmigaDOS/session authority. The AROS Shell will use this layer for its
Exec calls and the broker-backed DOS shim for DOS state.

`PathPart` is currently filesystem-independent.  It uses the original AROS
command source and native AmigaDOS-style `FilePart`, `PathPart`, and `AddPart`
implementations, so the paths it prints do not need to exist on Linux.

`Fault` and `Ask` are also built from their original AROS command sources.
`Fault` uses Amiga `/N/M` numeric-list argument handling, and `Ask` uses the
native console stream plus the `utility.library` compatibility surface.

The broker's first CLI-state draft provides session-local and broker-global
variables through `GetVar`, `SetVar`, and `DeleteVar`, plus a per-session
return-code/result2 record. `ace-brokerctl` can exercise these directly:

```sh
./build/ace-brokerctl setvar LOCAL value
./build/ace-brokerctl setgvar GLOBAL value
./build/ace-brokerctl getvar LOCAL
./build/ace-brokerctl setresult 10 205
./build/ace-brokerctl result
```

The original AROS `Set`, `Unset`, `Alias`, and `Unalias` commands are now
also built. Their local-variable lists are backed by the broker, and aliases
are kept separate from ordinary variables, as they are in AmigaDOS:

```sh
Set SCORE 5
Set
Alias HI Echo []
Alias
Get SCORE
Unalias HI
Unset SCORE
```

Alias expansion by the future shell command dispatcher is not implemented
yet; these commands currently provide the AmigaDOS alias state and its
inspection/update behavior.

The first CLI-lifecycle commands are also available. `FailAt` and `Prompt`
update broker-owned CLI state, while `Why` reads the previous command's
secondary result:

```sh
FailAt
FailAt 5
Prompt 'AMIGA> '
Get MISSING
Why
./build/ace-brokerctl cli
```

The diagnostic `cli` output is four lines: return code, `Result2`, fail
level, and prompt. A real interactive AmigaDOS-compatible command dispatcher
will consume this state in the next shell layer.

The first classic-console shell slice is now available. It uses a menu-free
GTK drawing surface as the Linux console, a full-duplex Unix stream for the
child CLI, and a cloned broker session. The surface is gray by default
and renders a first classic terminal subset: Amiga/ANSI CSI cursor movement,
colors, text attributes, erasing, tabs, scrolling, and local line editing.

The window now reaches that renderer through a small AROS-shaped
`console.device` seam in `src/console_device.[ch]`. It provides
`OpenDevice`, `CloseDevice`, `CMD_READ`, and `CMD_WRITE`, with asynchronous
`SendIO`, `WaitIO`, and `AbortIO` request handling. The device has a blocking
input queue and the GTK window's keyboard path uses it.

`src/con_handler.[ch]` adds the first menu-free classic `CON:` handler layer:
it owns cooked/raw input state, persistent line buffering, and the handler's
device-read/write path. The bootstrap shell is reached through a socket stream
and that stream is fed through the handler rather than a PTY. AROS Workbench,
menus, clipboard, and packet/task ABI remain deliberately outside this
profile.

```sh
source ./broker-start
export ACE_SESSION=main-shell
./build/ace-shell
```

`ace-shell` can also bootstrap the broker itself.  It starts the sibling
`ace-broker` only when the configured `ACE_BROKER_SOCKET` is unreachable,
waits for it to accept connections, and reuses an existing broker.  The
startup lock prevents concurrent shells from starting duplicates.  Set
`ACE_BROKER_BINARY` only when the broker is not beside `ace-shell`.

Inside that shell, command names are case-insensitive and AROS commands are
preferred over the Bash fallback. `NewCLI` opens a separate window and starts
another shell with a cloned initial session:

```text
AMIGA> SET FOO parent
AMIGA> NEWCLI
AMIGA> ENDSHELL
```

`build/NewCLI` is compiled from the original AROS `NewCLI.c` (which includes
`NewShell.c`).  Its unchanged `Open("CON:...")` and `SystemTagList()` calls
are currently backed by the host compatibility layer; the compatibility
layer launches the existing ACE console and clones the broker session.

This window and dispatcher are the host bootstrap layer. They establish the
classic window/session behavior while the real AROS Shell and `CON:` handler
are being ported behind the same interfaces. The GTK surface deliberately
has no AROS menus, Workbench integration, or clipboard extensions.

Global variables currently last only for the lifetime of the broker. The
`SAVE`/`ENVARC:` persistence behavior is still reserved for a later draft.
Every command built through the native AROS shell-command wrapper now
publishes its return code and `IoErr()` as the session's result record.
