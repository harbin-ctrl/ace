#include <stdio.h>
#include <string.h>

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>

int main(void)
{
    const char command[] = "'return 6 * 7'";
    struct MsgPort *port = FindPort("REXX");
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;
    int success;

    if (!port)
        return 20;
    reply_port = CreatePort(NULL, 0);
    message = reply_port ? CreateRexxMsg(reply_port, ".rexx", "COMMAND") : NULL;
    if (!message)
        return 20;
    message->rm_Action = RXCOMM | RXFF_RESULT;
    message->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)command,
                                                  strlen(command));
    if (!message->rm_Args[0])
        return 20;
    PutMsg(port, (struct Message *)message);
    reply = (struct RexxMsg *)WaitPort(reply_port);
    success = reply == message && message->rm_Result1 == 42 &&
              message->rm_Result2 != 0 &&
              LengthArgstring((UBYTE *)message->rm_Result2) == 2 &&
              memcmp((void *)message->rm_Result2, "42", 2) == 0;
    if (!success)
        fprintf(stderr, "reply=%p message=%p result1=%ld result2=%p len=%lu\n",
                (void *)reply, (void *)message, (long)message->rm_Result1,
                (void *)message->rm_Result2,
                message->rm_Result2
                    ? (unsigned long)LengthArgstring(
                          (UBYTE *)message->rm_Result2)
                    : 0UL);
    if (message->rm_Result2)
        DeleteArgstring((UBYTE *)message->rm_Result2);
    DeleteArgstring((UBYTE *)message->rm_Args[0]);
    DeleteRexxMsg(message);
    DeletePort(reply_port);
    return success ? 0 : 1;
}
