#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <devices/clipboard.h>
#include <libraries/iffparse.h>
#include <proto/iffparse.h>

#include "aros_exec_runtime.h"

#define ACE_IFFF_OPEN (1L << 19)

struct ace_iff_chunk {
    struct ContextNode public_chunk;
    size_t data_offset;
    size_t depth;
    int composite;
};

struct ace_iff_event {
    size_t chunk_index;
    int entering;
};

struct ace_iff_stop {
    LONG type;
    LONG id;
};

struct ace_iff_write_chunk {
    struct ContextNode public_chunk;
    size_t header_offset;
    int composite;
    int known_size;
};

struct ace_iff_handle {
    struct IFFHandle public_handle;

    struct ClipboardHandle *clipboard;
    UBYTE *input;
    size_t input_size;
    struct ace_iff_chunk *chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    struct ace_iff_event *events;
    size_t event_count;
    size_t event_capacity;
    struct ace_iff_stop *stops;
    size_t stop_count;
    size_t stop_capacity;
    size_t scan_cursor;
    size_t event_cursor;
    size_t current_chunk;
    int current_valid;

    struct ace_iff_write_chunk *write_stack;
    size_t write_depth;
    size_t write_capacity;
    size_t write_offset;
    LONG write_clip_id;
    int write_started;
    int initialized;
    int open;
};

#define ACE_IFF_HANDLE(iff) \
    ((struct ace_iff_handle *)(iff))

static ULONG read_be32(const UBYTE *bytes)
{
    return ((ULONG)bytes[0] << 24) |
           ((ULONG)bytes[1] << 16) |
           ((ULONG)bytes[2] << 8) |
           (ULONG)bytes[3];
}

static void write_be32(UBYTE *bytes, ULONG value)
{
    bytes[0] = (UBYTE)(value >> 24);
    bytes[1] = (UBYTE)(value >> 16);
    bytes[2] = (UBYTE)(value >> 8);
    bytes[3] = (UBYTE)value;
}

static int is_composite(ULONG id)
{
    return id == ID_FORM || id == ID_LIST || id == ID_CAT || id == ID_PROP;
}

static int grow_array(void **array, size_t *capacity, size_t element_size,
                      size_t needed)
{
    size_t new_capacity;
    void *grown;

    if (needed <= *capacity)
        return 0;
    new_capacity = *capacity ? *capacity : 8;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2)
            return -1;
        new_capacity *= 2;
    }
    if (element_size > SIZE_MAX / new_capacity)
        return -1;
    grown = realloc(*array, new_capacity * element_size);
    if (!grown)
        return -1;
    *array = grown;
    *capacity = new_capacity;
    return 0;
}

static void clear_read_state(struct ace_iff_handle *handle)
{
    free(handle->input);
    free(handle->chunks);
    free(handle->events);
    handle->input = NULL;
    handle->chunks = NULL;
    handle->events = NULL;
    handle->input_size = 0;
    handle->chunk_count = 0;
    handle->chunk_capacity = 0;
    handle->event_count = 0;
    handle->event_capacity = 0;
    handle->scan_cursor = 0;
    handle->event_cursor = 0;
    handle->current_chunk = 0;
    handle->current_valid = 0;
}

static LONG append_chunk(struct ace_iff_handle *handle, ULONG id, ULONG type,
                         ULONG size, size_t data_offset, size_t depth,
                         int composite, size_t *index)
{
    struct ace_iff_chunk *chunk;

    if (grow_array((void **)&handle->chunks, &handle->chunk_capacity,
                   sizeof(*handle->chunks), handle->chunk_count + 1) != 0)
        return IFFERR_NOMEM;
    chunk = &handle->chunks[handle->chunk_count];
    memset(chunk, 0, sizeof(*chunk));
    chunk->public_chunk.cn_ID = (LONG)id;
    chunk->public_chunk.cn_Type = (LONG)type;
    chunk->public_chunk.cn_Size = (LONG)size;
    chunk->data_offset = data_offset;
    chunk->depth = depth;
    chunk->composite = composite;
    *index = handle->chunk_count++;
    return 0;
}

static LONG append_event(struct ace_iff_handle *handle, size_t chunk_index,
                         int entering)
{
    struct ace_iff_event *event;

    if (grow_array((void **)&handle->events, &handle->event_capacity,
                   sizeof(*handle->events), handle->event_count + 1) != 0)
        return IFFERR_NOMEM;
    event = &handle->events[handle->event_count++];
    event->chunk_index = chunk_index;
    event->entering = entering;
    return 0;
}

