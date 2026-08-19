/*
 * CreateNewProc() with NP_Entry, exercised the way Regina exercises it.
 *
 * The handshake below is Amiga_fork_exec()/StartCommand() from the AROS
 * Regina port's os_amiga.c, reduced to what ACE has to get right: the child
 * has to be a different AmigaDOS process from its parent, both directions of
 * Signal()/Wait() have to reach the intended one of them, and what the child
 * writes has to be visible to the parent afterwards.
 *
 * That last one is why an NP_Entry process cannot be a fork: the two halves
 * share childinfo, and a copied address space would leave the parent reading
 * its own untouched copy.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>

#define CHILD_RESULT 42

static struct {
    struct Task *parent;
    struct Task *child;
    LONG parent_signal;
    LONG child_signal;
    int result;
    volatile int entered;
} info;

static void child_entry(void)
{
    info.child = FindTask(NULL);
    info.child_signal = AllocSignal(-1);
    info.entered = 1;

    /* Tell the parent the ChildInfo is filled in, then wait for its go-ahead.
       Regina splits it this way so the parent can hand over data the child
       reads only after being released. */
    Signal(info.parent, 1UL << info.parent_signal);
    Wait(1UL << info.child_signal);
    FreeSignal(info.child_signal);

    info.result = CHILD_RESULT;
    Signal(info.parent, 1UL << info.parent_signal);
}

int main(void)
{
    struct Process *created;

    info.parent = FindTask(NULL);
    assert(info.parent != NULL);
    info.parent_signal = AllocSignal(-1);
    assert(info.parent_signal >= 0);

    created = CreateNewProcTags(NP_Entry, (IPTR)child_entry,
                                NP_Cli, TRUE,
                                TAG_DONE, (IPTR)0);
    assert(created != NULL);

    Wait(1UL << info.parent_signal);
    assert(info.entered);

    /* The whole point of a separate struct Process: FindTask(NULL) in the
       child must not answer with the task that created it, or the Signal()
       below would be the parent signalling itself and the child would wait
       for ever. */
    assert(info.child != NULL);
    assert(info.child != info.parent);
    assert(info.child == (struct Task *)created);
    assert(info.child_signal >= 0);

    Signal(info.child, 1UL << info.child_signal);
    Wait(1UL << info.parent_signal);

    /* Written by the child, read here: one address space, as Exec has. */
    assert(info.result == CHILD_RESULT);

    /* The parent is still the parent.  A thread-local identity that leaked
       would show up here as the child's. */
    assert(FindTask(NULL) == info.parent);

    /* Nothing to run is refused rather than started.  Neither NP_Entry nor
       NP_Seglist means there is no process to make. */
    assert(CreateNewProcTags(NP_Cli, TRUE, TAG_DONE, (IPTR)0) == NULL);

    /* NP_Seglist names code to load, which is SystemTagList()'s job here.
       Refused rather than ignored: a process that silently runs nothing is
       far harder to diagnose than a failure at the call. */
    assert(CreateNewProcTags(NP_Seglist, (IPTR)1, TAG_DONE, (IPTR)0) == NULL);

    printf("create-new-proc: ok\n");
    return 0;
}
