/*
 * rexxsyslib.library's argstring and RexxMsg surface.
 *
 * These are the allocation and lifecycle half of ARexx: the part that makes
 * argstrings and RexxMsgs and takes them apart again. They are deliberately
 * separate from the delivery half -- named public ports, cross-process
 * PutMsg/ReplyMsg and message ownership -- which is broker work and is not
 * here. Regina's engine reaches this file; nothing in it needs to know where
 * a message is eventually sent.
 *
 * Behaviour follows AROS's own workbench/libs/rexxsyslib sources function for
 * function, because these structures are an ABI that Regina and any ARexx
 * client both read directly. Where AROS reaches through its library base for
 * per-library state, ACE keeps the equivalent as file statics: there is one
 * of this library per process here, and no library base to hang it on.
 */

/* pthread_mutexattr_settype() and PTHREAD_MUTEX_RECURSIVE are POSIX rather
   than C11, and -std=c11 hides them without this. */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/libraries.h>
#include <rexx/rxslib.h>
#include <rexx/storage.h>

/*
 * The base OpenLibrary("rexxsyslib.library") hands back.
 *
 * ACE implements this library's functions as plain C, so nothing here or in
 * Regina dereferences the base -- on AROS it is the implicit argument of a
 * library call, and there are no library calls to make. It still has to
 * exist, because a caller checks it:
 *
 *     atsd->rexxsysbase = (struct RxsLib *)OpenLibrary("rexxsyslib.library", 44);
 *     if ( atsd->rexxsysbase == NULL )
 *        return 0;                            -- amifuncs.c:397
 *
 * That is Regina's init_amigaf(), and returning 0 there means Regina's whole
 * ARexx side -- its reply port, its "Regina Helper" task, its message
 * handling -- is never set up. In the standalone interpreter the failure was
 * invisible, because mt_notmt.c accumulates the init results with |= rather
 * than &= and so records success no matter what this returns; the library
 * build (mt_amigalib.c) uses &= and fails outright. Both were the same
 * missing name here.
 *
 * isreginamsg.c opens it too, and returns FALSE when it cannot -- so
 * IsReginaMsg() answered "not mine" for every message until this existed,
 * which is what RexxMast asks about each message it receives.
 */
static struct RxsLib ace_rexxsyslib_base;

/*
 * Reached through a weak declaration in src/native_dos.c, because this object
 * is not in every command's link and OpenLibrary() is. Linked in, the name
 * resolves; absent, OpenLibrary() answers NULL for it exactly as before.
 */
struct Library *ace_rexxsyslib_library_base(void)
{
    return &ace_rexxsyslib_base.rl_Node;
}

/*
 * What marks a Message as a RexxMsg. AROS compares ln_Name against one
 * private string the library holds, so identity is pointer equality against
 * that exact object rather than strcmp -- a message that merely happens to be
 * named "REXX" is not one. Kept here for the same reason and read the same
 * way.
 */
static char rexx_message_id[] = "RexxMsg";

UBYTE *CreateArgstring(UBYTE *string, ULONG length)
{
    /* Size of the header, the bytes, and the terminator. ra_Buff is declared
       8 bytes long as a placeholder, so it is subtracted back out -- this is
       AROS's own arithmetic, kept identical because ra_Size is what
       DeleteArgstring() later frees. */
    ULONG size = (ULONG)(sizeof(struct RexxArg) - 8 + length + 1);
    struct RexxArg *argument;
    ULONG hash = 0;
    ULONG index;

    if (!string)
        return NULL;
    argument = calloc(1, size);
    if (!argument)
        return NULL;
    argument->ra_Size = (LONG)size;
    argument->ra_Length = (UWORD)length;
    /* Both fields are documented as deprecated and are set only so that a
       client reading them on AmigaOS sees what it expects. */
    argument->ra_Deprecated1 = 1 << 1 | 1 << 2 | 1 << 6;
    for (index = 0; index < length; index++)
        hash += string[index];
    argument->ra_Deprecated2 = (UBYTE)(hash & 255);
    memcpy(argument->ra_Buff, string, length);
    argument->ra_Buff[length] = '\0';
    /* The caller is given the bytes, not the header: an argstring is usable
       as a plain C string, and its length lives behind the pointer. */
    return (UBYTE *)argument->ra_Buff;
}

static struct RexxArg *argstring_header(UBYTE *argstring)
{
    return (struct RexxArg *)(void *)(argstring -
                                      offsetof(struct RexxArg, ra_Buff));
}

