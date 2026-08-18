#include "aros_exec_runtime.h"

#include <assert.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <devices/clipboard.h>
#include <dos/dosextens.h>
#include <proto/exec.h>

int main(void)
{
    struct MsgPort *port;
    struct Message message = {0};
    struct IOStdReq *request;
    struct IOClipReq *clip_writer;
    struct IOClipReq *clip_reader;
    char buffer[32] = {0};
    char clip_buffer[32] = {0};
    void *console;
    struct Task first_task = {0};
    struct Task second_task = {0};

    /* SIGUSR1 is ACE's private host notification for a console Ctrl-C. */
    assert(kill(getpid(), SIGUSR1) == 0);
    assert((ace_aros_runtime_set_signal(0, 0) & (1u << 12)) != 0);
    ace_aros_runtime_set_signal(0, 1u << 12);
    assert(kill(getpid(), SIGUSR2) == 0);
    assert((ace_aros_runtime_set_signal(0, 0) & (1u << 13)) != 0);
    ace_aros_runtime_set_signal(0, 1u << 13);

    /* Exec signals belong to the addressed task, rather than the process as
       a whole.  This is the live runtime's task registry, not exec_compat's
       separate unit-test implementation. */
    ace_aros_runtime_set_current_task(&first_task);
    assert(ace_aros_runtime_set_signal(1u << 4, 0) == 0);
    ace_aros_runtime_set_current_task(&second_task);
    assert(ace_aros_runtime_check_signal(~0u) == 0);
    ace_aros_runtime_signal_task(&first_task, 1u << 5);
    assert(ace_aros_runtime_check_signal(~0u) == 0);
    ace_aros_runtime_set_current_task(&first_task);
    assert(ace_aros_runtime_check_signal(~0u) == ((1u << 4) | (1u << 5)));
    ace_aros_runtime_set_signal(0, (1u << 4) | (1u << 5));

    port = CreateMsgPort();
    assert(port != NULL);
    PutMsg(port, &message);
    assert(GetMsg(port) == &message);

    request = CreateIORequest(port, sizeof(*request));
    assert(request != NULL);
    assert(OpenDevice("console.device", 3, (struct IORequest *)request, 0) ==
           0);
    console = ace_aros_console_last();
    assert(console != NULL);

    request->io_Command = CMD_WRITE;
    request->io_Data = (APTR)"hello\n";
    request->io_Length = 6;
    assert(DoIO((struct IORequest *)request) == 0);
    assert(ace_aros_console_take_output(console, buffer, sizeof(buffer)) == 6);
    assert(memcmp(buffer, "hello\n", 6) == 0);

    assert(ace_aros_console_feed(console, "input\n", 6) == 0);
    memset(buffer, 0, sizeof(buffer));
    request->io_Command = CMD_READ;
    request->io_Data = buffer;
    request->io_Length = sizeof(buffer);
    SendIO((struct IORequest *)request);
    assert(WaitIO((struct IORequest *)request) == 0);
    assert(request->io_Actual == 6);
    assert(memcmp(buffer, "input\n", 6) == 0);

    clip_writer = CreateIORequest(port, sizeof(*clip_writer));
    assert(clip_writer != NULL);
    assert(OpenDevice("clipboard.device", 0,
                      (struct IORequest *)clip_writer, 0) == 0);

    clip_writer->io_Command = CMD_WRITE;
    clip_writer->io_Data = (STRPTR)"FORM";
    clip_writer->io_Length = 4;
    clip_writer->io_Offset = 0;
    clip_writer->io_ClipID = 0;
    assert(DoIO((struct IORequest *)clip_writer) == 0);
    assert(clip_writer->io_Actual == 4);
    assert(clip_writer->io_ClipID != 0);

    clip_writer->io_Data = (STRPTR)"FTXT";
    clip_writer->io_Length = 4;
    assert(DoIO((struct IORequest *)clip_writer) == 0);
    assert(clip_writer->io_Actual == 4);

    clip_writer->io_Command = CMD_UPDATE;
    clip_writer->io_Data = NULL;
    clip_writer->io_Length = 0;
    assert(DoIO((struct IORequest *)clip_writer) == 0);
    assert(clip_writer->io_ClipID == -1);

    clip_reader = CreateIORequest(port, sizeof(*clip_reader));
    assert(clip_reader != NULL);
    assert(OpenDevice("clipboard.device", 0,
                      (struct IORequest *)clip_reader, 0) == 0);

    clip_reader->io_Command = CMD_READ;
    clip_reader->io_Data = NULL;
    clip_reader->io_Length = sizeof(clip_buffer);
    clip_reader->io_Offset = 0;
    clip_reader->io_ClipID = 0;
    assert(DoIO((struct IORequest *)clip_reader) == 0);
    assert(clip_reader->io_Actual == 8);
    assert(clip_reader->io_Offset == 8);

    clip_reader->io_Data = clip_buffer;
    clip_reader->io_Length = sizeof(clip_buffer);
    clip_reader->io_Offset = 0;
    clip_reader->io_ClipID = 0;
    assert(DoIO((struct IORequest *)clip_reader) == 0);
    assert(clip_reader->io_Actual == 8);
    assert(memcmp(clip_buffer, "FORMFTXT", 8) == 0);

    clip_reader->io_Data = clip_buffer;
    clip_reader->io_Length = sizeof(clip_buffer);
    assert(DoIO((struct IORequest *)clip_reader) == 0);
    assert(clip_reader->io_Actual == 0);
    assert(clip_reader->io_ClipID == -1);

    CloseDevice((struct IORequest *)clip_reader);
    CloseDevice((struct IORequest *)clip_writer);
    DeleteIORequest((struct IORequest *)clip_reader);
    DeleteIORequest((struct IORequest *)clip_writer);

    CloseDevice((struct IORequest *)request);
    DeleteIORequest((struct IORequest *)request);
    DeleteMsgPort(port);
    return 0;
}
