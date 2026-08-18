#ifndef ACE_AROS_REAL_PROTO_EXEC_H
#define ACE_AROS_REAL_PROTO_EXEC_H

#include <exec/types.h>
#include <exec/libraries.h>

struct Message;
struct MsgPort;
struct Task;

/* Public Exec prototypes used by AROS command/library sources.  The real
 * AROS SDK generates these declarations; ACE keeps them here so host-native
 * AROS sources do not silently fall back to 32-bit implicit-int calls. */
extern struct ExecBase *SysBase;
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *library);
struct Task *FindTask(CONST_STRPTR name);
void Forbid(void);
void Permit(void);
ULONG Wait(ULONG signals);
ULONG Signal(struct Task *task, ULONG signals);
LONG AllocSignal(LONG signal_number);
void FreeSignal(LONG signal_number);
void SetTaskPri(struct Task *task, LONG priority);
struct MsgPort *CreateMsgPort(void);
void DeleteMsgPort(struct MsgPort *port);
struct MsgPort *CreatePort(CONST_STRPTR name, LONG signal);
void DeletePort(struct MsgPort *port);
struct MsgPort *FindPort(CONST_STRPTR name);
void PutMsg(struct MsgPort *port, struct Message *message);
struct Message *GetMsg(struct MsgPort *port);
void ReplyMsg(struct Message *message);
struct Message *WaitPort(struct MsgPort *port);
struct Task *CreateTask(CONST_STRPTR name, LONG priority, APTR init_pc,
                        ULONG stack_size);

#if defined(ACE_BOOPSI_INTERN_H) || defined(ACE_AROS_REAL_HANDLER_TYPES_H) || \
    defined(ACE_GRAPHICS_INTERN_H)

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
 *
 * The console handler and console.device renderer share this declaration
 * set. In particular, support.c assembles CSI sequences split across writes
 * with AllocMem(); leaving that declaration implicit truncates its pointer
 * return on a 64-bit host.
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

#endif /* ACE_BOOPSI_INTERN_H || ACE_AROS_REAL_HANDLER_TYPES_H || ACE_GRAPHICS_INTERN_H */

#if defined(ACE_GRAPHICS_INTERN_H) || defined(ACE_AROS_REAL_HANDLER_TYPES_H)

#include <exec/types.h>

/* Used by stdconclass.c/consoleclass.c to zero their instance data, by
   support.c's writeToConsole() to assemble a pending CSI sequence, and by
   the console handler (see the AllocMem/FreeMem comment above for why this
   is one definition shared across compile groups rather than several). */
void SetMem(APTR destination, ULONG length, UBYTE value);
void CopyMem(CONST_APTR source, APTR destination, ULONG length);

#endif /* ACE_GRAPHICS_INTERN_H || ACE_AROS_REAL_HANDLER_TYPES_H */

#ifdef ACE_GRAPHICS_INTERN_H

#include <exec/types.h>

/*
 * TaggedOpenLibrary()/CloseLibrary(): stdconclass.c's only real library
 * dependency, opening graphics.library by AROS's TAGGEDOPEN_GRAPHICS tag.
 * See src/aros_graphics_runtime.c.
 */
void *TaggedOpenLibrary(IPTR library);
void  CloseLibrary(struct Library *library);

#endif /* ACE_GRAPHICS_INTERN_H */

#endif