void DeleteArgstring(UBYTE *argstring)
{
    if (!argstring)
        return;
    free(argstring_header(argstring));
}

ULONG LengthArgstring(UBYTE *argstring)
{
    if (!argstring)
        return 0;
    return argstring_header(argstring)->ra_Length;
}

struct RexxMsg *CreateRexxMsg(struct MsgPort *port, UBYTE *extension,
                              UBYTE *host)
{
    struct RexxMsg *message = calloc(1, sizeof(*message));

    if (!message)
        return NULL;
    message->rm_Node.mn_Node.ln_Type = NT_MESSAGE;
    message->rm_Node.mn_Node.ln_Name = rexx_message_id;
    message->rm_Node.mn_ReplyPort = port;
    message->rm_Node.mn_Length = sizeof(*message);
    message->rm_FileExt = (STRPTR)extension;
    message->rm_CommAddr = (STRPTR)host;
    return message;
}

void DeleteRexxMsg(struct RexxMsg *packet)
{
    if (!packet)
        return;
    /* Argstrings are not freed here. AROS does not free them either: the
       arguments belong to whoever filled them in, and ClearRexxMsg() is the
       call that releases them. Freeing them here would double-free every
       caller that follows the documented order. */
    free(packet);
}

void ClearRexxMsg(struct RexxMsg *msgptr, ULONG count)
{
    ULONG index;

    if (!msgptr)
        return;
    for (index = 0; index < count && index < 16; index++) {
        if (msgptr->rm_Args[index]) {
            DeleteArgstring(RXARG(msgptr, index));
            msgptr->rm_Args[index] = 0;
        }
    }
}

BOOL FillRexxMsg(struct RexxMsg *msgptr, ULONG count, ULONG mask)
{
    STRPTR arguments[16];
    char number[24];
    ULONG index;
    ULONG undo;

    if (!msgptr || count > 16)
        return FALSE;
    for (index = 0; index < count; index++) {
        /* A bit set in the mask says this slot currently holds an integer
           rather than a string pointer, and is to be spelled out. */
        if (mask & (1UL << index)) {
            snprintf(number, sizeof(number), "%ld",
                     (long)msgptr->rm_Args[index]);
            arguments[index] = (STRPTR)CreateArgstring((UBYTE *)number,
                                                       (ULONG)strlen(number));
        } else if (!msgptr->rm_Args[index]) {
            arguments[index] = NULL;
            continue;
        } else {
            UBYTE *text = RXARG(msgptr, index);

            arguments[index] = (STRPTR)CreateArgstring(text,
                                                       (ULONG)strlen((const char *)text));
        }
        if (!arguments[index]) {
            /* All or nothing: a half-converted message would leave the
               caller unable to tell which slots it now owns. */
            for (undo = 0; undo < index; undo++)
                if (arguments[undo])
                    DeleteArgstring((UBYTE *)arguments[undo]);
            return FALSE;
        }
    }
    memcpy(msgptr->rm_Args, arguments, count * sizeof(STRPTR));
    return TRUE;
}

BOOL IsRexxMsg(struct RexxMsg *msgptr)
{
    if (!msgptr)
        return FALSE;
    return msgptr->rm_Node.mn_Node.ln_Name == rexx_message_id ? TRUE : FALSE;
}

/*
 * AROS takes a semaphore in its library base. ACE has one of this library per
 * process and no base, so this is the same exclusion over the same state: the
 * argstring and RexxMsg allocators above, reachable from more than one thread
 * now that CreateNewProc() exists. Recursive because the documented use is a
 * lock held across a sequence of calls, and AROS's ObtainSemaphore() nests.
 */
static pthread_mutex_t rexx_base_lock;
static pthread_once_t rexx_base_lock_once = PTHREAD_ONCE_INIT;

static void rexx_base_lock_init(void)
{
    pthread_mutexattr_t attributes;

    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rexx_base_lock, &attributes);
    pthread_mutexattr_destroy(&attributes);
}

void LockRexxBase(ULONG resource)
{
    (void)resource;
    pthread_once(&rexx_base_lock_once, rexx_base_lock_init);
    pthread_mutex_lock(&rexx_base_lock);
}

void UnlockRexxBase(ULONG resource)
{
    (void)resource;
    pthread_once(&rexx_base_lock_once, rexx_base_lock_init);
    pthread_mutex_unlock(&rexx_base_lock);
}
