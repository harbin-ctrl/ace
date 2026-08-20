#ifndef AMIGA_SHELL_EXEC_SEMAPHORES_H
#define AMIGA_SHELL_EXEC_SEMAPHORES_H

#include <exec/nodes.h>
#include <exec/lists.h>

/*
 * The real layout from AROS's exec/semaphores.h, field for field.
 *
 * It has to be exactly that, not merely close enough for the fields ACE
 * reads, because ACE is not the only thing that lays one of these out. The
 * imported AROS sources -- BOOPSI, console.device -- are compiled against
 * AROS's own headers, so a semaphore is 104 bytes to them; Regina and every
 * ACE object see this file. src/aros_exec_memory.c's InitSemaphore() serves
 * both, and Regina's AmigaLockSemaphore() allocates one at the size *it*
 * sees and passes it in. This header used to drop ss_MultipleLink and swap
 * the last two fields, which made it 80 bytes, and InitSemaphore() then
 * memset() 24 bytes past the end of every semaphore Regina allocated:
 * "malloc(): corrupted top size", from the first RexxStart() onwards.
 *
 * So a compat structure that anything shared lays out is an ABI, and being
 * a subset of the real one is not a smaller version of correct. Where a
 * field is genuinely unused here, carry it anyway and say so.
 *
 * ACE reads ss_NestCount and ss_Owner; the rest is layout. AROS's
 * SemaphoreRequest carries a spinlock member under __AROSPLATFORM_SMP__,
 * which ACE's AROS tree does not define -- if that ever changes, this
 * structure changes size and this file has to follow it.
 */
struct SemaphoreRequest {
    struct MinNode sr_Link;
    struct Task *sr_Waiter;
};

struct SignalSemaphore {
    struct Node ss_Link;
    WORD        ss_NestCount;
    struct MinList ss_WaitQueue;
    struct SemaphoreRequest ss_MultipleLink;
    struct Task *ss_Owner;
    WORD        ss_QueueCount;
};

#endif
