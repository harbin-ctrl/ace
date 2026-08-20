#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>

#include <exec/ports.h>
#include <proto/exec.h>
#include <rexx/storage.h>
#include <clib/rexxsyslib_protos.h>

struct helper_context {
    struct MsgPort *port;
    struct MsgPort *expected_port;
    void *expected_tsd;
    struct RexxRsrc *expected_resource;
    int pointers_ok;
};

static void *private_helper(void *argument)
{
    struct helper_context *context = argument;
    struct RexxMsg *message;

    WaitPort(context->port);
    message = (struct RexxMsg *)GetMsg(context->port);
    if (!message)
        return NULL;
    context->pointers_ok =
        message->rm_Private1 == (IPTR)context->expected_port &&
        message->rm_Private2 == (IPTR)context->expected_tsd &&
        message->rm_Args[0] == (IPTR)context->expected_resource;
    message->rm_Result1 = context->pointers_ok ? 77 : 78;
    message->rm_Result2 = 0;
    ReplyMsg((struct Message *)message);
    return NULL;
}

int main(void)
{
    struct MsgPort *remote = FindPort("REXX");
    struct MsgPort *reply_port = CreateMsgPort();
    struct MsgPort *helper_port = CreateMsgPort();
    struct RexxMsg *message;
    struct RexxRsrc resource = { 0 };
    struct helper_context context;
    pthread_t helper_thread;
    void *tsd = &context;
    int success;

    if (!remote || !reply_port || !helper_port)
        return 1;
    context.port = helper_port;
    context.expected_port = helper_port;
    context.expected_tsd = tsd;
    context.expected_resource = &resource;
    context.pointers_ok = 0;
    if (pthread_create(&helper_thread, NULL, private_helper, &context) != 0)
        return 1;

    message = CreateRexxMsg(reply_port, NULL, NULL);
    if (!message)
        return 1;
    message->rm_Private1 = (IPTR)helper_port;
    message->rm_Private2 = (IPTR)tsd;
    message->rm_Action = RXADDRSRC;
    message->rm_Args[0] = (IPTR)&resource;
    PutMsg(remote, (struct Message *)message);
    if (WaitPort(reply_port) != (struct Message *)message)
        return 1;

    pthread_join(helper_thread, NULL);
    success = context.pointers_ok && message->rm_Result1 == 77;
    if (!success)
        fprintf(stderr, "private route failed: pointers=%d result1=%ld\n",
                context.pointers_ok, (long)message->rm_Result1);

    /* RXADDRSRC's arg0 is a resource pointer, not an argstring. */
    message->rm_Args[0] = 0;
    message->rm_Private1 = 0;
    message->rm_Private2 = 0;
    DeleteRexxMsg(message);
    DeleteMsgPort(helper_port);
    DeleteMsgPort(reply_port);
    return success ? 0 : 1;
}
