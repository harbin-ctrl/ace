#!/usr/bin/env python3

import os
import pty
import select
import signal
import socket
import subprocess
import sys
import tempfile
import time


def fail(message, output):
    print(message, file=sys.stderr)
    print(repr(output[-1200:]), file=sys.stderr)
    raise SystemExit(1)


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    tine = os.path.join(repo, "tools", "tine", "tine")
    broker_binary = os.path.join(repo, "build", "ace-broker")

    with tempfile.TemporaryDirectory(prefix="ace-tine-query-") as directory:
        socket_path = os.path.join(directory, "broker.sock")
        environment = os.environ.copy()
        environment.update({
            "ACE_SYS_DIR": directory,
            "ACE_BROKER_SOCKET": socket_path,
        })
        broker = subprocess.Popen(
            [broker_binary, socket_path],
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            for _ in range(100):
                if os.path.exists(socket_path):
                    break
                time.sleep(0.01)
            if not os.path.exists(socket_path):
                fail("broker did not start", b"")

            pid, master = pty.fork()
            if pid == 0:
                child_environment = environment.copy()
                child_environment.update({
                    "ACE_SESSION": "tine-duplicate-query-test",
                    "TERM": "amiga",
                })
                os.environ.update(child_environment)
                os.execv(tine, [tine, "-E", "SYS:missing.txt"])

            output = bytearray()
            reply_sent = False
            quit_sent = False
            deadline = time.time() + 5
            while time.time() < deadline:
                readable, _, _ = select.select([master], [], [], 0.1)
                if not readable:
                    continue
                try:
                    data = os.read(master, 8192)
                except OSError:
                    break
                if not data:
                    break
                output.extend(data)
                if b"\x9b0 q" in output and not reply_sent:
                    # The second reply models a window-size response that
                    # races the initial resize event. The trailing newline
                    # lets a pty's initial canonical mode deliver the bytes;
                    # TINE flushes it when it enters raw mode.
                    os.write(
                        master,
                        b"\x9b1;1;24;80 r\x9b1;1;24;80 r\n",
                    )
                    reply_sent = True
                if (reply_sent and not quit_sent and
                        b"Creating new file" in output):
                    os.write(master, b"\x1bQ\r")
                    quit_sent = True

            if b"Creating new file" not in output:
                fail("missing-file diagnostic was not displayed", output)
            if b"Edits will be lost" in output:
                fail("duplicate size reply became a dirty edit", output)
            if not quit_sent:
                fail("TINE did not reach the extended command prompt", output)
            os.waitpid(pid, 0)
        finally:
            if broker.poll() is None:
                broker.send_signal(signal.SIGTERM)
                broker.wait(timeout=2)


if __name__ == "__main__":
    main()
