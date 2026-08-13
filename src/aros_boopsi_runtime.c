/*
 * Host services for the real AROS BOOPSI sources.
 *
 * ACE compiles rom/intuition/{rootclass,makeclass,freeclass,addclass,
 * removeclass,findclass,newobjecta,disposeobject,setattrsa,getattr,
 * nextobject}.c and compiler/alib/{domethod,dosupermethod,coercemethod}.c
 * unchanged.  Those sources call Exec for memory, pools, semaphores and list
 * handling, and expect a library base holding the class list.  This file is
 * that seam and nothing more: no part of the class, object or dispatch
 * behaviour lives here.
 */

#include "aros_boopsi_runtime.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

/* rootclass.c defines this through AROS_UFH3; see compat aros/asmcall.h. */
extern IPTR rootDispatcher(Class *cl, Object *o, Msg msg);

/*
 * struct IntIntuitionBase and the extern declaration of IntuitionBase come
 * from the forced include, ace_boopsi_intern.h, so the AROS sources and this
 * one cannot disagree about the layout.  This is the single definition.
 */
struct IntIntuitionBase *IntuitionBase;

/* -------------------------------------------------------------------------
 * Memory
 * ---------------------------------------------------------------------- */

/*
 * AllocMem()/FreeMem() carry the allocation size at the call site the way
 * AmigaOS does, so the host allocator only has to honour MEMF_CLEAR.
 */
APTR AllocMem(ULONG byteSize, ULONG requirements)
{
    void *memory;

    if (byteSize == 0)
        return NULL;
    memory = malloc(byteSize);
    if (memory && (requirements & MEMF_CLEAR))
        memset(memory, 0, byteSize);
    return memory;
}

void FreeMem(APTR memoryBlock, ULONG byteSize)
{
    (void)byteSize;
    free(memoryBlock);
}

/*
 * AllocVec()/FreeVec() record their own size, which on AmigaOS means a hidden
 * header ahead of the block.  malloc() already tracks it, so the host forms
 * are the same allocation without the header.  GetMsgFromStack() in
 * compiler/alib/alib_util.c is the caller that matters here.
 */
APTR AllocVec(ULONG byteSize, ULONG requirements)
{
    return AllocMem(byteSize, requirements);
}

void FreeVec(APTR memoryBlock)
{
    free(memoryBlock);
}

/* -------------------------------------------------------------------------
 * Memory pools
 *
 * MakeClass() gives every class its own pool and DeletePool() in FreeClass()
 * is expected to release whatever objects remain in it.  The host pool is a
 * doubly linked list of blocks so FreePooled() stays constant time and
 * DeletePool() can free the rest.  Puddle and threshold sizes are advisory on
 * AmigaOS and are recorded but unused here.
 * ---------------------------------------------------------------------- */

struct ace_pool_block
{
    struct ace_pool_block *next;
    struct ace_pool_block *previous;
};

struct ace_pool
{
    ULONG                  requirements;
    ULONG                  puddleSize;
    ULONG                  threshSize;
    struct ace_pool_block *blocks;
};

APTR CreatePool(ULONG requirements, ULONG puddleSize, ULONG threshSize)
{
    struct ace_pool *pool = calloc(1, sizeof(*pool));

    if (!pool)
        return NULL;
    pool->requirements = requirements;
    pool->puddleSize = puddleSize;
    pool->threshSize = threshSize;
    return pool;
}

void DeletePool(APTR poolHeader)
{
    struct ace_pool *pool = poolHeader;
    struct ace_pool_block *block;

    if (!pool)
        return;
    block = pool->blocks;
    while (block) {
        struct ace_pool_block *next = block->next;

        free(block);
        block = next;
    }
    free(pool);
}

APTR AllocPooled(APTR poolHeader, ULONG memSize)
{
    struct ace_pool *pool = poolHeader;
    struct ace_pool_block *block;

    if (!pool || memSize == 0)
        return NULL;
    block = malloc(sizeof(*block) + memSize);
    if (!block)
        return NULL;
    block->previous = NULL;
    block->next = pool->blocks;
    if (pool->blocks)
        pool->blocks->previous = block;
    pool->blocks = block;
    if (pool->requirements & MEMF_CLEAR)
        memset(block + 1, 0, memSize);
    return block + 1;
}

void FreePooled(APTR poolHeader, APTR memory, ULONG memSize)
{
    struct ace_pool *pool = poolHeader;
    struct ace_pool_block *block;

    (void)memSize;
    if (!pool || !memory)
        return;
    block = (struct ace_pool_block *)memory - 1;
    if (block->previous)
        block->previous->next = block->next;
    else
        pool->blocks = block->next;
    if (block->next)
        block->next->previous = block->previous;
    free(block);
}

/* -------------------------------------------------------------------------
 * Semaphores
 *
 * The class list lock is taken recursively: FreeClass() holds it and then
 * calls RemoveClass(), which takes it again.  Exec SignalSemaphores are
 * recursive for their owning task, so the host version tracks an owner and a
 * nesting count.  Shared locks are granted as exclusive ones; that is
 * stricter than Exec and therefore safe, and the only contention would be
 * between concurrent class lookups.
 *
 * Task identity is the address of a thread-local object, which is unique per
 * host thread and needs no Exec task to exist yet.
 * ---------------------------------------------------------------------- */

