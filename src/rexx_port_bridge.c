#define _POSIX_C_SOURCE 200809L

/*
 * PutMsg() and ReplyMsg() across processes.
 *
 * Inside one process a port is a list and a message is a pointer onto it, so
 * PutMsg() is a list insert and ReplyMsg() puts the same pointer back. None of
 * that survives a process boundary, and the two sides need different things
 * from this file:
 *
 *   Sending. PutMsg() to a port another process owns has to serialise the
 *   message, hand it to the broker, and come back. The caller then waits on
 *   its reply port exactly as it always did. When the answer arrives -- on the
 *   delivery channel, on another thread -- the results are written into *the
 *   caller's own struct RexxMsg* and that message is put on its reply port.
 *   It must be that same pointer: sendrexxmsg.c asserts reply == msg, and
 *   Regina's sendandwait() (amifuncs.c:601) replies to anything on its reply
 *   port that is not the pointer it sent and goes back to waiting. So there is
 *   no out-of-band way to release a sender; the message it sent is the only
 *   thing it will accept back.
 *
 *   Receiving. A message pushed to this process is rebuilt as a real RexxMsg
 *   and queued on the local port with the ordinary PutMsg(), so WaitPort() and
 *   GetMsg() work without knowing anything about this. ReplyMsg() on such a
 *   message routes back through the broker instead of onto a reply port that
 *   does not exist here.
 *
 * Deliberately a separate object from aros_exec_runtime.c. That one is linked
 * into ace-console, which has no broker connection and no use for ARexx; the
 * hooks there are weak so this file is linked only where it is wanted.
 *
 * rm_Stdin and rm_Stdout travel with the message as descriptors. They are the
 * sender's own streams, and they are what makes a script sent to another
 * process write on the console of the process that sent it -- RexxMast adopts
 * them as the script's input and output when they are not BNULL. On AmigaOS
 * that costs nothing because a BPTR FileHandle is valid in any task; here the
 * descriptors are passed with SCM_RIGHTS and the receiver wraps what it is
 * given in a FILE of its own.
 */

#include "broker_client.h"
#include "broker_protocol.h"

#include "aros_exec_runtime.h"

/* native_dos.c: the host descriptor behind a DOS handle, for passing this
   process's own streams to another one. */
int ace_dos_handle_descriptor(BPTR handle);

#include <exec/ports.h>
#include <exec/nodes.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <clib/rexxsyslib_protos.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The wire form of a RexxMsg.
 *
 * Counted throughout, because an argstring is counted bytes that may contain
 * anything -- that is why the broker stopped measuring payloads with strlen().
 * A length of ACE_REXX_ABSENT distinguishes "this slot is empty" from "this
 * slot holds a zero-length string", which are different things to a Rexx
 * program.
 *
 * Same-host only, and only ever exchanged between two builds of ACE that
 * agree on the broker protocol version, so the fixed-width fields are written
 * in native order rather than byte-swapped.
 */
#define ACE_REXX_WIRE_MAGIC 0x52584d31u     /* "RXM1" */
#define ACE_REXX_ABSENT     0xffffffffu
/* rm_Args[0..15], then rm_Result2, rm_CommAddr, rm_FileExt. */
#define ACE_REXX_SLOTS      19
#define ACE_REXX_SLOT_RESULT2  16
#define ACE_REXX_SLOT_COMMADDR 17
#define ACE_REXX_SLOT_FILEEXT  18

struct ace_rexx_wire {
    uint32_t magic;
    uint32_t action;
    int32_t result1;
    uint32_t slots;
    uint32_t length[ACE_REXX_SLOTS];
};

/* A message this process sent and is waiting on. */
struct ace_rexx_sent {
    uint64_t message_id;
    struct RexxMsg *message;
    struct ace_rexx_sent *next;
};

/* A message this process was given and has not yet answered. */
struct ace_rexx_received {
    uint64_t message_id;
    struct RexxMsg *message;
    /* The streams this process was handed, wrapped for DOS. Closed when the
       message is answered: they are the sender's console, and holding them
       open would keep it from ever seeing end-of-file. */
    FILE *stdin_stream;
    FILE *stdout_stream;
    struct ace_rexx_received *next;
};

