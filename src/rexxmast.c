/*
 * ACE's Phase 1 RexxMast entry point.
 *
 * The server/resource code remains the upstream Regina implementation.  The
 * one part that cannot survive a host process boundary is its StartFile()
 * helper: AmigaOS passes a RexxMsg pointer to a second copy of RexxMast,
 * while ACE's SystemTags() forks and execs.  This adapter keeps the pointer in
 * the current address space and runs StartFileSlave() as an ACE process
 * thread instead.  The included source stays vendored and unmodified.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <clib/regina_protos.h>
#include <rexx/storage.h>

static pthread_mutex_t rexxmast_job_lock = PTHREAD_MUTEX_INITIALIZER;
struct rexxmast_job {
    struct RexxMsg *message;
    struct rexxmast_job *next;
};
static struct rexxmast_job *rexxmast_job_head;
static struct rexxmast_job *rexxmast_job_tail;
static _Thread_local APIRET rexxmast_api_result;
static _Thread_local SHORT rexxmast_script_return_code;
static _Thread_local int rexxmast_started;

static void rexxmast_startfile_entry(void);
static int rexxmast_start_job(struct RexxMsg *message);

/* A private Regina message arrives in RexxMast with a local proxy port in
   rm_Private1. It is a valid server message even though Regina's own marker
   cannot recognize it: the proxy is how ACE turns the later PutMsg() into a
   second broker hop back to the originating process. */
extern int ace_rexxmast_private_message(struct RexxMsg *message);
extern BOOL IsReginaMsg(struct RexxMsg *message);
extern void ace_rexx_broadcast_resource_reply(struct RexxMsg *message);

static BOOL ace_rexxmast_is_regina_msg(struct RexxMsg *message)
{
    if (ace_rexxmast_private_message(message))
        return TRUE;
    return IsReginaMsg(message);
}

static void ace_rexxmast_reply_msg(struct Message *message)
{
    ace_rexx_broadcast_resource_reply((struct RexxMsg *)message);
    ReplyMsg(message);
}

/* Regina separates the API result from the script's RETURN value. The
   original RexxMast source uses one USHORT for both, which is correct for the
   Amiga Regina interface it was written against but would discard rm_Result2
   with ACE's Regina library whenever a script returns a number. Preserve the
   script value for the message while presenting the upstream helper with the
   API status it expects. */
static APIRET ace_rexxmast_rexx_start(LONG argcount, PRXSTRING arglist,
                                      PCSZ progname, PRXSTRING instore,
                                      PCSZ envname, LONG calltype,
                                      PRXSYSEXIT exits, PSHORT return_code,
                                      PRXSTRING result)
{
    SHORT script_return_code = 0;
    APIRET api_result = RexxStart(argcount, arglist, progname, instore,
                                  envname, calltype, exits,
                                  &script_return_code, result);

    rexxmast_started = 1;
    rexxmast_api_result = api_result;
    rexxmast_script_return_code = script_return_code;
    if (return_code)
        *return_code = (SHORT)api_result;
    return api_result;
}

/* ACE has one native DOS output selection. RexxMast only needs these symbols
   to bracket a script invocation; the message's stdin/stdout are selected by
   the real SelectInput()/SelectOutput() calls in StartFileSlave(). */
BPTR SelectErrorOutput(BPTR handle)
{
    return handle;
}

void updatestdio(void)
{
    fflush(stdout);
    fflush(stderr);
}

/* RexxMast's resource actions are Phase 2, but their upstream helper still
   has to link in Phase 1. Keep the Exec ordering contract available without
   widening the general Exec compatibility surface yet. */
void Enqueue(struct List *list, struct Node *node)
{
    struct Node *cursor;
    struct Node *predecessor = NULL;

    if (!list || !node)
        return;
    for (cursor = list->lh_Head; cursor && cursor->ln_Succ;
         cursor = cursor->ln_Succ) {
        if (node->ln_Pri > cursor->ln_Pri)
            break;
        predecessor = cursor;
    }
    if (!predecessor) {
        node->ln_Succ = list->lh_Head;
        node->ln_Pred = (struct Node *)list;
        list->lh_Head->ln_Pred = node;
        list->lh_Head = node;
    } else {
        node->ln_Succ = predecessor->ln_Succ;
        node->ln_Pred = predecessor;
        predecessor->ln_Succ->ln_Pred = node;
        predecessor->ln_Succ = node;
    }
}