static pthread_mutex_t semaphore_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  semaphore_free = PTHREAD_COND_INITIALIZER;
static _Thread_local char semaphore_task_identity;

static struct Task *current_task_identity(void)
{
    return (struct Task *)&semaphore_task_identity;
}

void InitSemaphore(struct SignalSemaphore *sigSem)
{
    if (!sigSem)
        return;
    memset(sigSem, 0, sizeof(*sigSem));
    NEWLIST((struct List *)&sigSem->ss_WaitQueue);
    sigSem->ss_NestCount = 0;
    sigSem->ss_Owner = NULL;
}

void ObtainSemaphore(struct SignalSemaphore *sigSem)
{
    struct Task *self = current_task_identity();

    if (!sigSem)
        return;
    pthread_mutex_lock(&semaphore_mutex);
    while (sigSem->ss_NestCount > 0 && sigSem->ss_Owner != self)
        pthread_cond_wait(&semaphore_free, &semaphore_mutex);
    sigSem->ss_Owner = self;
    sigSem->ss_NestCount++;
    pthread_mutex_unlock(&semaphore_mutex);
}

void ObtainSemaphoreShared(struct SignalSemaphore *sigSem)
{
    ObtainSemaphore(sigSem);
}

void ReleaseSemaphore(struct SignalSemaphore *sigSem)
{
    if (!sigSem)
        return;
    pthread_mutex_lock(&semaphore_mutex);
    if (sigSem->ss_NestCount > 0 && --sigSem->ss_NestCount == 0) {
        sigSem->ss_Owner = NULL;
        pthread_cond_broadcast(&semaphore_free);
    }
    pthread_mutex_unlock(&semaphore_mutex);
}

/* -------------------------------------------------------------------------
 * Lists
 * ---------------------------------------------------------------------- */

void AddHead(struct List *list, struct Node *node)
{
    ADDHEAD(list, node);
}

void AddTail(struct List *list, struct Node *node)
{
    ADDTAIL(list, node);
}

/*
 * Guarded rather than a bare REMOVE(): real AmigaOS Remove() requires the
 * node actually be linked, but the console handler (src/aros_console_editor.c,
 * which shares this definition -- see proto/exec.h) calls it speculatively
 * on nodes that may not be, so an unguarded REMOVE() would dereference a
 * NULL/stale ln_Pred/ln_Succ. The guard is a no-op for an already-linked
 * node, so it costs BOOPSI's own real callers (rootclass.c's OM_REMOVE)
 * nothing.
 */
void Remove(struct Node *node)
{
    if (node && node->ln_Pred && node->ln_Succ)
        REMOVE(node);
}

/* -------------------------------------------------------------------------
 * Bootstrap
 *
 * A real AROS build reaches this through InitRootClass() in
 * rom/intuition/intuition_init.c.  The steps below are that function: the
 * class list and its lock, then a rootclass whose dispatcher is the real
 * rootDispatcher() from rom/intuition/rootclass.c, added to the list under
 * its standard name so FindClass("rootclass") resolves.
 * ---------------------------------------------------------------------- */

int ace_boopsi_init(void)
{
    struct IntIntuitionBase *base;

    if (IntuitionBase)
        return 0;
    base = calloc(1, sizeof(*base));
    if (!base)
        return -1;

    InitSemaphore(&base->ClassListLock);
    NEWLIST((struct List *)&base->ClassList);

    base->RootClass.cl_Dispatcher.h_Entry = (APTR)rootDispatcher;
    base->RootClass.cl_ID = (ClassID)ROOTCLASS;
    base->RootClass.cl_UserData = (IPTR)base;

    IntuitionBase = base;
    AddClass(&base->RootClass);
    return 0;
}

void ace_boopsi_cleanup(void)
{
    struct IntIntuitionBase *base = IntuitionBase;
    struct IClass *iclass;

    if (!base)
        return;

    /*
     * Drop every registered class except the rootclass, which is embedded in
     * the base.  FreeClass() refuses a class that still has subclasses or
     * objects, so repeat until a pass frees nothing and leaks are visible to
     * the caller rather than hidden by forced teardown.
     */
    for (;;) {
        int freed = 0;

        iclass = (struct IClass *)base->ClassList.mlh_Head;
        while (((struct MinNode *)iclass)->mln_Succ) {
            struct IClass *next =
                (struct IClass *)((struct MinNode *)iclass)->mln_Succ;

            if (iclass != &base->RootClass && FreeClass(iclass))
                freed = 1;
            iclass = next;
        }
        if (!freed)
            break;
    }

    RemoveClass(&base->RootClass);
    IntuitionBase = NULL;
    free(base);
}

struct IClass *ace_boopsi_rootclass(void)
{
    return IntuitionBase ? &IntuitionBase->RootClass : NULL;
}