/* A local port whose name the broker published, so a delivery naming its id
   can be queued on the right one. */
struct ace_rexx_local_port {
    uint64_t broker_id;
    struct MsgPort *port;
    struct ace_rexx_local_port *next;
};

/*
 * What a message from another process names as its reply port.
 *
 * It cannot be the real one: that lives in the sender's memory. But it cannot
 * be NULL either -- RexxMast's StartFileSlave() does
 *
 *     process = (struct Process *)msg->rm_Node.mn_ReplyPort->mp_SigTask;
 *
 * before it looks at anything else, so a NULL reply port is a crash rather
 * than a missing feature.
 *
 * So a remote sender is represented as a Task rather than a Process, which is
 * both true here -- from this process it is not a DOS process anything can
 * see -- and exactly the case AmigaOS already handles: RexxMast checks
 * ln_Type == NT_PROCESS before reading pr_CIS, pr_COS, pr_CES and
 * pr_CurrentDir, and falls back to rm_Stdin and rm_Stdout when the sender is
 * a plain Task. Which is where the real streams are, because they were passed
 * with the message.
 *
 * Nothing is ever queued on this port: ReplyMsg() finds the message in the
 * received table and routes it through the broker before it looks at
 * mn_ReplyPort at all.
 */
static struct Task remote_sender_task;
static struct MsgPort remote_sender_port;
static pthread_once_t remote_sender_once = PTHREAD_ONCE_INIT;

static void remote_sender_init(void)
{
    remote_sender_task.tc_Node.ln_Type = NT_TASK;
    remote_sender_task.tc_Node.ln_Name = (char *)"ARexx sender";
    remote_sender_port.mp_Node.ln_Type = NT_MSGPORT;
    remote_sender_port.mp_Node.ln_Name = (char *)"ARexx sender";
    remote_sender_port.mp_SigTask = &remote_sender_task;
    NEWLIST(&remote_sender_port.mp_MsgList);
}

static pthread_mutex_t bridge_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ace_rexx_sent *sent_messages;
static struct ace_rexx_received *received_messages;
static struct ace_rexx_local_port *local_ports;
static int channel_ready;

static void bridge_handler(uint32_t operation, uint64_t message_id,
                           uint64_t port_id, const char *payload,
                           size_t payload_length, int stdin_fd, int stdout_fd,
                           void *context);

/*
 * Opens the delivery channel if it is not open yet.
 *
 * Both directions need it: a receiver to be given messages, a sender to be
 * given replies. native_broker_port_attach() is idempotent, so whichever of
 * the two happens first in a process opens it and the other joins.
 */
static int ensure_channel(void)
{
    uint64_t channel = 0;
    int ready;

    pthread_mutex_lock(&bridge_lock);
    ready = channel_ready;
    pthread_mutex_unlock(&bridge_lock);
    if (ready)
        return 0;
    if (native_broker_port_attach(bridge_handler, NULL, &channel) != 0)
        return -1;
    pthread_mutex_lock(&bridge_lock);
    channel_ready = 1;
    pthread_mutex_unlock(&bridge_lock);
    return 0;
}

static size_t slot_length(const void *pointer, int argstring)
{
    if (!pointer)
        return 0;
    return argstring ? LengthArgstring((UBYTE *)pointer)
                     : strlen((const char *)pointer);
}

static LONG rexx_action_code(LONG action)
{
    return action & RXCODEMASK;
}

/*
 * Flattens a RexxMsg. Returns the buffer and its length, or NULL.
 *
 * rm_PassPort, rm_Stdin and rm_Stdout are not carried: the first is a pointer
 * into the sender's memory and meaningless here, and the other two need
 * descriptor passing (1.5).
 */
