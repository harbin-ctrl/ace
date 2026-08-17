#!/usr/bin/env python3
"""Exercise ET through the ACE console byte channel.

The real Amiga reference for this test is tools/amiga-debugcon: the test
doesn't pretend that ACE's host PTY is an Amiga console, but it does make the
ACE-side contract observable at the same raw byte boundary.  A small fake
console answers the public size query and injects one Amiga resize report.
"""

import os
import pty
import select
import signal
import subprocess
import tempfile
import time


CSI = b"\x9b"
SIZE_REPLY = CSI + b"1;1;24;80 r"
RESIZED_REPLY = CSI + b"1;1;20;60 r"
RESIZE_REPORT = CSI + b"12;1;1;0;0;0;0;0|"


def fail(message, output):
    raise AssertionError(f"{message}: {output!r}")


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tine = os.path.join(repo, "tools", "tine", "tine")

    with tempfile.TemporaryDirectory(prefix="ace-tine-screen-") as directory:
        broker_socket = os.path.join(directory, "broker.sock")
        os.makedirs(os.path.join(directory, "C"))
        environment = os.environ.copy()
        environment.update({
            "ACE_SYS_DIR": directory,
            "ACE_BROKER_SOCKET": broker_socket,
        })
        broker = subprocess.Popen(
            [os.path.join(repo, "build", "ace-broker"), broker_socket],
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        for _ in range(100):
            if os.path.exists(broker_socket):
                break
            time.sleep(0.01)
        if not os.path.exists(broker_socket):
            broker.terminate()
            raise AssertionError("screen trace broker did not start")

        pid, master = pty.fork()
        if pid == 0:
            child_environment = environment.copy()
            child_environment.update({
                "ACE_SESSION": "tine-screen-trace-test",
                "TERM": "amiga",
            })
            os.environ.clear()
            os.environ.update(child_environment)
            os.execv(tine, [tine, "-E", "SYS:missing.txt"])

        output = bytearray()
        queries = 0
        input_sent = False
        resize_sent = False
        resize_queries = 0
        quit_sent = False
        child_status = None
        shutdown_marker = (CSI + b"12}" + CSI + b"2}" + CSI + b"10}" +
                           CSI + b"11}" + b"\x1b[0m" + CSI + b" p\n")
        deadline = time.time() + 8
        try:
            while time.time() < deadline:
                readable, _, _ = select.select([master], [], [], 0.05)
                if readable:
                    try:
                        output.extend(os.read(master, 65536))
                    except OSError:
                        break

                new_queries = output.count(CSI + b"0 q")
                if new_queries > queries:
                    for _ in range(new_queries - queries):
                        reply = RESIZED_REPLY if resize_sent else SIZE_REPLY
                        if queries == 0:
                            reply += b"\n"
                        os.write(master, reply)
                    queries = new_queries

                if (not input_sent and CSI + b"12{" in output and
                        b"\f" in output):
                    os.write(master, b"alpha")
                    os.write(master, RESIZE_REPORT)
                    input_sent = True
                    resize_sent = True
                    resize_queries = queries

                if (input_sent and not quit_sent and
                        queries > resize_queries):
                    os.write(master, b"\x1bX\r")
                    quit_sent = True

                if shutdown_marker in output:
                    child_status = os.waitpid(pid, 0)[1]
                    break

                waited, status = os.waitpid(pid, os.WNOHANG)
                if waited:
                    child_status = status
                    break

            if child_status is None:
                os.kill(pid, signal.SIGTERM)
                os.waitpid(pid, 0)
                fail("ET did not exit", output)
            if (not os.WIFEXITED(child_status) or
                    os.WEXITSTATUS(child_status) != 0):
                fail(f"ET exited with status {child_status}", output)
        finally:
            os.close(master)
            if broker.poll() is None:
                broker.send_signal(signal.SIGTERM)
                broker.wait(timeout=2)

        expected_start = CSI + b"12{" + CSI + b"2{" + CSI + b"10{" + CSI + b"11{"
        if output.count(CSI + b"0 q") != 2:
            fail("expected one startup and one resize size query", output)
        if expected_start not in output:
            fail("raw event setup was not emitted", output)
        if not output.startswith(CSI + b"0 q"):
            fail("startup did not begin with the public size query", output)
        if output.count(b"\f") != 1:
            fail("startup did not use one form-feed clear", output)
        if b"\x1b[2J" in output:
            fail("startup emitted the non-Amiga CSI 2J clear", output)
        if (b"\x1b[33m\x1b[24;1H\x1b[1KCreating new file\x1b[31m"
                not in output):
            fail("status text did not use the Ed status pen transition", output)
        if b"\x1b[20;1H" not in output:
            fail("resize did not redraw the new bottom document row", output)
        if CSI + b"12}" not in output:
            fail("resize raw event was not reset on shutdown", output)
        if not output.endswith(shutdown_marker):
            fail("shutdown did not restore the console protocol", output)
        if b"alpha" not in output:
            fail("edited text was not drawn", output)
if __name__ == "__main__":
    main()