/* This is the SystemTags() call made by the unmodified StartFile(). Its
   command string contains the RexxMsg address solely because that is how the
   Amiga implementation hands the message to its slave. Here the address is
   still valid: the slave is an ACE host thread in this process. */
static LONG ace_rexxmast_system_tags(CONST_STRPTR command, ...)
{
    const char *address;
    void *pointer = NULL;

    address = strrchr(command ? command : "", ' ');
    if (!address || sscanf(address + 1, "%p", &pointer) != 1 || !pointer)
        return -1;
    return rexxmast_start_job((struct RexxMsg *)pointer) == 0 ? 0 : -1;
}

/* Rename the imported main and SystemTags call, then provide a direct server
   entry point. The upstream main creates a second server and waits for a
   break signal; ACE already has a process for this executable, so the server
   itself is the process and RXCLOSE can return from it normally. */
#define main ace_upstream_rexxmast_main
#define SystemTags ace_rexxmast_system_tags
#define RexxStart ace_rexxmast_rexx_start
#define IsReginaMsg ace_rexxmast_is_regina_msg
#define ReplyMsg ace_rexxmast_reply_msg
/* The imported main is not called, but its unused Amiga stack-size probe
   still has to compile. ACE's Process intentionally has no pr_StackSize; the
   value is ignored by CreateNewProcTags here, so use an existing process
   field solely for that dead expression. */
#define pr_StackSize pr_CIS
#include "../third_party/regina/rexxmast/RexxMast.c"
#undef pr_StackSize
#undef RexxStart
#undef IsReginaMsg
#undef ReplyMsg
#undef SystemTags
#undef main

static void rexxmast_startfile_entry(void)
{
    struct rexxmast_job *job;
    struct RexxMsg *message;

    pthread_mutex_lock(&rexxmast_job_lock);
    job = rexxmast_job_head;
    if (job) {
        rexxmast_job_head = job->next;
        if (!rexxmast_job_head)
            rexxmast_job_tail = NULL;
    }
    pthread_mutex_unlock(&rexxmast_job_lock);
    message = job ? job->message : NULL;
    free(job);
    if (!message)
        return;
    rexxmast_api_result = 0;
    rexxmast_script_return_code = 0;
    rexxmast_started = 0;
    StartFileSlave(message);
    /* The upstream failure path uses small numeric error identifiers in
       rm_Result2. ACE's cross-process bridge treats a nonzero result there
       as an argstring, so do not let a numeric sentinel become a bogus
       pointer. rm_Result1 carries the actionable failure code in Phase 1. */
    if (message->rm_Result2 != 0 && message->rm_Result2 < 4096)
        message->rm_Result2 = 0;
    if (rexxmast_started && rexxmast_api_result == 0)
        message->rm_Result1 = rexxmast_script_return_code;
    ReplyMsg((struct Message *)message);
}

static int rexxmast_start_job(struct RexxMsg *message)
{
    struct rexxmast_job *job;
    struct rexxmast_job **cursor;
    struct rexxmast_job *previous_tail;
    struct Process *process;

    job = malloc(sizeof(*job));
    if (!job)
        return -1;
    job->message = message;
    job->next = NULL;
    pthread_mutex_lock(&rexxmast_job_lock);
    previous_tail = rexxmast_job_tail;
    if (rexxmast_job_tail)
        rexxmast_job_tail->next = job;
    else
        rexxmast_job_head = job;
    rexxmast_job_tail = job;

    /* Hold the lock while creating the thread. The new entry point cannot
       consume this job until the enqueue either succeeds or is rolled back,
       so a failed CreateNewProcTags() cannot strand a message in the queue. */
    process = CreateNewProcTags(NP_Entry, (IPTR)rexxmast_startfile_entry,
                                NP_Name, (IPTR)"RexxMast worker",
                                TAG_DONE, (IPTR)0);
    if (process) {
        pthread_mutex_unlock(&rexxmast_job_lock);
        return 0;
    }

    cursor = &rexxmast_job_head;
    while (*cursor && *cursor != job)
        cursor = &(*cursor)->next;
    if (*cursor) {
        *cursor = job->next;
        if (rexxmast_job_tail == job)
            rexxmast_job_tail = previous_tail;
    }
    pthread_mutex_unlock(&rexxmast_job_lock);
    free(job);
    return -1;
}

int main(void)
{
    server_process();
    return 0;
}