static void *serialise(struct RexxMsg *message, size_t *out_length)
{
    struct ace_rexx_wire header;
    const void *slot_data[ACE_REXX_SLOTS];
    size_t slot_size[ACE_REXX_SLOTS];
    size_t total = sizeof(header);
    char *buffer;
    char *cursor;
    char numeric_length[32];
    LONG action = rexx_action_code(message->rm_Action);
    int index;

    memset(&header, 0, sizeof(header));
    header.magic = ACE_REXX_WIRE_MAGIC;
    header.action = (uint32_t)message->rm_Action;
    header.result1 = (int32_t)message->rm_Result1;
    header.slots = ACE_REXX_SLOTS;

    for (index = 0; index < 16; index++) {
        slot_data[index] = (const void *)message->rm_Args[index];
        if ((action == RXADDCON && index <= 2) ||
            (action == RXREMCON && index == 0))
            slot_size[index] = 0;
        else
            slot_size[index] = slot_length(slot_data[index], 1);
    }

    /* SETCLIP is the one public resource action whose wire arguments are not
       all argstrings. Regina sends the name as a C string, the clipboard as
       counted bytes, and the byte count as an IPTR. Keep that contract here;
       treating rm_Args[2] as an argstring would dereference the integer as a
       pointer, while strlen() would truncate clipboard data at NUL. */
    if (action == RXADDCON) {
        slot_data[0] = (const void *)message->rm_Args[0];
        slot_size[0] = slot_length(slot_data[0], 0);
        slot_data[1] = (const void *)message->rm_Args[1];
        slot_size[1] = message->rm_Args[1]
                           ? (size_t)message->rm_Args[2]
                           : 0;
        snprintf(numeric_length, sizeof(numeric_length), "%llu",
                 (unsigned long long)message->rm_Args[2]);
        slot_data[2] = numeric_length;
        slot_size[2] = strlen(numeric_length);
    } else if (action == RXREMCON) {
        slot_data[0] = (const void *)message->rm_Args[0];
        slot_size[0] = slot_length(slot_data[0], 0);
    }
    slot_data[ACE_REXX_SLOT_RESULT2] = (const void *)message->rm_Result2;
    slot_size[ACE_REXX_SLOT_RESULT2] =
        slot_length(slot_data[ACE_REXX_SLOT_RESULT2], 1);
    slot_data[ACE_REXX_SLOT_COMMADDR] = message->rm_CommAddr;
    slot_size[ACE_REXX_SLOT_COMMADDR] =
        slot_length(slot_data[ACE_REXX_SLOT_COMMADDR], 0);
    slot_data[ACE_REXX_SLOT_FILEEXT] = message->rm_FileExt;
    slot_size[ACE_REXX_SLOT_FILEEXT] =
        slot_length(slot_data[ACE_REXX_SLOT_FILEEXT], 0);

    for (index = 0; index < ACE_REXX_SLOTS; index++) {
        if (!slot_data[index]) {
            header.length[index] = ACE_REXX_ABSENT;
            continue;
        }
        header.length[index] = (uint32_t)slot_size[index];
        total += slot_size[index];
    }
    if (total > AMIGA_BROKER_MAX_PAYLOAD)
        return NULL;

    buffer = malloc(total);
    if (!buffer)
        return NULL;
    memcpy(buffer, &header, sizeof(header));
    cursor = buffer + sizeof(header);
    for (index = 0; index < ACE_REXX_SLOTS; index++) {
        if (!slot_data[index] || !slot_size[index])
            continue;
        memcpy(cursor, slot_data[index], slot_size[index]);
        cursor += slot_size[index];
    }
    *out_length = total;
    return buffer;
}

/*
 * Rebuilds the slots of a RexxMsg from a wire payload.
 *
 * Every string slot is recreated with CreateArgstring() in this process's own memory.
 * Handing back a pointer into the payload would be a pointer into a buffer the
 * reader thread frees, and the receiver is entitled to DeleteArgstring() what
 * it is given.
 */
