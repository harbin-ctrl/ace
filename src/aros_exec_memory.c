#define _POSIX_C_SOURCE 200809L

/*
 * Exec memory pools and semaphores.
 *
 * Moved out of aros_boopsi_runtime.c unchanged. They were written for the
 * AROS BOOPSI sources, but nothing in them is about BOOPSI: Regina's library
 * build needs the same four pool calls and the same semaphores for its
 * per-task state, and linking the BOOPSI runtime to get them would drag
 * Intuition into the Rexx interpreter.
 *
 * One copy, so the two callers cannot drift apart -- and so a fix to the
 * recursive-semaphore accounting is a fix for both.
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/semaphores.h>

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
    /* No cast: ss_WaitQueue is a MinList, and casting it to struct List *
       picks NEWLIST's full-list initialiser, which writes an lh_Type and a
       pad byte past the three pointers a MinList has. */
    NEWLIST(&sigSem->ss_WaitQueue);
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

