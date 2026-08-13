#include "aros_exec_runtime.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    struct MsgPort *port;
    struct Message message = {0};
    struct IOStdReq *request;
    char buffer[32] = {0};
    void *console;

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

    CloseDevice((struct IORequest *)request);
    DeleteIORequest((struct IORequest *)request);
    DeleteMsgPort(port);
    return 0;
}