static int deserialise(struct RexxMsg *message, const char *payload,
                       size_t payload_length, int with_results)
{
    struct ace_rexx_wire header;
    const char *cursor;
    size_t remaining;
    LONG action;
    int index;

    if (payload_length < sizeof(header))
        return -1;
    memcpy(&header, payload, sizeof(header));
    if (header.magic != ACE_REXX_WIRE_MAGIC ||
        header.slots != ACE_REXX_SLOTS)
        return -1;
    cursor = payload + sizeof(header);
    remaining = payload_length - sizeof(header);

    if (with_results) {
        message->rm_Action = (LONG)header.action;
        message->rm_Result1 = (LONG)header.result1;
    }
    action = rexx_action_code(with_results ? (LONG)header.action
                                           : message->rm_Action);
    for (index = 0; index < ACE_REXX_SLOTS; index++) {
        uint32_t length = header.length[index];
        UBYTE *copy = NULL;

        if (length == ACE_REXX_ABSENT)
            continue;
        if (length > remaining)
            return -1;
        if (!with_results && index != ACE_REXX_SLOT_RESULT2) {
            cursor += length;
            remaining -= length;
            continue;
        }
        if (index == 2 && action == RXADDCON) {
            char number[32];
            char *end;
            unsigned long long value;

            if (length == 0 || length >= sizeof(number))
                return -1;
            memcpy(number, cursor, length);
            number[length] = '\0';
            errno = 0;
            value = strtoull(number, &end, 10);
            if (errno != 0 || end == number || *end != '\0' ||
                value > (unsigned long long)(IPTR)-1)
                return -1;
            cursor += length;
            remaining -= length;
            message->rm_Args[index] = (IPTR)value;
            continue;
        }
        copy = CreateArgstring((UBYTE *)cursor, length);
        if (!copy)
            return -1;
        cursor += length;
        remaining -= length;
        if (index < 16)
            message->rm_Args[index] = (IPTR)copy;
        else if (index == ACE_REXX_SLOT_RESULT2)
            message->rm_Result2 = (IPTR)copy;
        else if (index == ACE_REXX_SLOT_COMMADDR)
            message->rm_CommAddr = (STRPTR)copy;
        else
            message->rm_FileExt = (STRPTR)copy;
    }
    return 0;
}

static void clear_received_message(struct RexxMsg *message)
{
    if (!message)
        return;
    /* RXADDCON carries its byte count in rm_Args[2], not in an argstring.
       ClearRexxMsg() quite correctly assumes ordinary argstrings, so remove
       this action-specific scalar before handing the remaining slots to it. */
    if (rexx_action_code(message->rm_Action) == RXADDCON)
        message->rm_Args[2] = 0;
    ClearRexxMsg(message, 16);
    if (message->rm_Result2) {
        DeleteArgstring((UBYTE *)message->rm_Result2);
        message->rm_Result2 = 0;
    }
    if (message->rm_CommAddr) {
        DeleteArgstring((UBYTE *)message->rm_CommAddr);
        message->rm_CommAddr = NULL;
    }
    if (message->rm_FileExt) {
        DeleteArgstring((UBYTE *)message->rm_FileExt);
        message->rm_FileExt = NULL;
    }
}

/*
 * Wraps a passed descriptor for DOS, in whichever direction it actually
 * supports.
 *
 * Not simply "r" for stdin and "w" for stdout: listen4msg.c, from the AROS
 * tree and unmodified, does Write(msg->rm_Stdin, "Hello\n", 6). That is not
 * a mistake. On AmigaOS rm_Stdin is a handle on the sender's console and a
 * CON: handle is read/write, so writing to the input stream puts text on the
 * sender's screen. Opening it "r" here would make that silently fail.
 *
 * So the mode comes from what the descriptor can do rather than from which
 * slot it arrived in, and a console -- a tty opened O_RDWR -- behaves as it
 * does on the Amiga.
 */
static FILE *wrap_descriptor(int descriptor)
{
    int flags = fcntl(descriptor, F_GETFL);
    const char *mode;

    if (flags < 0)
        return NULL;
    switch (flags & O_ACCMODE) {
    case O_RDONLY:
        mode = "r";
        break;
    case O_WRONLY:
        mode = "w";
        break;
    default:
        mode = "r+";
        break;
    }
    return fdopen(descriptor, mode);
}

