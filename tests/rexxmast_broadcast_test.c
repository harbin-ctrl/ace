#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <exec/lists.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <rexx/errors.h>
#include <rexx/rxslib.h>
#include <rexx/storage.h>
#include <clib/rexxsyslib_protos.h>

#define RESOURCE_NAME "ACE.TEST.BROADCAST.LIB"

static struct RexxRsrc *find_library(const char *name)
{
    struct Node *node;

    for (node = RexxSysBase->rl_LibList.lh_Head;
         node && node->ln_Succ; node = node->ln_Succ) {
        if (node->ln_Name && strcmp(node->ln_Name, name) == 0)
            return (struct RexxRsrc *)node;
    }
    return NULL;
}

static int wait_for_broadcast(void)
{
    struct RexxRsrc *resource = NULL;

    for (int attempt = 0; attempt < 200; attempt++) {
        LockRexxBase(0);
        resource = find_library(RESOURCE_NAME);
        if (resource && resource->rr_Node.ln_Pri == 7 &&
            resource->rr_Arg1 == 12 && resource->rr_Arg2 == 3) {
            UnlockRexxBase(0);
            return 0;
        }
        UnlockRexxBase(0);
        struct timespec delay = { 0, 10000000L };
        nanosleep(&delay, NULL);
    }
    return 1;
}

static int child_process(int ready_fd)
{
    struct MsgPort *remote = FindPort("REXX");
    struct MsgPort *reply_port = CreateMsgPort();
    struct MsgPort *helper_port = CreateMsgPort();
    struct RexxMsg *message;
    int tsd_marker = 0;
    char ready = 'F';

    if (!remote || !reply_port || !helper_port)
        goto done;
    message = CreateRexxMsg(reply_port, NULL, NULL);
    if (!message)
        goto done;
    /* RXCHECKMSG is a private action, but RexxMast answers it locally after
       recognizing the proxy. This warms the child's delivery channel before
       the parent causes the resource broadcast. */
    message->rm_Private1 = (IPTR)helper_port;
    message->rm_Private2 = (IPTR)&tsd_marker;
    message->rm_Action = RXCHECKMSG;
    PutMsg(remote, (struct Message *)message);
    if (WaitPort(reply_port) != (struct Message *)message ||
        message->rm_Result1 != RC_OK) {
        DeleteRexxMsg(message);
        goto done;
    }
    DeleteRexxMsg(message);
    DeleteMsgPort(helper_port);
    DeleteMsgPort(reply_port);
    ready = 'R';
    (void)write(ready_fd, &ready, 1);
    return wait_for_broadcast();

done:
    (void)write(ready_fd, &ready, 1);
    return 1;
}

int main(void)
{
    int ready_pipe[2];
    pid_t child;
    char ready;
    struct MsgPort *remote;
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    const char *arguments[] = { RESOURCE_NAME, "7", "12", "3" };
    int child_status;
    int success = 0;

    if (pipe(ready_pipe) != 0)
        return 1;
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        close(ready_pipe[0]);
        _exit(child_process(ready_pipe[1]));
    }
    close(ready_pipe[1]);
    if (read(ready_pipe[0], &ready, 1) != 1 || ready != 'R')
        goto wait_child;

    remote = FindPort("REXX");
    reply_port = CreateMsgPort();
    message = reply_port ? CreateRexxMsg(reply_port, NULL, NULL) : NULL;
    if (!remote || !message)
        goto wait_child;
    message->rm_Action = RXADDLIB;
    for (size_t index = 0; index < 4; index++)
        message->rm_Args[index] =
            (IPTR)CreateArgstring((UBYTE *)arguments[index],
                                   strlen(arguments[index]));
    PutMsg(remote, (struct Message *)message);
    if (WaitPort(reply_port) == (struct Message *)message &&
        message->rm_Result1 == RC_OK)
        success = 1;
    ClearRexxMsg(message, 4);
    DeleteRexxMsg(message);
    DeleteMsgPort(reply_port);

wait_child:
    close(ready_pipe[0]);
    waitpid(child, &child_status, 0);
    if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0)
        success = 0;
    if (!success)
        fprintf(stderr, "resource broadcast was not observed by the child\n");
    return success ? 0 : 1;
}