static LONG parse_chunk(struct ace_iff_handle *handle, size_t offset,
                        size_t limit, ULONG parent_type, size_t depth,
                        size_t *next_offset)
{
    ULONG id;
    ULONG size;
    ULONG type = parent_type;
    size_t payload_offset;
    size_t end_offset;
    size_t aligned_end;
    size_t chunk_index;
    LONG error;

    if (offset > limit || limit - offset < 8)
        return IFFERR_MANGLED;
    id = read_be32(handle->input + offset);
    size = read_be32(handle->input + offset + 4);
    payload_offset = offset + 8;
    if ((size_t)size > limit - payload_offset)
        return IFFERR_MANGLED;
    end_offset = payload_offset + (size_t)size;
    aligned_end = end_offset + (size & 1U);
    if (aligned_end > limit)
        return IFFERR_MANGLED;

    if (is_composite(id)) {
        if (size < 4)
            return IFFERR_MANGLED;
        type = read_be32(handle->input + payload_offset);
        error = append_chunk(handle, id, type, size, payload_offset + 4,
                             depth, 1, &chunk_index);
        if (error)
            return error;
        error = append_event(handle, chunk_index, 1);
        if (error)
            return error;
        payload_offset += 4;
        while (payload_offset < end_offset) {
            size_t child_end;

            error = parse_chunk(handle, payload_offset, end_offset, type,
                                depth + 1, &child_end);
            if (error)
                return error;
            payload_offset = child_end;
        }
        if (payload_offset != end_offset)
            return IFFERR_MANGLED;
        error = append_event(handle, chunk_index, 0);
        if (error)
            return error;
    } else {
        error = append_chunk(handle, id, type, size, payload_offset, depth,
                             0, &chunk_index);
        if (error)
            return error;
        error = append_event(handle, chunk_index, 1);
        if (error)
            return error;
        error = append_event(handle, chunk_index, 0);
        if (error)
            return error;
    }
    *next_offset = aligned_end;
    return 0;
}

static LONG load_clipboard(struct ace_iff_handle *handle)
{
    struct IOClipReq *request;
    ULONG size;
    size_t next_offset;
    LONG error;

    request = &handle->clipboard->cbh_Req;
    request->io_Command = CMD_READ;
    request->io_Data = NULL;
    request->io_Length = UINT32_MAX;
    request->io_Offset = 0;
    request->io_ClipID = 0;
    error = DoIO((struct IORequest *)request);
    if (error)
        return IFFERR_READ;
    size = request->io_Actual;
    if (size == 0)
        return IFFERR_NOTIFF;

    handle->input = malloc(size);
    if (!handle->input)
        return IFFERR_NOMEM;
    handle->input_size = size;

    request->io_Command = CMD_READ;
    request->io_Data = (STRPTR)handle->input;
    request->io_Length = size;
    request->io_Offset = 0;
    request->io_ClipID = 0;
    error = DoIO((struct IORequest *)request);
    if (error || request->io_Actual != size) {
        clear_read_state(handle);
        return IFFERR_READ;
    }
    if (!is_composite(read_be32(handle->input)))
        return IFFERR_NOTIFF;
    error = parse_chunk(handle, 0, handle->input_size, 0, 0,
                        &next_offset);
    if (error)
        return error;
    if (next_offset != handle->input_size)
        return IFFERR_MANGLED;
    return 0;
}

static LONG write_at(struct ace_iff_handle *handle, const void *data,
                     size_t length, size_t offset, int advance)
{
    struct IOClipReq *request;
    LONG error;

    if (offset > UINT32_MAX || length > UINT32_MAX ||
        offset > UINT32_MAX - length)
        return IFFERR_WRITE;
    request = &handle->clipboard->cbh_Req;
    request->io_Command = CMD_WRITE;
    request->io_Data = (STRPTR)data;
    request->io_Length = (ULONG)length;
    request->io_Offset = (ULONG)offset;
    request->io_ClipID = handle->write_clip_id;
    error = DoIO((struct IORequest *)request);
    if (error || request->io_Actual != length)
        return IFFERR_WRITE;
    handle->write_clip_id = request->io_ClipID;
    handle->write_started = 1;
    if (advance)
        handle->write_offset = offset + length;
    return 0;
}

static LONG write_stream(struct ace_iff_handle *handle, const void *data,
                         size_t length)
{
    return write_at(handle, data, length, handle->write_offset, 1);
}

