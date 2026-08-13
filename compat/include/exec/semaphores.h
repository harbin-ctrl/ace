#ifndef AMIGA_SHELL_EXEC_SEMAPHORES_H
#define AMIGA_SHELL_EXEC_SEMAPHORES_H

#include <exec/nodes.h>
#include <exec/lists.h>

/* Real, from exec/semaphores.h -- only used by dosextens.h's struct
   FileLock (fl_Task is a MsgPort*, not a SignalSemaphore, so this is here
   for headers that forward-declare it, not for any field ACE reads). */
struct SignalSemaphore {
    struct Node ss_Link;
    WORD        ss_NestCount;
    struct MinList ss_WaitQueue;
    WORD        ss_QueueCount;
    struct Task *ss_Owner;
};

#endif
