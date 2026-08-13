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
     test-console-device test-exec-compat
```

For an unusual host, override both variables explicitly, for example
`make AROS_ROOT=/opt/aros AROS_CPU_ARCH=aarch64-all -j2 all`.

The patch is required for the AROS console handler to compile without
GadTools, Workbench AppWindow, and completion support. It leaves the original
console editing and history code in place. Apply it once to a clean checkout;
`patch` will report that it is already applied if repeated.

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
compiles selected AROS commands, the AROS shell, and the AROS console handler
against the compatibility headers in `compat/`. The next work is expanding
the DOS filesystem/device seam and porting more AROS commands while keeping
the AROS source behavior intact.
