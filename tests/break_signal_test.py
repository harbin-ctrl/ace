#!/usr/bin/env python3
"""Prove that a console break sent to the shell reaches its busy child."""

import os
import pathlib
import select
import signal
import subprocess
import sys
import tempfile
import time


def fail(message, output=b""):
    sys.stderr.write(message + "\n")
    if output:
        sys.stderr.buffer.write(output)
    raise SystemExit(1)


def main():
    repo = pathlib.Path(__file__).resolve().parent.parent
    shell = repo / "build" / "ace-user-shell"
    command = f"rootfs:{repo.as_posix()[1:]}/build/break-probe\nEndCLI\n"
    child = subprocess.Popen(
        [str(shell)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, env=os.environ.copy())
    child.stdin.write(command.encode())
    child.stdin.flush()
    ready = b""
    deadline = time.monotonic() + 5
    while b"break-probe: ready\n" not in ready and time.monotonic() < deadline:
        readable, _, _ = select.select([child.stdout], [], [], 0.1)
        if readable:
            ready += os.read(child.stdout.fileno(), 4096)
        if child.poll() is not None:
            fail("foreground command exited before Ctrl-C", ready)
    if b"break-probe: ready\n" not in ready:
        child.kill()
        output, _ = child.communicate()
        fail("foreground command did not become ready", ready + output)
    os.kill(child.pid, signal.SIGUSR1)
    try:
        output, _ = child.communicate(timeout=8)
    except subprocess.TimeoutExpired:
        child.kill()
        output, _ = child.communicate()
        fail("Ctrl-C did not stop the foreground command", ready + output)
    if child.returncode != 0:
        fail(f"shell exited with {child.returncode}", ready + output)
    if b"break-probe: SIGBREAKF_CTRL_C received" not in ready + output:
        fail("foreground command did not receive SIGBREAKF_CTRL_C", ready + output)

    # Ctrl-D belongs to the shell.  During a script it must allow the current
    # command to complete, then stop before the following line is executed.
    script = tempfile.NamedTemporaryFile(prefix="ace-ctrl-d-", dir=repo,
                                         delete=False, mode="w")
    try:
        amiga_script = f"rootfs:{pathlib.Path(script.name).as_posix()[1:]}"
        script.write(f"rootfs:{repo.as_posix()[1:]}/build/break-probe short\n")
        script.write("Echo SHOULD-NOT-APPEAR\n")
        script.close()
        environment = os.environ.copy()
        environment["ACE_STARTUP_SCRIPT"] = amiga_script
        child = subprocess.Popen(
            [str(shell)], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, env=environment)
        ready = b""
        deadline = time.monotonic() + 5
        while b"break-probe: ready\n" not in ready and time.monotonic() < deadline:
            readable, _, _ = select.select([child.stdout], [], [], 0.1)
            if readable:
                ready += os.read(child.stdout.fileno(), 4096)
        if b"break-probe: ready\n" not in ready:
            child.kill()
            output, _ = child.communicate()
            fail("script command did not become ready", ready + output)
        os.kill(child.pid, signal.SIGUSR2)
        output, _ = child.communicate(timeout=8)
        output = ready + output
        if child.returncode != 0:
            fail(f"script shell exited with {child.returncode}", output)
        if b"Shell: ***Break" not in output:
            fail("shell did not report Ctrl-D script break", output)
        if b"SHOULD-NOT-APPEAR" in output:
            fail("Ctrl-D did not stop the remaining script", output)
    finally:
        script.close()
        os.unlink(script.name)


if __name__ == "__main__":
    main()
