#ifndef ACE_AROS_REAL_PROTO_EXEC_H
#define ACE_AROS_REAL_PROTO_EXEC_H

struct Message;
struct MsgPort;

struct Message *GetMsg(struct MsgPort *port);

#if defined(ACE_BOOPSI_INTERN_H) || defined(ACE_AROS_REAL_HANDLER_TYPES_H)

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
 * The console handler (ace_handler_types.h) shares this declaration set: it
 * links src/aros_console_editor.o into the same ace-console binary as the
 * BOOPSI/graphics seams from Phase 3 onward, and needs the same AllocMem/
 * FreeMem/AllocVec/FreeVec/AddTail/Remove -- one definition apiece, in
 * src/aros_boopsi_runtime.c, rather than a second copy in
 * aros_console_editor.c that would collide with it at link time.
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

#endif /* ACE_BOOPSI_INTERN_H || ACE_AROS_REAL_HANDLER_TYPES_H */

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
void  CloseLibrary(void *library);

#endif /* ACE_GRAPHICS_INTERN_H */

#endif
