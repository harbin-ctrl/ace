#ifndef ACE_AROS_REAL_PROTO_EXEC_H
#define ACE_AROS_REAL_PROTO_EXEC_H

struct Message;
struct MsgPort;

struct Message *GetMsg(struct MsgPort *port);

#ifdef ACE_BOOPSI_INTERN_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/semaphores.h>

/*
 * The Exec surface the AROS BOOPSI sources call.  These are declared rather
 * than left implicit on purpose: AllocMem(), AllocPooled() and CreatePool()
 * all return pointers, and an implicit declaration would return int and
 * truncate them on a 64-bit host.  ACE implements them in
 * src/aros_boopsi_runtime.c.
 */
APTR AllocMem(ULONG byteSize, ULONG requirements);
void FreeMem(APTR memoryBlock, ULONG byteSize);
APTR AllocVec(ULONG byteSize, ULONG requirements);
void FreeVec(APTR memoryBlock);

APTR CreatePool(ULONG requirements, ULONG puddleSize, ULONG threshSize);
void DeletePool(APTR poolHeader);
APTR AllocPooled(APTR poolHeader, ULONG memSize);
void FreePooled(APTR poolHeader, APTR memory, ULONG memSize);

void InitSemaphore(struct SignalSemaphore *sigSem);
void ObtainSemaphore(struct SignalSemaphore *sigSem);
void ObtainSemaphoreShared(struct SignalSemaphore *sigSem);
void ReleaseSemaphore(struct SignalSemaphore *sigSem);

void AddHead(struct List *list, struct Node *node);
void AddTail(struct List *list, struct Node *node);
void Remove(struct Node *node);

#endif /* ACE_BOOPSI_INTERN_H */

#endif
