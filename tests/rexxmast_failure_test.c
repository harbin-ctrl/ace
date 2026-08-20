#include <stdio.h>
#include <string.h>

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>

int main(void)
{
    const char missing[] = "/tmp/ace-rexxmast-file-that-does-not-exist";
    struct MsgPort *port = FindPort("REXX");
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;
    int success;

    reply_port = port ? CreatePort(NULL, 0) : NULL;
    message = reply_port
                  ? CreateRexxMsg(reply_port, (UBYTE *)".rexx",
                                  (UBYTE *)"COMMAND")
                  : NULL;
    if (!message)
        return 20;

    message->rm_Action = RXFUNC | RXFF_RESULT;
    message->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)missing,
                                                  strlen(missing));
    if (!message->rm_Args[0]) {
        DeleteRexxMsg(message);
        DeletePort(reply_port);
        return 20;
    }

    PutMsg(port, (struct Message *)message);
    reply = (struct RexxMsg *)WaitPort(reply_port);
    success = reply == message && message->rm_Result1 == 5 &&
              message->rm_Result2 == 0;
    if (!success)
        fprintf(stderr,
                "failure reply=%p message=%p result1=%ld result2=%p\n",
                (void *)reply, (void *)message, (long)message->rm_Result1,
                (void *)message->rm_Result2);

    if (message->rm_Result2 && message->rm_Result2 > 4096)
        DeleteArgstring((UBYTE *)message->rm_Result2);
    DeleteArgstring((UBYTE *)message->rm_Args[0]);
    DeleteRexxMsg(message);
    DeletePort(reply_port);
    return success ? 0 : 1;
}
