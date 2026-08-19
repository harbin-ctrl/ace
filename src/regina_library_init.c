#define _POSIX_C_SOURCE 200809L

/*
 * What regina_init.c would have set up, done the ACE way.
 *
 * Upstream's regina_init.c is AROS module glue: it includes LC_LIBDEFS_FILE
 * and aros/symbolsets.h and hangs InitLib/ExpungeLib off the module system
 * that builds a .library. ACE has no module system and should not grow a
 * pretend one, so that single file is left out of the object set and what it
 * actually provided is provided here instead -- the per-task Regina state
 * list, and the pool both it and mt_amigalib.c allocate from.
 *
 * This is the ordinary shape of the exercise: where Regina depends on
 * something AROS supplies, ACE supplies it rather than Regina being cut down.
 * Everything else in the library object set is upstream's, untouched.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/alib.h>
#include <exec/libraries.h>

/* Declared by mt_amigalib.h; defined here because regina_init.c is not
   linked. __regina_semaphorepool is upstream's, in mt_amigalib.c. */
struct MinList *__regina_tsdlist;
extern APTR __regina_semaphorepool;

static void __attribute__((constructor)) ace_regina_library_init(void)
{
    __regina_semaphorepool = CreatePool(MEMF_PUBLIC, 1024, 256);
    if (!__regina_semaphorepool)
        return;
    __regina_tsdlist = AllocPooled(__regina_semaphorepool,
                                   sizeof(*__regina_tsdlist));
    if (__regina_tsdlist)
        NewList((struct List *)__regina_tsdlist);
}

/*
 * The library base, and the offset table that gives a task access to it.
 *
 * Both exist because AROS Regina is a shared library: amifuncs.c stores the
 * base in its per-task state and calls __aros_setoffsettable() in the helper
 * task so relocatable library code can reach it. Statically linked there is
 * one copy of the code at a fixed place and every thread already reaches it,
 * so there is nothing to hand over -- which is why these are empty rather
 * than unimplemented.
 */
struct Library *__aros_getbase_ReginaBase(void)
{
    return NULL;
}

void __aros_setoffsettable(struct Library *base)
{
    (void)base;
}

static void __attribute__((destructor)) ace_regina_library_expunge(void)
{
    if (__regina_semaphorepool)
        DeletePool(__regina_semaphorepool);
    __regina_semaphorepool = NULL;
    __regina_tsdlist = NULL;
}
