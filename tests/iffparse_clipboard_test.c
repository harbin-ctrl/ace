#include <assert.h>
#include <string.h>

#include <datatypes/textclass.h>
#include <proto/iffparse.h>

#include "aros_exec_runtime.h"

static struct IFFHandle *new_handle(struct ClipboardHandle **clipboard,
                                    LONG unit, LONG mode)
{
    struct IFFHandle *iff = AllocIFF();

    assert(iff != NULL);
    *clipboard = OpenClipboard(unit);
    assert(*clipboard != NULL);
    iff->iff_Stream = (IPTR)*clipboard;
    InitIFFasClip(iff);
    assert(OpenIFF(iff, mode) == 0);
    return iff;
}

static void close_handle(struct IFFHandle *iff,
                         struct ClipboardHandle *clipboard)
{
    CloseIFF(iff);
    CloseClipboard(clipboard);
    FreeIFF(iff);
}

static void write_sample(void)
{
    struct ClipboardHandle *clipboard;
    struct IFFHandle *iff = new_handle(&clipboard, 0, IFFF_WRITE);
    const char odd[] = "xyz";
    const char first[] = "hello";
    const char second[] = "world";

    assert(PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) == 0);
    assert(PushChunk(iff, 0, MAKE_ID('U', 'N', 'K', 'N'),
                     IFFSIZE_UNKNOWN) == 0);
    assert(WriteChunkBytes(iff, (APTR)odd, 3) == 3);
    assert(PopChunk(iff) == 0);
    assert(PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN) == 0);
    assert(WriteChunkBytes(iff, (APTR)first, 5) == 5);
    assert(PopChunk(iff) == 0);
    assert(PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN) == 0);
    assert(WriteChunkBytes(iff, (APTR)second, 5) == 5);
    assert(PopChunk(iff) == 0);
    assert(PopChunk(iff) == 0);
    close_handle(iff, clipboard);
}

static void read_sample(void)
{
    struct ClipboardHandle *clipboard;
    struct IFFHandle *iff = new_handle(&clipboard, 0, IFFF_READ);
    struct ContextNode *chunk;
    char output[16] = {0};
    LONG error;
    LONG pairs[] = {ID_FTXT, ID_CHRS};
    size_t output_size = 0;

    CloseIFF(iff);
    assert(OpenIFF(iff, IFFF_READ) == 0);
    assert(StopChunks(iff, pairs, 1) == 0);
    while ((error = ParseIFF(iff, IFFPARSE_SCAN)) == 0) {
        chunk = CurrentChunk(iff);
        assert(chunk != NULL);
        assert(chunk->cn_Type == ID_FTXT);
        assert(chunk->cn_ID == ID_CHRS);
        assert(ReadChunkBytes(iff, output + output_size, 2) == 2);
        output_size += 2;
        assert(ReadChunkBytes(iff, output + output_size, 16) == 3);
        output_size += 3;
        assert(ReadChunkBytes(iff, output, 1) == 0);
    }
    assert(error == IFFERR_EOF);
    assert(output_size == 10);
    assert(memcmp(output, "helloworld", output_size) == 0);
    close_handle(iff, clipboard);
}

static void read_malformed(void)
{
    struct ClipboardHandle *clipboard = OpenClipboard(1);
    struct IFFHandle *iff;
    const char malformed[] = "NOTIFF!";

    assert(clipboard != NULL);
    {
        struct IOClipReq *request = &clipboard->cbh_Req;

        request->io_Command = CMD_WRITE;
        request->io_Data = (STRPTR)malformed;
        request->io_Length = sizeof(malformed) - 1;
        request->io_Offset = 0;
        request->io_ClipID = 0;
        assert(DoIO((struct IORequest *)request) == 0);
        request->io_Command = CMD_UPDATE;
        assert(DoIO((struct IORequest *)request) == 0);
    }

    iff = AllocIFF();
    assert(iff != NULL);
    iff->iff_Stream = (IPTR)clipboard;
    InitIFFasClip(iff);
    assert(OpenIFF(iff, IFFF_READ) == IFFERR_NOTIFF);
    FreeIFF(iff);
    CloseClipboard(clipboard);
}

static void read_truncated(void)
{
    struct ClipboardHandle *clipboard = OpenClipboard(2);
    struct IOClipReq *request;
    struct IFFHandle *iff;
    const UBYTE truncated[] = {
        'F', 'O', 'R', 'M', 0, 0, 0, 12,
        'F', 'T', 'X', 'T', 'C', 'H', 'R', 'S', 0, 0, 0, 4,
    };

    assert(clipboard != NULL);
    request = &clipboard->cbh_Req;
    request->io_Command = CMD_WRITE;
    request->io_Data = (STRPTR)truncated;
    request->io_Length = sizeof(truncated);
    request->io_Offset = 0;
    request->io_ClipID = 0;
    assert(DoIO((struct IORequest *)request) == 0);
    request->io_Command = CMD_UPDATE;
    assert(DoIO((struct IORequest *)request) == 0);

    iff = AllocIFF();
    assert(iff != NULL);
    iff->iff_Stream = (IPTR)clipboard;
    InitIFFasClip(iff);
    assert(OpenIFF(iff, IFFF_READ) == IFFERR_MANGLED);
    FreeIFF(iff);
    CloseClipboard(clipboard);
}

int main(void)
{
    write_sample();
    read_sample();
    read_malformed();
    read_truncated();
    return 0;
}