/* Hands a message back to whoever sent it, on the port it is waiting on. */
static void release_sender(struct RexxMsg *message)
{
    struct MsgPort *reply_port = message->rm_Node.mn_ReplyPort;

    message->rm_Node.mn_Node.ln_Type = NT_REPLYMSG;
    if (reply_port)
        PutMsg(reply_port, (struct Message *)message);
}

static struct RexxMsg *take_sent(uint64_t message_id)
{
    struct ace_rexx_sent **cursor;
    struct RexxMsg *message = NULL;

    pthread_mutex_lock(&bridge_lock);
    cursor = &sent_messages;
    while (*cursor && (*cursor)->message_id != message_id)
        cursor = &(*cursor)->next;
    if (*cursor) {
        struct ace_rexx_sent *entry = *cursor;

        message = entry->message;
        *cursor = entry->next;
        free(entry);
    }
    pthread_mutex_unlock(&bridge_lock);
    return message;
}

/*
 * Everything the broker pushes at this process, on the channel's reader
 * thread. Nothing here may block: a sender is waiting on the other side of
 * every one of these.
 */
static void bridge_handler(uint32_t operation, uint64_t message_id,
                           uint64_t port_id, const char *payload,
                           size_t payload_length, int stdin_fd, int stdout_fd,
                           void *context)
{
    (void)context;

    /* Descriptors only ever accompany a delivery. Anything else that arrives
       with them is malformed, and they are ours to close either way. */
    if (operation != AMIGA_BROKER_PORT_PUT) {
        if (stdin_fd >= 0)
            close(stdin_fd);
        if (stdout_fd >= 0)
            close(stdout_fd);
        stdin_fd = -1;
        stdout_fd = -1;
    }

    if (operation == AMIGA_BROKER_PORT_PUT) {
        struct ace_rexx_local_port *entry;
        struct ace_rexx_received *record;
        struct MsgPort *target = NULL;
        struct RexxMsg *message;

        pthread_mutex_lock(&bridge_lock);
        for (entry = local_ports; entry; entry = entry->next) {
            if (entry->broker_id == port_id) {
                target = entry->port;
                break;
            }
        }
        pthread_mutex_unlock(&bridge_lock);
        if (!target) {
            if (stdin_fd >= 0)
                close(stdin_fd);
            if (stdout_fd >= 0)
                close(stdout_fd);
            return;
        }
        message = CreateRexxMsg(NULL, NULL, NULL);
        if (!message) {
            if (stdin_fd >= 0)
                close(stdin_fd);
            if (stdout_fd >= 0)
                close(stdout_fd);
            return;
        }
        if (deserialise(message, payload, payload_length, 1) != 0) {
            clear_received_message(message);
            DeleteRexxMsg(message);
            if (stdin_fd >= 0)
                close(stdin_fd);
            if (stdout_fd >= 0)
                close(stdout_fd);
            return;
        }
        /* A stand-in for the sender's reply port; see remote_sender_port.
           Not NULL, because a receiver is entitled to follow it. */
        (void)pthread_once(&remote_sender_once, remote_sender_init);
        message->rm_Node.mn_ReplyPort = &remote_sender_port;
        record = calloc(1, sizeof(*record));
        if (!record) {
            clear_received_message(message);
            DeleteRexxMsg(message);
            if (stdin_fd >= 0)
                close(stdin_fd);
            if (stdout_fd >= 0)
                close(stdout_fd);
            return;
        }
        record->message_id = message_id;
        record->message = message;
        /* A BPTR is a FILE * here, so wrapping the descriptor is all it takes
           for Read() and Write() to reach the sender's console. */
        if (stdin_fd >= 0) {
            record->stdin_stream = wrap_descriptor(stdin_fd);
            if (record->stdin_stream)
                message->rm_Stdin = (BPTR)record->stdin_stream;
            else
                close(stdin_fd);
        }
        if (stdout_fd >= 0) {
            record->stdout_stream = wrap_descriptor(stdout_fd);
            if (record->stdout_stream)
                message->rm_Stdout = (BPTR)record->stdout_stream;
            else
                close(stdout_fd);
        }
        pthread_mutex_lock(&bridge_lock);
        record->next = received_messages;
        received_messages = record;
        pthread_mutex_unlock(&bridge_lock);
        /* The ordinary local path from here: it queues the message and wakes
           whoever is in WaitPort(). */
        PutMsg(target, (struct Message *)message);
        return;
    }

    if (operation == AMIGA_BROKER_PORT_REPLY) {
        struct RexxMsg *message = take_sent(message_id);

        if (!message)
            return;
        /* Results only. The arguments still belong to the sender, which will
           free them itself; overwriting them here would leak the originals. */
        message->rm_Result1 = 0;
        message->rm_Result2 = 0;
        if (deserialise(message, payload, payload_length, 0) != 0) {
            message->rm_Result1 = RC_FATAL;
            message->rm_Result2 = 0;
        } else {
            struct ace_rexx_wire header;

            memcpy(&header, payload, sizeof(header));
            message->rm_Result1 = (LONG)header.result1;
        }
        release_sender(message);
        return;
    }

    if (operation == AMIGA_BROKER_PORT_ABANDONED) {
        struct RexxMsg *message = take_sent(message_id);

        if (!message)
            return;
        /*
         * Nobody answered, and nobody ever will. The broker keeps that
         * distinct from a reply, but the sender is in WaitPort() and the only
         * thing that releases it is its own message coming back -- so the
         * distinction has to be turned into the one vocabulary ARexx has here.
         * RC_FATAL is what a Rexx program then sees in RC.
         */
        message->rm_Result1 = RC_FATAL;
        message->rm_Result2 = 0;
        release_sender(message);
        return;
    }
}

