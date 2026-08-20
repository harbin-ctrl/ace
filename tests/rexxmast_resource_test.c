#include <stdio.h>
#include <string.h>

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>

static int send_argstrings(struct MsgPort *port, LONG action,
                           const char *const *values, size_t count,
                           LONG expected)
{
    struct MsgPort *reply_port = CreatePort(NULL, 0);
    struct RexxMsg *message = reply_port
                                  ? CreateRexxMsg(reply_port, NULL, NULL)
                                  : NULL;
    struct RexxMsg *reply;
    size_t index;
    int success;

    if (!message)
        return 0;
    message->rm_Action = action;
    for (index = 0; index < count; index++) {
        message->rm_Args[index] =
            (IPTR)CreateArgstring((UBYTE *)values[index],
                                  strlen(values[index]));
        if (!message->rm_Args[index]) {
            ClearRexxMsg(message, (ULONG)count);
            DeleteRexxMsg(message);
            DeletePort(reply_port);
            return 0;
        }
    }
    PutMsg(port, (struct Message *)message);
    reply = (struct RexxMsg *)WaitPort(reply_port);
    success = reply == message && message->rm_Result1 == expected;
    if (!success)
        fprintf(stderr, "action %08lx result1=%ld expected=%ld\n",
                (unsigned long)action, (long)message->rm_Result1,
                (long)expected);
    ClearRexxMsg(message, (ULONG)count);
    DeleteRexxMsg(message);
    DeletePort(reply_port);
    return success;
}

static int send_clip(struct MsgPort *port, const char *name,
                     const unsigned char *value, size_t value_length,
                     LONG action, LONG expected)
{
    struct MsgPort *reply_port = CreatePort(NULL, 0);
    struct RexxMsg *message = reply_port
                                  ? CreateRexxMsg(reply_port, NULL, NULL)
                                  : NULL;
    struct RexxMsg *reply;
    int success;

    if (!message)
        return 0;
    message->rm_Action = action;
    message->rm_Args[0] = (IPTR)name;
    if (action == RXADDCON) {
        message->rm_Args[1] = (IPTR)value;
        message->rm_Args[2] = (IPTR)value_length;
    }
    PutMsg(port, (struct Message *)message);
    reply = (struct RexxMsg *)WaitPort(reply_port);
    success = reply == message && message->rm_Result1 == expected;
    if (!success)
        fprintf(stderr, "clip action %08lx result1=%ld expected=%ld\n",
                (unsigned long)action, (long)message->rm_Result1,
                (long)expected);
    DeleteRexxMsg(message);
    DeletePort(reply_port);
    return success;
}

int main(void)
{
    static const char *library[] = { "ACE.TEST.LIB", "5", "12", "3" };
    static const char *host[] = { "ACE.TEST.HOST", "2" };
    static const char *remove_library[] = { "ACE.TEST.LIB" };
    static const char *remove_host[] = { "ACE.TEST.HOST" };
    static const unsigned char first_clip[] = { 'a', '\0', 'b' };
    static const unsigned char second_clip[] = { 'n', 'e', 'w' };
    struct MsgPort *port = FindPort("REXX");
    int success = port != NULL;

    if (success)
        success = send_argstrings(port, RXADDLIB, library, 4, 0);
    if (success)
        success = send_argstrings(port, RXADDLIB, library, 4, 5);
    if (success)
        success = send_argstrings(port, RXREMLIB, remove_library, 1, 0);
    if (success)
        success = send_argstrings(port, RXREMLIB, remove_library, 1, 5);

    if (success)
        success = send_argstrings(port, RXADDFH, host, 2, 0);
    if (success)
        success = send_argstrings(port, RXADDFH, host, 2, 5);
    if (success)
        success = send_argstrings(port, RXREMLIB, remove_host, 1, 0);

    if (success)
        success = send_clip(port, "ACE.TEST.CLIP", first_clip,
                            sizeof(first_clip), RXADDCON, 0);
    if (success)
        success = send_clip(port, "ACE.TEST.CLIP", second_clip,
                            sizeof(second_clip), RXADDCON, 0);
    if (success)
        success = send_clip(port, "ACE.TEST.CLIP", NULL, 0, RXREMCON, 0);
    if (success)
        success = send_clip(port, "ACE.TEST.CLIP", NULL, 0, RXREMCON, 0);

    return success ? 0 : 1;
}
