#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <datatypes/textclass.h>
#include <libraries/iffparse.h>
#include <proto/iffparse.h>

#define ACEPASTE_USAGE \
    "usage: acepaste [--unit N|-u N] [--get|--set]\n"

static int parse_unit(const char *text, ULONG *unit)
{
    char *end;
    unsigned long value;

    if (!text || !*text)
        return -1;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || *end || value >= 256)
        return -1;
    *unit = (ULONG)value;
    return 0;
}

static int get_clip(ULONG unit)
{
    struct IFFHandle *iff = NULL;
    struct ClipboardHandle *clipboard = NULL;
    struct ContextNode *chunk;
    LONG stops[] = {ID_FTXT, ID_CHRS};
    LONG error;
    int found = 0;
    int result = 1;

    iff = AllocIFF();
    if (!iff)
        goto done;
    clipboard = OpenClipboard((LONG)unit);
    if (!clipboard)
        goto done;
    iff->iff_Stream = (IPTR)clipboard;
    InitIFFasClip(iff);
    error = OpenIFF(iff, IFFF_READ);
    if (error != 0)
        goto done;
    if (StopChunk(iff, ID_FTXT, ID_CHRS) != 0)
        goto close_iff;
    for (;;) {
        error = ParseIFF(iff, IFFPARSE_SCAN);
        if (error == IFFERR_EOF)
            break;
        if (error == IFFERR_EOC)
            continue;
        if (error != 0)
            goto close_iff;
        chunk = CurrentChunk(iff);
        if (!chunk || chunk->cn_Type != stops[0] ||
            chunk->cn_ID != stops[1])
            continue;
        found = 1;
        while (chunk->cn_Size != 0) {
            unsigned char buffer[4096];
            ULONG wanted = (ULONG)chunk->cn_Size > (ULONG)sizeof(buffer) ?
                           (ULONG)sizeof(buffer) : (ULONG)chunk->cn_Size;
            LONG actual = ReadChunkBytes(iff, buffer, wanted);

            if (actual != (LONG)wanted ||
                fwrite(buffer, 1, (size_t)actual, stdout) !=
                    (size_t)actual)
                goto close_iff;
            chunk->cn_Size -= (ULONG)actual;
        }
    }
    result = found ? 0 : 2;

close_iff:
    CloseIFF(iff);
done:
    if (result == 2)
        fprintf(stderr, "acepaste: clipboard unit %lu has no FTXT/CHRS text\n",
                (unsigned long)unit);
    else if (result != 0)
        fprintf(stderr, "acepaste: unable to read clipboard unit %lu\n",
                (unsigned long)unit);
    if (clipboard)
        CloseClipboard(clipboard);
    if (iff)
        FreeIFF(iff);
    return result;
}

static int set_clip(ULONG unit)
{
    struct IFFHandle *iff = NULL;
    struct ClipboardHandle *clipboard = NULL;
    unsigned char buffer[4096];
    size_t count;
    int result = 1;

    iff = AllocIFF();
    if (!iff)
        goto done;
    clipboard = OpenClipboard((LONG)unit);
    if (!clipboard)
        goto done;
    iff->iff_Stream = (IPTR)clipboard;
    InitIFFasClip(iff);
    if (OpenIFF(iff, IFFF_WRITE) != 0 ||
        PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) != 0 ||
        PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN) != 0)
        goto close_iff;
    while ((count = fread(buffer, 1, sizeof(buffer), stdin)) != 0) {
        if (WriteChunkBytes(iff, buffer, (LONG)count) != (LONG)count)
            goto close_iff;
    }
    if (ferror(stdin) || PopChunk(iff) != 0 || PopChunk(iff) != 0)
        goto close_iff;
    result = 0;

close_iff:
    CloseIFF(iff);
done:
    if (result != 0)
        fprintf(stderr, "acepaste: unable to write clipboard unit %lu\n",
                (unsigned long)unit);
    if (clipboard)
        CloseClipboard(clipboard);
    if (iff)
        FreeIFF(iff);
    return result;
}

int main(int argc, char **argv)
{
    ULONG unit = 0;
    int mode = 0;

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];

        if (strcmp(argument, "--get") == 0 || strcmp(argument, "-g") == 0)
            mode = 1;
        else if (strcmp(argument, "--set") == 0 || strcmp(argument, "-s") == 0)
            mode = 2;
        else if (strcmp(argument, "--unit") == 0 ||
                 strcmp(argument, "-u") == 0) {
            if (++index == argc || parse_unit(argv[index], &unit) != 0) {
                fputs(ACEPASTE_USAGE, stderr);
                return 2;
            }
        } else if (strncmp(argument, "--unit=", 7) == 0) {
            if (parse_unit(argument + 7, &unit) != 0) {
                fputs(ACEPASTE_USAGE, stderr);
                return 2;
            }
        } else if (strcmp(argument, "--help") == 0 ||
                   strcmp(argument, "-h") == 0) {
            fputs(ACEPASTE_USAGE, stdout);
            return 0;
        } else if (parse_unit(argument, &unit) != 0) {
            fputs(ACEPASTE_USAGE, stderr);
            return 2;
        }
    }
    if (!mode)
        mode = isatty(STDIN_FILENO) ? 1 : 2;
    return mode == 1 ? get_clip(unit) : set_clip(unit);
}
