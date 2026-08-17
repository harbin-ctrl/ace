# ACE clipboard system

This document is the implementation plan for bringing the Amiga clipboard
stack into ACE. The target is the AmigaOS/AROS programming model, not just a
Linux copy-and-paste convenience command.

## Scope

The complete subsystem consists of:

1. `clipboard.device`, with units 0 through 255 and the Amiga Exec I/O
   protocol.
2. `iffparse.library`, including its clipboard stream adapter and the IFF
   parser needed by clipboard clients.
3. The `CLIPS:` DOS namespace used by `Clip` for unit existence, deletion,
   counting, and notification.
4. An ACE host bridge connecting unit 0 to the Linux clipboard.
5. `acepaste`, a text-only Linux stdin/stdout adapter for the ACE clipboard.
6. Migration of ACE console and editor copy/paste paths to that common
   device/library path.

The existing AROS `Clip` command is the first compatibility client. The AROS
`Cut` command is not a clipboard client; it extracts characters or words from
a string. Other editors, datatypes, and Workbench-facing clients will be
additional consumers later.

## Architecture

```text
Amiga client
    |
    v
iffparse.library  -- parses and writes IFF streams
    |
    v
clipboard.device  -- raw byte-stream and transaction semantics
    |                         |
    | unit 0                  | units 0..255
    v                         v
Linux text clipboard       CLIPS:clip0..clip255
```

The device should retain the raw IFF representation. A text clipboard is
normally encoded as:

```text
FORM FTXT
    CHRS <text bytes>
```

The host bridge translates that representation to and from Linux plain text.
Keeping translation outside the device preserves unknown IFF chunks and lets
future ACE clients exchange data other than text. An optional text-only
compatibility backend can be added if a host environment cannot retain raw
IFF.

The device layer must not call GTK or another GUI toolkit directly. ACE's
unit-0 bridge uses the host clipboard command available in the session
(`wl-paste`/`wl-copy`, `xclip`, or `xsel`). A deterministic
`ACE_CLIPBOARD_HOST_FILE` backend is available for tests and headless setups.
The device handler's cross-process equivalent is a per-unit `flock()` lock
file plus an atomic temporary-file rename. This is intentionally simple: ACE
is a compatibility connection point, not a latency-critical clipboard
server.

## Implementation stages

### Stage 1 — public compatibility contract and ABI scaffolding

Add the public clipboard and IFF structures, constants, IDs, and function
prototypes based on the AROS headers. Add compile-time ABI coverage for those
headers. This stage must not register a device, change the existing GTK
clipboard path, or claim that clipboard I/O works yet.

### Stage 2 — native `clipboard.device`

Implement `src/clipboard_device.c` and its private header. Model each unit on
AROS's clipboard device:

- read and write clip IDs;
- transactional writes ending at `CMD_UPDATE`;
- snapshot reads with offsets and EOF;
- size probing with `io_Data == NULL`;
- locking and concurrent readers/writers;
- `CBD_POST`, current-ID queries, and change hooks;
- `NSCMD_DEVICEQUERY` and the Amiga error conventions.

Register `clipboard.device` through ACE's Exec runtime and keep its backend
independent of GTK.

### Stage 3 — clipboard-capable `iffparse.library`

Current implementation: the clipboard stream slice is now present in
`src/iffparse_clipboard.c`. It preserves the public Amiga `IFFHandle` layout
while keeping parser state private, and covers `AllocIFF`, `FreeIFF`,
`OpenIFF`, `CloseIFF`, `ParseIFF`, `CurrentChunk`, `ReadChunkBytes`,
`WriteChunkBytes`, `PushChunk`, `PopChunk`, `StopChunk`, `StopChunks`,
`OpenClipboard`, `CloseClipboard`, and `InitIFFasClip`. It reads and writes
big-endian IFF headers, composite FORM/LIST/CAT/PROP chunks, odd-byte padding,
multiple matching chunks, and malformed/truncated streams through the native
clipboard device.

The rest of this stage remains deliberately separate: DOS and buffered stream
handlers, properties, collections, entry/exit handlers, and local-context
items will be added when a client needs them. The current implementation is
therefore sufficient for the planned text clipboard clients, but is not yet a
complete drop-in replacement for every iffparse.library consumer.