static LONG update_clipboard(struct ace_iff_handle *handle)
{
    struct IOClipReq *request;

    if (!handle->write_started)
        return 0;
    request = &handle->clipboard->cbh_Req;
    request->io_Command = CMD_UPDATE;
    request->io_Data = NULL;
    request->io_Length = 0;
    request->io_ClipID = handle->write_clip_id;
    if (DoIO((struct IORequest *)request) != 0)
        return IFFERR_WRITE;
    handle->write_clip_id = -1;
    handle->write_started = 0;
    return 0;
}

struct IFFHandle *AllocIFF(void)
{
    struct ace_iff_handle *handle = calloc(1, sizeof(*handle));

    if (!handle)
        return NULL;
    handle->public_handle.iff_Flags = IFFF_READ;
    handle->current_chunk = 0;
    handle->initialized = 1;
    return &handle->public_handle;
}

void FreeIFF(struct IFFHandle *iff)
{
    struct ace_iff_handle *handle;

    if (!iff)
        return;
    handle = ACE_IFF_HANDLE(iff);
    if (handle->open)
        CloseIFF(iff);
    clear_read_state(handle);
    free(handle->stops);
    free(handle->write_stack);
    free(handle);
}

void InitIFFasClip(struct IFFHandle *iff)
{
    struct ace_iff_handle *handle;

    if (!iff)
        return;
    handle = ACE_IFF_HANDLE(iff);
    iff->iff_Flags |= IFFF_RSEEK;
    handle->initialized = 1;
}

struct ClipboardHandle *OpenClipboard(LONG unit_number)
{
    struct ClipboardHandle *clipboard;

    if (unit_number < 0 || unit_number > 255)
        return NULL;
    clipboard = calloc(1, sizeof(*clipboard));
    if (!clipboard)
        return NULL;
    clipboard->cbh_Req.io_Message.mn_Length = sizeof(clipboard->cbh_Req);
    if (OpenDevice("clipboard.device", (ULONG)unit_number,
                   (struct IORequest *)&clipboard->cbh_Req, 0) != 0) {
        free(clipboard);
        return NULL;
    }
    return clipboard;
}

void CloseClipboard(struct ClipboardHandle *clipboard)
{
    if (!clipboard)
        return;
    CloseDevice((struct IORequest *)&clipboard->cbh_Req);
    free(clipboard);
}

LONG OpenIFF(struct IFFHandle *iff, LONG rw_mode)
{
    struct ace_iff_handle *handle;
    LONG error;

    if (!iff)
        return IFFERR_NOMEM;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->initialized || handle->open ||
        (rw_mode != IFFF_READ && rw_mode != IFFF_WRITE) || !iff->iff_Stream)
        return IFFERR_SYNTAX;
    handle->clipboard = (struct ClipboardHandle *)(uintptr_t)iff->iff_Stream;
    if (!handle->clipboard)
        return IFFERR_NOSCOPE;
    clear_read_state(handle);
    free(handle->write_stack);
    handle->write_stack = NULL;
    handle->write_depth = 0;
    handle->write_capacity = 0;
    handle->write_offset = 0;
    handle->write_clip_id = 0;
    handle->write_started = 0;
    handle->open = 1;
    iff->iff_Depth = 0;
    iff->iff_Flags = (iff->iff_Flags & ~(IFFF_RWBITS | ACE_IFFF_OPEN)) |
                     rw_mode | ACE_IFFF_OPEN;
    if (rw_mode == IFFF_READ) {
        error = load_clipboard(handle);
        if (error) {
            handle->open = 0;
            iff->iff_Flags &= ~ACE_IFFF_OPEN;
            return error;
        }
    }
    return 0;
}

void CloseIFF(struct IFFHandle *iff)
{
    struct ace_iff_handle *handle;

    if (!iff)
        return;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->open)
        return;
    if (iff->iff_Flags & IFFF_WRITE) {
        while (handle->write_depth)
            if (PopChunk(iff) != 0)
                break;
        (void)update_clipboard(handle);
    }
    handle->open = 0;
    iff->iff_Flags &= ~ACE_IFFF_OPEN;
    iff->iff_Depth = 0;
}

struct ContextNode *CurrentChunk(struct IFFHandle *iff)
{
    struct ace_iff_handle *handle;

    if (!iff)
        return NULL;
    handle = ACE_IFF_HANDLE(iff);
    if (iff->iff_Flags & IFFF_WRITE) {
        if (!handle->write_depth)
            return NULL;
        return &handle->write_stack[handle->write_depth - 1].public_chunk;
    }
    if (!handle->current_valid || handle->current_chunk >= handle->chunk_count)
        return NULL;
    return &handle->chunks[handle->current_chunk].public_chunk;
}

