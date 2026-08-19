#define _POSIX_C_SOURCE 200809L

/*
 * PutMsg() and ReplyMsg() between two processes, through the Amiga API only.
 *
 * The tests below this one drive the broker directly; this one does not
 * mention it. It creates a port, finds it from another process, sends a
 * RexxMsg and waits on a reply port, which is what an ARexx client actually
 * does -- and is the shape of sendrexxmsg.c and listen4msg.c from the AROS
 * tree.
 *
 * The assertion that matters most is that the reply is the *same pointer* the
 * sender sent. sendrexxmsg.c asserts it, and Regina's sendandwait()
 * (amifuncs.c:601) replies to anything on its reply port that is not the
 * pointer it sent and goes straight back to waiting -- so a "reply" that is a
 * different message hangs the sender just as surely as no reply at all.
 */

#include "broker_client.h"

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <clib/rexxsyslib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

#define PORT_NAME "ace-rexx-port-test"

/* NUL in the middle and bytes above 0x7f: an argstring is counted bytes. */
static const unsigned char argument[] = {
    'a', 'r', 'g', 0x00, 0xfe, 0xff, 'z'
};
#define ARGUMENT_LENGTH (sizeof(argument))
static const unsigned char answer[] = {
    'o', 'k', 0x00, 0x80, 0xff, '!'
};
#define ANSWER_LENGTH (sizeof(answer))

static void receiver(int ready_fd)
{
    struct MsgPort *port;
    struct RexxMsg *message;

    native_broker_reset_after_fork();
    if (native_broker_ensure() != 0)
        _exit(2);
    port = CreatePort((CONST_STRPTR)PORT_NAME, 0);
    if (!port)
        _exit(3);
    if (write(ready_fd, "R", 1) != 1)
        _exit(4);

    WaitPort(port);
    message = (struct RexxMsg *)GetMsg(port);
    if (!message)
        _exit(5);
    if (!IsRexxMsg(message))
        _exit(6);
    if (message->rm_Action != 42)
        _exit(7);
    if (!message->rm_Args[0] ||
        LengthArgstring((UBYTE *)message->rm_Args[0]) != ARGUMENT_LENGTH ||
        memcmp((void *)message->rm_Args[0], argument, ARGUMENT_LENGTH) != 0)
        _exit(8);

    message->rm_Result1 = 0;
    message->rm_Result2 = (IPTR)CreateArgstring((UBYTE *)answer,
                                                ANSWER_LENGTH);
    ReplyMsg((struct Message *)message);
    DeletePort(port);
    _exit(0);
}

int main(void)
{
    int ready[2];
    pid_t child;
    char byte;
    int status;
    struct MsgPort *remote;
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;

    if (native_broker_ensure() != 0) {
        fprintf(stderr, "FAIL: no broker\n");
        return 1;
    }
    if (pipe(ready) != 0)
        return 1;
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        close(ready[0]);
        receiver(ready[1]);
    }
    close(ready[1]);
    if (read(ready[0], &byte, 1) != 1) {
        fprintf(stderr, "FAIL: the receiver never came up\n");
        return 1;
    }

    remote = FindPort((CONST_STRPTR)PORT_NAME);
    check(remote != NULL, "another process's port is found");
    if (!remote)
        return 1;

    reply_port = CreateMsgPort();
    check(reply_port != NULL, "the sender has a reply port");
    message = CreateRexxMsg(reply_port, NULL, (UBYTE *)PORT_NAME);
    check(message != NULL, "the sender has a message");
    if (!reply_port || !message)
        return 1;
    message->rm_Action = 42;
    message->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)argument,
                                                ARGUMENT_LENGTH);

    PutMsg(remote, (struct Message *)message);
    WaitPort(reply_port);
    reply = (struct RexxMsg *)GetMsg(reply_port);

    check(reply == message, "the reply is the very message that was sent");
    check(reply && reply->rm_Result1 == 0, "the result code comes back");
    check(reply && reply->rm_Result2 != 0, "a result string comes back");
    if (reply && reply->rm_Result2) {
        check(LengthArgstring((UBYTE *)reply->rm_Result2) == ANSWER_LENGTH,
              "the result keeps its length, NULs included");
        check(memcmp((void *)reply->rm_Result2, answer, ANSWER_LENGTH) == 0,
              "the result arrives byte for byte");
        DeleteArgstring((UBYTE *)reply->rm_Result2);
    }
    /* The argument is still the sender's to free: a reply carries results
       back, it does not take ownership of what was sent. */
    check(message->rm_Args[0] != 0, "the sender still owns its argument");
    if (message->rm_Args[0])
        DeleteArgstring((UBYTE *)message->rm_Args[0]);
    DeleteRexxMsg(message);

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        fprintf(stderr, "FAIL: receiver exited %d\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        failures++;
    }
    close(ready[0]);

    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("rexx port: ok\n");
    return failures ? 1 : 0;
}