Port the clipboard stream path first: `OpenClipboard`, `CloseClipboard`,
`InitIFFasClip`, `OpenIFF`, `CloseIFF`, `ParseIFF`, `StopChunk`, chunk byte
read/write, and chunk push/pop. Then add the broader parser ABI: buffered and
DOS streams, properties, collections, entry/exit handlers, and local-context
items as clients require them.

The parser must honor IFF's big-endian four-byte IDs and lengths, nested
composite forms, unknown chunks, even-byte padding, multiple `CHRS` chunks,
malformed input, and truncated streams.

### Stage 4 — host bridge and `CLIPS:` (implemented)

Unit 0 now synchronizes with the Linux clipboard. Host text is imported as
`FORM FTXT/CHRS`, Amiga text is published back to the host, and an external
host change is detected on the next unit-0 read and becomes a new clipboard
device generation. The normal host commands are discovered in this order:
Wayland (`wl-paste`/`wl-copy`), X11 (`xclip`), then X11 (`xsel`). ACE can use
`ACE_CLIPBOARD_HOST_FILE` instead, which is useful for tests and machines
without a desktop clipboard service.

All 256 units have ACE-owned backing files named `clip0` through `clip255`.
The default directory is `$XDG_RUNTIME_DIR/ace/clips`, the volatile
`T:`-like area already used by ACE; `/tmp/aros` is the fallback. The
`ACE_CLIPBOARD_DIR` environment variable overrides it. `CLIPS:` is assigned
to this directory, so the file-visible behavior used by `Clip`—existence,
deletion, and counting—works across separate ACE command processes. Unit 0
also mirrors its text to Linux. Deleting `CLIPS:0` clears the host text
clipboard when the host backend permits it. Writes use a per-unit lock and
atomic replacement.

The text encoding boundary must be explicit. `CHRS` is a byte stream while
Linux clipboard text is usually UTF-8. ACE currently treats the host bytes as
UTF-8 without recoding them: the bytes are preserved in `CHRS`, and no
silent high-bit conversion is performed. A future locale-aware Amiga charset
policy can be added at this boundary.

### Stage 5 — clients (acepaste slice implemented)

Run the existing AROS `Clip` command unchanged against ACE. Move console
selection/paste and ET copy/paste behind the same clipboard path.

`acepaste` is now the Linux-facing text adapter. It concatenates `CHRS`
chunks from `FORM FTXT` on reads and wraps stdin as `FORM FTXT/CHRS` on
writes. It uses the ACE clipboard device/library path rather than reading
`CLIPS:` files or GTK directly. It defaults to unit 0, accepts `--unit N` or
`-u N`, and selects read mode for a terminal stdin and write mode for piped
stdin; `--get` and `--set` make the choice explicit:

```sh
acepaste > clipboard.txt
cat replacement.txt | acepaste
acepaste --unit 7 --get > alternate.txt
cat alternate.txt | acepaste --unit 7 --set
```

It must not add a newline or separators between chunks. Non-text clipboard
data should produce an error rather than being emitted as opaque bytes.

Add other clipboard-aware commands or datatypes only after the
device/library contract is working.

### Stage 6 — verification

Maintain byte-level golden fixtures and emulator tests for:

- empty, ordinary, odd-length, and non-ASCII text;
- multiple `CHRS` chunks and padding;
- unknown and malformed IFF chunks;
- read/write transactions, offsets, IDs, and EOF;
- multiple units, deletion, `COUNT`, and `WAIT`;
- concurrent access and interrupted waits;
- host-to-Amiga and Amiga-to-host synchronization;
- console and ET copy/paste.

Each implementation increment will be built, installed, tested, committed,
and pushed before the next increment begins.

## AROS reference sources

- `devices/clipboard.h`
- `workbench/devs/clipboard/clipboard.c`
- `libraries/iffparse.h`
- `workbench/libs/iffparse/clipboardfuncs.c`
- `workbench/libs/iffparse/openclipboard.c`
- `workbench/libs/iffparse/initiffasclip.c`
- `workbench/c/shellcommands/Clip.c`

The corresponding local references are under `/home/pi/aros`. ACE's existing
host clipboard calls are currently in
[`src/amiga_console.c`](../src/amiga_console.c), and the future Exec device
registration belongs beside the existing code in
[`src/aros_exec_runtime.c`](../src/aros_exec_runtime.c).