LONG ReadChunkBytes(struct IFFHandle *iff, APTR buffer, LONG bytes)
{
    struct ace_iff_handle *handle;
    struct ace_iff_chunk *chunk;
    size_t remaining;
    size_t amount;

    if (!iff || bytes < 0)
        return IFFERR_READ;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->open || (iff->iff_Flags & IFFF_WRITE) ||
        !handle->current_valid)
        return IFFERR_NOSCOPE;
    chunk = &handle->chunks[handle->current_chunk];
    if ((ULONG)chunk->public_chunk.cn_Scan >
        (ULONG)chunk->public_chunk.cn_Size)
        return IFFERR_READ;
    remaining = (size_t)chunk->public_chunk.cn_Size -
                (size_t)chunk->public_chunk.cn_Scan;
    amount = (size_t)bytes < remaining ? (size_t)bytes : remaining;
    if (amount && !buffer)
        return IFFERR_READ;
    if (amount)
        memcpy(buffer, handle->input + chunk->data_offset +
                         chunk->public_chunk.cn_Scan, amount);
    chunk->public_chunk.cn_Scan += (LONG)amount;
    return (LONG)amount;
}

LONG WriteChunkBytes(struct IFFHandle *iff, APTR buffer, LONG bytes)
{
    struct ace_iff_handle *handle;
    struct ace_iff_write_chunk *chunk;
    size_t amount;
    LONG error;

    if (!iff || bytes < 0)
        return IFFERR_WRITE;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->open || !(iff->iff_Flags & IFFF_WRITE) ||
        !handle->write_depth)
        return IFFERR_NOSCOPE;
    chunk = &handle->write_stack[handle->write_depth - 1];
    amount = (size_t)bytes;
    if (chunk->known_size) {
        size_t remaining = (size_t)chunk->public_chunk.cn_Size -
                           (size_t)chunk->public_chunk.cn_Scan;
        if (amount > remaining)
            amount = remaining;
    }
    if (amount && !buffer)
        return IFFERR_WRITE;
    error = write_stream(handle, buffer, amount);
    if (error)
        return error;
    chunk->public_chunk.cn_Scan += (LONG)amount;
    return (LONG)amount;
}

LONG PushChunk(struct IFFHandle *iff, LONG type, LONG id, LONG size)
{
    struct ace_iff_handle *handle;
    struct ace_iff_write_chunk *chunk;
    UBYTE header[12];
    int composite;
    LONG error;

    if (!iff)
        return IFFERR_NOSCOPE;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->open || !(iff->iff_Flags & IFFF_WRITE))
        return IFFERR_SYNTAX;
    composite = is_composite((ULONG)id);
    if (!handle->write_depth && id != ID_FORM && id != ID_LIST &&
        id != ID_CAT)
        return IFFERR_NOTIFF;
    if (handle->write_depth && !composite &&
        handle->write_stack[handle->write_depth - 1].public_chunk.cn_ID !=
            ID_FORM &&
        handle->write_stack[handle->write_depth - 1].public_chunk.cn_ID !=
            ID_PROP)
        return IFFERR_SYNTAX;
    if (composite && type == ID_NULL)
        return IFFERR_NOTIFF;
    if (id == ID_PROP && (!handle->write_depth ||
                          handle->write_stack[handle->write_depth - 1]
                              .public_chunk.cn_ID != ID_LIST))
        return IFFERR_SYNTAX;
    if (size < IFFSIZE_UNKNOWN)
        return IFFERR_SYNTAX;
    if (grow_array((void **)&handle->write_stack, &handle->write_capacity,
                   sizeof(*handle->write_stack), handle->write_depth + 1) != 0)
        return IFFERR_NOMEM;

    write_be32(header, (ULONG)id);
    write_be32(header + 4, size == IFFSIZE_UNKNOWN ? 0 : (ULONG)size);
    if (composite)
        write_be32(header + 8, (ULONG)type);
    error = write_stream(handle, header, composite ? 12 : 8);
    if (error)
        return error;
    chunk = &handle->write_stack[handle->write_depth++];
    memset(chunk, 0, sizeof(*chunk));
    chunk->public_chunk.cn_ID = id;
    chunk->public_chunk.cn_Type = type;
    chunk->public_chunk.cn_Size = size;
    chunk->header_offset = handle->write_offset - (composite ? 12 : 8);
    chunk->composite = composite;
    chunk->known_size = size != IFFSIZE_UNKNOWN;
    if (composite) {
        chunk->public_chunk.cn_Scan = 4;
    }
    iff->iff_Depth = (LONG)handle->write_depth;
    return 0;
}