/*
 * PutMsg()'s hook. Returns 1 when the port belongs to another process and the
 * message has been dealt with, 0 when it is an ordinary local port.
 */
int ace_rexx_port_forward(struct MsgPort *port, struct Message *message)
{
    uint64_t remote_id = ace_aros_runtime_remote_port_id(port);
    struct RexxMsg *rexx = (struct RexxMsg *)message;
    struct ace_rexx_sent *record;
    uint64_t message_id = 0;
    void *payload;
    size_t length = 0;

    if (!remote_id || !message)
        return 0;
    /* Only a RexxMsg can cross: an ordinary Message is a bare header plus
       whatever the sender allocated behind it, and this side cannot know its
       shape. Left to the local path, which queues it on the stand-in port
       where it will sit unread -- the same as sending to a port nobody is
       serving. */
    if (!IsRexxMsg(rexx))
        return 0;
    if (ensure_channel() != 0)
        return 0;

    /* Regina-private actions contain rm_Private1/rm_Private2 pointers to a
       helper port and TSD in the sender process. They have no meaningful wire
       representation yet. Reject them here, before ordinary serialization
       mistakes can interpret a private pointer as an argstring and before a
       message is left waiting on a remote stand-in port. */
    if ((uint32_t)rexx_action_code(rexx->rm_Action) >= (uint32_t)RXADDRSRC) {
        rexx->rm_Result1 = RC_ERROR;
        rexx->rm_Result2 = 0;
        release_sender(rexx);
        return 1;
    }

    payload = serialise(rexx, &length);
    if (!payload)
        return 0;

    /* Recorded before the send, because the reply can arrive on the reader
       thread before native_broker_port_put() has returned here. */
    record = calloc(1, sizeof(*record));
    if (!record) {
        free(payload);
        return 0;
    }
    record->message = rexx;
    pthread_mutex_lock(&bridge_lock);
    record->next = sent_messages;
    sent_messages = record;
    pthread_mutex_unlock(&bridge_lock);

    /* The stand-in port carries the name it stands in for, which is what
       PORT_PUT wants: the broker finds the owner and delivers in one step, so
       the port cannot go away between the two. */
    if (native_broker_port_put((const char *)port->mp_Node.ln_Name,
                               payload, length,
                               ace_dos_handle_descriptor(rexx->rm_Stdin),
                               ace_dos_handle_descriptor(rexx->rm_Stdout),
                               &message_id) != 0) {
        int failed = errno;

        free(payload);
        pthread_mutex_lock(&bridge_lock);
        {
            struct ace_rexx_sent **cursor = &sent_messages;

            while (*cursor && *cursor != record)
                cursor = &(*cursor)->next;
            if (*cursor)
                *cursor = record->next;
        }
        pthread_mutex_unlock(&bridge_lock);
        free(record);
        (void)failed;
        /* The send failed, so nothing will ever reply. Release the caller
           now rather than letting it wait for a message already lost --
           ESRCH here is the ordinary "no such port", since nothing starts
           RexxMast on demand. */
        rexx->rm_Result1 = RC_FATAL;
        rexx->rm_Result2 = 0;
        release_sender(rexx);
        return 1;
    }
    pthread_mutex_lock(&bridge_lock);
    record->message_id = message_id;
    pthread_mutex_unlock(&bridge_lock);
    free(payload);
    return 1;
}

