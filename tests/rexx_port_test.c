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
 * It also checks the thing rm_Stdin and rm_Stdout exist for: the receiver
 * writes to the stream that came with the message, and the bytes come out on
 * the *sender's* stream. That is how ADDRESS <port> output reaches the user,
 * and it is free on AmigaOS -- a BPTR FileHandle is valid in any task -- so
 * it is exactly the sort of thing that quietly does not happen here.
 *
 * The assertion that matters most is that the reply is the *same pointer* the
 * sender sent. sendrexxmsg.c asserts it, and Regina's sendandwait()
 * (amifuncs.c:601) replies to anything on its reply port that is not the
 * pointer it sent and goes straight back to waiting -- so a "reply" that is a
 * different message hangs the sender just as surely as no reply at all.
 */

#include "broker_client.h"
#include "broker_protocol.h"

#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <clib/rexxsyslib_protos.h>

/*
 * Declared here rather than reached through <proto/dos.h>. This file is built
 * with compat/aros-real/include first, for the port and message declarations,
 * and that tree carries its own thin proto/dos.h which wins the lookup and
 * hides these three -- the same shadowing compat/regina/include exists to
 * work around for Regina. Three prototypes are a smaller price here than
 * bending a test's include order around it.
 */
LONG Write(BPTR file, CONST_STRPTR buffer, LONG length);
BPTR Input(void);
BPTR Output(void);

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

/*
 * How many descriptors the broker is holding.
 *
 * Here because passing streams is exactly the kind of thing that works and
 * leaks: sendmsg() with SCM_RIGHTS duplicates a descriptor into the receiver
 * rather than handing this process's copy over, so a forwarder that assumes
 * otherwise keeps one per message. The broker is the process that never
 * exits, so it is the one that would run out.
 */
static long broker_descriptor_count(void)
{
    char report[AMIGA_BROKER_MAX_PAYLOAD];
    char path[64];
    const char *found;
    DIR *directory;
    long count = 0;

    if (native_broker_status(report, sizeof(report)) != 0)
        return -1;
    found = strstr(report, "\npid\t");
    if (!found)
        return -1;
    snprintf(path, sizeof(path), "/proc/%ld/fd",
             strtol(found + 5, NULL, 10));
    directory = opendir(path);
    if (!directory)
        return -1;
    while (readdir(directory))
        count++;
    closedir(directory);
    return count;
}

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

/* Written by the receiver, to the stream the sender sent with the message. */
static const char through_stream[] = "hello from the receiver\n";

#define MESSAGES 4

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

    /* More than one, because a descriptor leak shows as a difference between
       two steady states rather than in any single exchange. The last one is
       served after the sender has finished measuring, so that this process
       exiting -- which closes its two broker connections and drops the count
       by two -- cannot be mistaken for the thing being measured. */
    for (int served = 0; served < MESSAGES; served++) {
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
            memcmp((void *)message->rm_Args[0], argument,
                   ARGUMENT_LENGTH) != 0)
            _exit(8);

        /* The sender's own console, handed over with the message. This is
           what lets a script sent to another process print where the sender
           can see it. Written once: the later messages are there to compare
           descriptor counts, not to produce output. */
        if (!message->rm_Stdout)
            _exit(9);
        if (!served &&
            Write(message->rm_Stdout, (CONST_STRPTR)through_stream,
                  (LONG)strlen(through_stream)) != (LONG)strlen(through_stream))
            _exit(10);

        message->rm_Result1 = 0;
        message->rm_Result2 = (IPTR)CreateArgstring((UBYTE *)answer,
                                                    ANSWER_LENGTH);
        ReplyMsg((struct Message *)message);
    }
    DeletePort(port);
    _exit(0);
}

/* One complete exchange: build a message, send it with this process's own
   streams, wait for it to come back. Returns the reply, which must be the
   very message that was sent. */
static struct RexxMsg *exchange(struct MsgPort *remote,
                                struct MsgPort *reply_port)
{
    struct RexxMsg *message = CreateRexxMsg(reply_port, NULL,
                                            (UBYTE *)PORT_NAME);
    struct RexxMsg *reply;

    if (!message)
        return NULL;
    message->rm_Action = 42;
    message->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)argument,
                                                ARGUMENT_LENGTH);
    message->rm_Stdin = Input();
    message->rm_Stdout = Output();
    PutMsg(remote, (struct Message *)message);
    WaitPort(reply_port);
    reply = (struct RexxMsg *)GetMsg(reply_port);
    if (reply != message)
        return NULL;
    return reply;
}

int main(void)
{
    int ready[2];
    int captured[2];
    int saved_stdout;
    pid_t child;
    char byte;
    int status;
    char written[128];
    ssize_t written_length;
    struct MsgPort *remote;
    struct MsgPort *reply_port;
    struct RexxMsg *message;
    struct RexxMsg *reply;

    if (native_broker_ensure() != 0) {
        fprintf(stderr, "FAIL: no broker\n");
        return 1;
    }
    if (pipe(ready) != 0 || pipe(captured) != 0)
        return 1;
    /* Stand a pipe in for this process's stdout, so that what the receiver
       writes to the stream it was given can be read back here. The receiver
       is a separate process and gets the descriptor through the broker, so
       this has to be in place before the fork. */
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(captured[1], STDOUT_FILENO) < 0)
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

    /* The sender's own streams travel with the message. */
    message->rm_Stdin = Input();
    message->rm_Stdout = Output();

    PutMsg(remote, (struct Message *)message);
    WaitPort(reply_port);
    reply = (struct RexxMsg *)GetMsg(reply_port);

    /* Put the real stdout back before reporting anything. */
    fflush(stdout);
    (void)dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    close(captured[1]);
    written_length = read(captured[0], written, sizeof(written) - 1);
    if (written_length < 0)
        written_length = 0;
    written[written_length] = '\0';
    close(captured[0]);
    check(strcmp(written, through_stream) == 0,
          "what the receiver wrote came out on the sender's stream");

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

    /*
     * Two more exchanges, to compare the broker's descriptor count between
     * two steady states. Everything that opens a connection has already
     * opened it, so the only thing that could move the count is a stream
     * descriptor the broker forwarded and then kept.
     */
    {
        long before = -1;
        long after = -1;
        int round;

        /* Measured around the third exchange, with the receiver still alive
           and waiting for a fourth. Every connection that will be opened is
           open by now, so the only thing that could move the count is a
           stream descriptor the broker forwarded and then kept. */
        for (round = 2; round <= MESSAGES; round++) {
            struct RexxMsg *sent;

            if (round == 3)
                before = broker_descriptor_count();
            sent = exchange(remote, reply_port);
            check(sent != NULL, "a repeat exchange works");
            if (sent) {
                if (sent->rm_Result2)
                    DeleteArgstring((UBYTE *)sent->rm_Result2);
                if (sent->rm_Args[0])
                    DeleteArgstring((UBYTE *)sent->rm_Args[0]);
                DeleteRexxMsg(sent);
            }
            if (round == 3)
                after = broker_descriptor_count();
        }
        check(before > 0 && after > 0,
              "the broker's descriptor count can be read");
        check(before > 0 && after == before,
              "the broker keeps no descriptor from a message it forwarded");
    }

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
