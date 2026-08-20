#include <stdio.h>

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>

int main(void)
{
    struct MsgPort *port = FindPort("REXX");
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;

    reply_port = port ? CreatePort(NULL, 0) : NULL;
    message = reply_port
                  ? CreateRexxMsg(reply_port, (UBYTE *)".rexx",
                                  (UBYTE *)"COMMAND")
                  : NULL;
    if (!message)
        return 20;
    message->rm_Action = RXCLOSE;
    PutMsg(port, (struct Message *)message);
    reply = (struct RexxMsg *)WaitPort(reply_port);
    if (reply != message) {
        fprintf(stderr, "close reply=%p message=%p\n", (void *)reply,
                (void *)message);
        DeleteRexxMsg(message);
        DeletePort(reply_port);
        return 1;
    }
    DeleteRexxMsg(message);
    DeletePort(reply_port);
    return 0;
}
