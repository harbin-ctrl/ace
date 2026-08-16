DBGCON — Amiga console byte-stream tracer

DBGCON runs a command while temporarily patching the real AmigaOS
console.device BeginIO vector.  CMD_WRITE requests are copied verbatim to a
trace file and then passed to the original console.device implementation.
This leaves CON:, the normal CON handler, and console.device in the rendering
path, so the trace is useful for comparing ED and ET at the terminal-byte
level.

The program is intended to be compiled for 68K AmigaOS with VBCC and the
AmigaOS NDK headers.  It is not a replacement CON: handler and does not
install a permanent system patch.

Usage on AmigaOS:

    DBGCON Work:trace.bin ED Work:test.txt WINDOW CONSOLE:

The trace file is replaced on each run.  Command arguments are reconstructed
with single spaces, so shell metacharacters and quoted arguments should be
avoided in the first version of the probe.  The target inherits DBGCON's
standard CLI input and output.

The source is kept in the repository, while VBCC and the NDK remain outside
the repository because they are third-party Amiga development packages.