/*
 * ReplyMsg()'s hook. Returns 1 when the message came from another process and
 * the answer has been routed back to it.
 */
int ace_rexx_port_reply(struct Message *message)
{
    struct ace_rexx_received **cursor;
    struct ace_rexx_received *entry = NULL;
    struct RexxMsg *rexx = (struct RexxMsg *)message;
    void *payload;
    size_t length = 0;

    if (!message)
        return 0;
    pthread_mutex_lock(&bridge_lock);
    cursor = &received_messages;
    while (*cursor && (*cursor)->message != rexx)
        cursor = &(*cursor)->next;
    if (*cursor) {
        entry = *cursor;
        *cursor = entry->next;
    }
    pthread_mutex_unlock(&bridge_lock);
    if (!entry)
        return 0;

    payload = serialise(rexx, &length);
    if (payload) {
        (void)native_broker_port_reply(entry->message_id, payload, length);
        free(payload);
    } else {
        (void)native_broker_port_reply(entry->message_id, NULL, 0);
    }
    /* The sender's streams go back with the answer: anything written to them
       has been written, and holding them open would leave the sender's
       console with a reader or writer it no longer has. */
    if (entry->stdout_stream)
        fclose(entry->stdout_stream);
    if (entry->stdin_stream)
        fclose(entry->stdin_stream);
    rexx->rm_Stdin = BNULL;
    rexx->rm_Stdout = BNULL;
    free(entry);
    /* This message was made here, on delivery, and the receiver is done with
       it. Its argstrings belong to the receiver, so release every rebuilt
       slot before freeing the message itself. */
    clear_received_message(rexx);
    DeleteRexxMsg(rexx);
    return 1;
}

/* Called when CreatePort() has published a name, so a delivery naming the
   broker's id for it can be queued on the right local port. */
void ace_rexx_port_published(struct MsgPort *port, uint64_t broker_id)
{
    struct ace_rexx_local_port *entry;

    if (!port || !broker_id)
        return;
    entry = calloc(1, sizeof(*entry));
    if (!entry)
        return;
    entry->broker_id = broker_id;
    entry->port = port;
    pthread_mutex_lock(&bridge_lock);
    entry->next = local_ports;
    local_ports = entry;
    pthread_mutex_unlock(&bridge_lock);
    /* A published port is a port other processes can send to, so this is the
       moment the channel has to exist. */
    (void)ensure_channel();
}

void ace_rexx_port_unpublished(uint64_t broker_id)
{
    struct ace_rexx_local_port **cursor;

    if (!broker_id)
        return;
    pthread_mutex_lock(&bridge_lock);
    cursor = &local_ports;
    while (*cursor && (*cursor)->broker_id != broker_id)
        cursor = &(*cursor)->next;
    if (*cursor) {
        struct ace_rexx_local_port *entry = *cursor;

        *cursor = entry->next;
        free(entry);
    }
    pthread_mutex_unlock(&bridge_lock);
}
