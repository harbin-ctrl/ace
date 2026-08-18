#ifndef CLIB_REXXSYSLIB_PROTOS_H
#define CLIB_REXXSYSLIB_PROTOS_H

#include <exec/types.h>
#include <exec/ports.h>
#include <rexx/storage.h>

/* The generated AROS SDK header declares these at library vectors 21-28,
 * 75 and 76.  ACE deliberately exposes the same API as direct C calls. */
UBYTE *CreateArgstring(UBYTE *string, ULONG length);
void DeleteArgstring(UBYTE *argstring);
ULONG LengthArgstring(UBYTE *argstring);
struct RexxMsg *CreateRexxMsg(struct MsgPort *port, UBYTE *extension, UBYTE *host);
void DeleteRexxMsg(struct RexxMsg *packet);
void ClearRexxMsg(struct RexxMsg *msgptr, ULONG count);
BOOL FillRexxMsg(struct RexxMsg *msgptr, ULONG count, ULONG mask);
BOOL IsRexxMsg(struct RexxMsg *msgptr);
void LockRexxBase(ULONG resource);
void UnlockRexxBase(ULONG resource);

#endif
