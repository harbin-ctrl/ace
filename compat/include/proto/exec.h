#ifndef AMIGA_SHELL_PROTO_EXEC_H
#define AMIGA_SHELL_PROTO_EXEC_H

#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/libraries.h>

struct ExecBase;
extern struct ExecBase *SysBase;

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#include <exec/compat.h>
#endif

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *library);
APTR FindTask(CONST_STRPTR name);
void Forbid(void);
void Permit(void);
ULONG SetSignal(ULONG set_mask, ULONG clear_mask);
ULONG CheckSignal(ULONG mask);
LONG AllocSignal(LONG signal_number);
void FreeSignal(LONG signal_number);

#endif
