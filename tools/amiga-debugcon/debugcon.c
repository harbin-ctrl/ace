/*
 * DBGCON - record the bytes sent to AmigaOS console.device while a command
 * runs, then pass every request on to the original device implementation.
 *
 * This deliberately hooks console.device rather than replacing CON:.  The
 * real CON handler, console.device, and the target program therefore remain
 * in the path; the trace is the byte stream immediately before rendering.
 */

#include <exec/types.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <devices/conunit.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

typedef VOID (*BeginIOFunction)(
    __reg("a1") struct IORequest *request,
    __reg("a6") struct Device *device);

static BeginIOFunction original_begin_io;
static BPTR trace_file;
static BOOL trace_in_progress;

static VOID trace_bytes(const UBYTE *data, ULONG length)
{
    while (length != 0)
    {
        LONG written = Write(trace_file, data, (LONG)length);

        if (written <= 0)
            break;

        data += written;
        length -= (ULONG)written;
    }

    Flush(trace_file);
}

static __saveds VOID trace_begin_io(
    __reg("a1") struct IORequest *request,
    __reg("a6") struct Device *device)
{
    struct IOStdReq *standard_request = (struct IOStdReq *)request;

    /* The guard prevents the diagnostic file write from recursively tracing
     * itself if DOS happens to use the console while writing the file. */
    if (!trace_in_progress &&
        standard_request != NULL &&
        standard_request->io_Command == CMD_WRITE &&
        standard_request->io_Data != NULL &&
        standard_request->io_Length != 0)
    {
        trace_in_progress = TRUE;
        trace_bytes((const UBYTE *)standard_request->io_Data,
                    standard_request->io_Length);
        trace_in_progress = FALSE;
    }

    original_begin_io(request, device);
}

static BOOL append_command(char *buffer, ULONG buffer_size,
                           int argc, char **argv)
{
    ULONG used = 0;
    int index;

    buffer[0] = '\0';

    for (index = 2; index < argc; index++)
    {
        ULONG part_length = (ULONG)strlen(argv[index]);

        if (index != 2)
        {
            if (used + 1 >= buffer_size)
                return FALSE;

            buffer[used++] = ' ';
        }

        if (used + part_length >= buffer_size)
            return FALSE;

        memcpy(buffer + used, argv[index], part_length);
        used += part_length;
        buffer[used] = '\0';
    }

    return used != 0;
}

int main(int argc, char **argv)
{
    struct MsgPort *message_port = NULL;
    struct IOStdReq *io_request = NULL;
    struct Device *console_device = NULL;
    char command[512];
    LONG command_result;
    BOOL hooked = FALSE;
    int result = RETURN_FAIL;

    /* Syntax: DBGCON trace-file command [command arguments...] */
    if (argc < 3 || !append_command(command, sizeof(command), argc, argv))
        return RETURN_ERROR;

    trace_file = Open(argv[1], MODE_NEWFILE);
    if (trace_file == 0)
        return RETURN_FAIL;

    message_port = CreateMsgPort();
    if (message_port == NULL)
        goto cleanup;

    io_request = (struct IOStdReq *)CreateIORequest(
        message_port, sizeof(struct IOStdReq));
    if (io_request == NULL)
        goto cleanup;

    /* CONU_LIBRARY opens the device without binding it to a window. */
    if (OpenDevice("console.device", CONU_LIBRARY,
                   (struct IORequest *)io_request, 0) != 0)
        goto cleanup;

    console_device = io_request->io_Device;
    original_begin_io = (BeginIOFunction)SetFunction(
        (struct Library *)console_device,
        DEV_BEGINIO,
        (ULONG (*)())trace_begin_io);
    if (original_begin_io == NULL)
        goto cleanup_device;

    hooked = TRUE;
    command_result = SystemTags(command, TAG_DONE);

    SetFunction((struct Library *)console_device,
                DEV_BEGINIO,
                (ULONG (*)())original_begin_io);
    hooked = FALSE;
    result = (int)command_result;

cleanup_device:
    if (hooked)
        SetFunction((struct Library *)console_device,
                    DEV_BEGINIO,
                    (ULONG (*)())original_begin_io);
    CloseDevice((struct IORequest *)io_request);

cleanup:
    if (io_request != NULL)
        DeleteIORequest((APTR)io_request);
    if (message_port != NULL)
        DeleteMsgPort(message_port);
    if (trace_file != 0)
        Close(trace_file);

    return result;
}