LONG PopChunk(struct IFFHandle *iff)
{
    struct ace_iff_handle *handle;
    struct ace_iff_write_chunk *chunk;
    ULONG size;
    UBYTE pad = 0;
    LONG error;

    if (!iff)
        return IFFERR_NOSCOPE;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->open || !(iff->iff_Flags & IFFF_WRITE) ||
        !handle->write_depth)
        return IFFERR_NOSCOPE;
    chunk = &handle->write_stack[handle->write_depth - 1];
    if (chunk->known_size) {
        if (chunk->public_chunk.cn_Scan > chunk->public_chunk.cn_Size)
            return IFFERR_WRITE;
        size = (ULONG)chunk->public_chunk.cn_Size;
    } else {
        size = (ULONG)chunk->public_chunk.cn_Scan;
        {
            UBYTE encoded[4];

            write_be32(encoded, size);
            error = write_at(handle, encoded, sizeof(encoded),
                             chunk->header_offset + 4, 0);
        }
        if (error)
            return error;
    }
    if (size & 1U) {
        error = write_stream(handle, &pad, 1);
        if (error)
            return error;
    }
    handle->write_depth--;
    if (handle->write_depth) {
        struct ace_iff_write_chunk *parent =
            &handle->write_stack[handle->write_depth - 1];
        parent->public_chunk.cn_Scan += 8 + size + (size & 1U);
    }
    iff->iff_Depth = (LONG)handle->write_depth;
    return 0;
}

LONG StopChunk(struct IFFHandle *iff, LONG type, LONG id)
{
    struct ace_iff_handle *handle;

    if (!iff)
        return IFFERR_NOSCOPE;
    handle = ACE_IFF_HANDLE(iff);
    if (grow_array((void **)&handle->stops, &handle->stop_capacity,
                   sizeof(*handle->stops), handle->stop_count + 1) != 0)
        return IFFERR_NOMEM;
    handle->stops[handle->stop_count].type = type;
    handle->stops[handle->stop_count].id = id;
    handle->stop_count++;
    return 0;
}

LONG StopChunks(struct IFFHandle *iff, const LONG *pairs, LONG count)
{
    LONG index;
    LONG error;

    if (!iff || count < 0 || (count && !pairs))
        return IFFERR_SYNTAX;
    for (index = 0; index < count; index++) {
        error = StopChunk(iff, pairs[index * 2], pairs[index * 2 + 1]);
        if (error)
            return error;
    }
    return 0;
}

static int is_stopped(const struct ace_iff_handle *handle,
                      const struct ace_iff_chunk *chunk)
{
    size_t index;

    for (index = 0; index < handle->stop_count; index++)
        if (handle->stops[index].type == chunk->public_chunk.cn_Type &&
            handle->stops[index].id == chunk->public_chunk.cn_ID)
            return 1;
    return 0;
}

LONG ParseIFF(struct IFFHandle *iff, LONG mode)
{
    struct ace_iff_handle *handle;

    if (!iff)
        return IFFERR_NOTIFF;
    handle = ACE_IFF_HANDLE(iff);
    if (!handle->open || (iff->iff_Flags & IFFF_WRITE))
        return IFFERR_NOTIFF;
    if (mode != IFFPARSE_SCAN && mode != IFFPARSE_STEP &&
        mode != IFFPARSE_RAWSTEP)
        return IFFERR_SYNTAX;

    if (mode == IFFPARSE_SCAN) {
        while (handle->scan_cursor < handle->chunk_count) {
            struct ace_iff_chunk *chunk =
                &handle->chunks[handle->scan_cursor++];

            if (is_stopped(handle, chunk)) {
                chunk->public_chunk.cn_Scan = 0;
                handle->current_chunk = handle->scan_cursor - 1;
                handle->current_valid = 1;
                iff->iff_Depth = (LONG)(chunk->depth + 1);
                return 0;
            }
        }
        return IFFERR_EOF;
    }

    if (handle->event_cursor >= handle->event_count)
        return IFFERR_EOF;
    handle->current_chunk = handle->events[handle->event_cursor].chunk_index;
    handle->current_valid = 1;
    iff->iff_Depth = (LONG)(handle->chunks[handle->current_chunk].depth + 1);
    if (handle->events[handle->event_cursor++].entering)
        return 0;
    return IFFERR_EOC;
}
