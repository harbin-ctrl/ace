#ifndef AMIGA_SHELL_PROTO_EXEC_H
#define AMIGA_SHELL_PROTO_EXEC_H

#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/libraries.h>

struct ExecBase;
struct Message;
extern struct ExecBase *SysBase;

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#include <exec/compat.h>
#endif

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *library);
APTR FindTask(CONST_STRPTR name);
void ReplyMsg(struct Message *message);
void Forbid(void);
void Permit(void);
ULONG Wait(ULONG signals);
ULONG SetSignal(ULONG set_mask, ULONG clear_mask);
ULONG CheckSignal(ULONG mask);
LONG AllocSignal(LONG signal_number);
void FreeSignal(LONG signal_number);

/* Host implementation of Exec's fast, non-registerized memory copy. */
void CopyMemQuick(CONST_APTR source, APTR destination, ULONG length);

#endif
