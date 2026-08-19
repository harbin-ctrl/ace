#define _POSIX_C_SOURCE 200809L

/*
 * The other end for the two AROS demo programs.
 *
 * They do not pair with each other: sendrexxmsg.c sends to "REXX" and
 * listen4msg.c serves "TEST", because on a real Amiga the thing on the far
 * side of sendrexxmsg is RexxMast. So each is run against a counterpart
 * written here, which is what lets both be exercised *unmodified* -- the
 * point of the exercise being that ACE implements the contract, not that the
 * demos were adjusted until they passed.
 *
 *   serve <port>          own <port>, answer one message, reply with a result
 *   send  <port> <text>   send <text> to <port> and wait for it to come back
 *
 * "send" passes its own stdin and stdout with the message, which is what
 * listen4msg writes to.
 */

#include "broker_client.h"

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <rexx/storage.h>
#include <clib/rexxsyslib_protos.h>

/* Declared here rather than through <proto/dos.h>: this file is built with
   compat/aros-real/include first, and that tree's thin proto/dos.h wins the
   lookup and hides them. */
BPTR Input(void);
BPTR Output(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char reply_text[] = "hello everybody";

static int serve(const char *port_name)
{
    struct MsgPort *port = CreatePort((CONST_STRPTR)port_name, 0);
    struct RexxMsg *message;

    if (!port) {
        fprintf(stderr, "peer: cannot create %s\n", port_name);
        return 20;
    }
    WaitPort(port);
    message = (struct RexxMsg *)GetMsg(port);
    if (!message) {
        fprintf(stderr, "peer: no message\n");
        return 20;
    }
    if (!IsRexxMsg(message)) {
        fprintf(stderr, "peer: not a RexxMsg\n");
        return 20;
    }
    printf("peer: action %08lx\n", (unsigned long)message->rm_Action);
    if (message->rm_Args[0])
        printf("peer: argument %s\n", (char *)message->rm_Args[0]);
    fflush(stdout);
    message->rm_Result1 = 0;
    message->rm_Result2 = (IPTR)CreateArgstring((UBYTE *)reply_text,
                                                (ULONG)strlen(reply_text));
    ReplyMsg((struct Message *)message);
    DeletePort(port);
    return 0;
}

static int send_to(const char *port_name, const char *text)
{
    struct MsgPort *port = FindPort((CONST_STRPTR)port_name);
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;

    if (!port) {
        fprintf(stderr, "peer: %s not found\n", port_name);
        return 20;
    }
    reply_port = CreateMsgPort();
    message = CreateRexxMsg(reply_port, NULL, NULL);
    if (!reply_port || !message) {
        fprintf(stderr, "peer: out of memory\n");
        return 20;
    }
    message->rm_Action = 0x01000000;
    message->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)text,
                                                (ULONG)strlen(text));
    /* The streams listen4msg writes to. */
    message->rm_Stdin = Input();
    message->rm_Stdout = Output();
    PutMsg(port, (struct Message *)message);
    WaitPort(reply_port);
    reply = (struct RexxMsg *)GetMsg(reply_port);
    if (reply != message) {
        fprintf(stderr, "peer: got back a different message\n");
        return 20;
    }
    printf("peer: result1 %ld\n", (long)reply->rm_Result1);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    if (native_broker_ensure() != 0) {
        fprintf(stderr, "peer: no broker\n");
        return 20;
    }
    if (argc == 3 && strcmp(argv[1], "serve") == 0)
        return serve(argv[2]);
    if (argc == 4 && strcmp(argv[1], "send") == 0)
        return send_to(argv[2], argv[3]);
    fprintf(stderr, "use: %s serve <port> | send <port> <text>\n", argv[0]);
    return 20;
}
