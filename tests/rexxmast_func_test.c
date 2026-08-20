#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>

static int write_all(int descriptor, const char *text, size_t length)
{
    while (length) {
        ssize_t written = write(descriptor, text, length);

        if (written <= 0)
            return -1;
        text += written;
        length -= (size_t)written;
    }
    return 0;
}

int main(void)
{
    char script_path[] = "ace-rexxmast-func-test.rexx";
    const char program[] = "return arg(1) || ':' || arg(2)\n";
    const char first[] = "left";
    const char second[] = "right";
    const char expected[] = "left:right";
    struct MsgPort *port;
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;
    int descriptor;
    int success;

    descriptor = open(script_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0)
        return 20;
    if (write_all(descriptor, program, sizeof(program) - 1) != 0) {
        close(descriptor);
        unlink(script_path);
        return 20;
    }
    close(descriptor);

    port = FindPort("REXX");
    reply_port = port ? CreatePort(NULL, 0) : NULL;
    message = reply_port
                  ? CreateRexxMsg(reply_port, (UBYTE *)".rexx",
                                  (UBYTE *)"COMMAND")
                  : NULL;
    if (!message)
        goto fail_file;

    message->rm_Action = RXFUNC | RXFF_RESULT | 2;
    message->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)script_path,
                                                  strlen(script_path));
    message->rm_Args[1] = (IPTR)CreateArgstring((UBYTE *)first,
                                                  strlen(first));
    message->rm_Args[2] = (IPTR)CreateArgstring((UBYTE *)second,
                                                  strlen(second));
    if (!message->rm_Args[0] || !message->rm_Args[1] || !message->rm_Args[2])
        goto fail_message;

    PutMsg(port, (struct Message *)message);
    reply = (struct RexxMsg *)WaitPort(reply_port);
    success = reply == message && message->rm_Result1 == 0 &&
              message->rm_Result2 != 0 &&
              LengthArgstring((UBYTE *)message->rm_Result2) ==
                  sizeof(expected) - 1 &&
              memcmp((void *)message->rm_Result2, expected,
                     sizeof(expected) - 1) == 0;
    if (!success)
        fprintf(stderr, "RXFUNC reply=%p message=%p result1=%ld result2=%p\n",
                (void *)reply, (void *)message, (long)message->rm_Result1,
                (void *)message->rm_Result2);

    if (message->rm_Result2)
        DeleteArgstring((UBYTE *)message->rm_Result2);
    DeleteArgstring((UBYTE *)message->rm_Args[0]);
    DeleteArgstring((UBYTE *)message->rm_Args[1]);
    DeleteArgstring((UBYTE *)message->rm_Args[2]);
    DeleteRexxMsg(message);
    DeletePort(reply_port);
    unlink(script_path);
    return success ? 0 : 1;

fail_message:
    if (message->rm_Args[0])
        DeleteArgstring((UBYTE *)message->rm_Args[0]);
    if (message->rm_Args[1])
        DeleteArgstring((UBYTE *)message->rm_Args[1]);
    if (message->rm_Args[2])
        DeleteArgstring((UBYTE *)message->rm_Args[2]);
    DeleteRexxMsg(message);
    DeletePort(reply_port);
fail_file:
    if (!message && reply_port)
        DeletePort(reply_port);
    unlink(script_path);
    return 20;
}
