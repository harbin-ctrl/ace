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
5. Migration of ACE console and editor copy/paste paths to that common
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
    |
    v
ACE clipboard manager
    |                         |
    v                         v
Linux text clipboard       CLIPS: units 0..255
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

The device layer must not call GTK or another GUI toolkit directly. Host
clipboard ownership belongs to the ACE GUI/broker bridge, with IPC if the
device-serving process is headless.

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

Port the clipboard stream path first: `OpenClipboard`, `CloseClipboard`,
`InitIFFasClip`, `OpenIFF`, `CloseIFF`, `ParseIFF`, `StopChunk`, chunk byte
read/write, and chunk push/pop. Then add the broader parser ABI: buffered and
DOS streams, properties, collections, entry/exit handlers, and local-context
items as clients require them.

The parser must honor IFF's big-endian four-byte IDs and lengths, nested
composite forms, unknown chunks, even-byte padding, multiple `CHRS` chunks,
malformed input, and truncated streams.

### Stage 4 — host bridge and `CLIPS:`

Make unit 0 synchronize with the Linux clipboard. Import host text as
`FORM FTXT/CHRS`, publish Amiga text back to Linux, detect external clipboard
changes, and generate the corresponding post/change notifications.

Provide an ACE-owned `CLIPS:` backing namespace for units 0 through 255. It
must support the file-visible behavior used by `Clip`: existence, deletion,
counting, and notification. Writes should be committed atomically.

The text encoding boundary must be explicit. `CHRS` is a byte stream while
Linux clipboard text is usually UTF-8; the initial implementation should
preserve raw IFF bytes and document the chosen Amiga-byte-to-UTF-8 policy
rather than silently corrupting high-bit data.

### Stage 5 — clients

Run the existing AROS `Clip` command unchanged against ACE. Move console
selection/paste and ET copy/paste behind the same clipboard path. Add other
clipboard-aware commands or datatypes only after the device/library contract
is working.

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
