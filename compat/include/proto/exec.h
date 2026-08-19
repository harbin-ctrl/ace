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
struct Task;
#ifndef FindTask
struct Task *FindTask(CONST_STRPTR name);
#endif
void ReplyMsg(struct Message *message);
void Forbid(void);
void Permit(void);
ULONG Wait(ULONG signals);
/* Implemented in src/native_dos.c.  Declared here because a DOS-side caller
   reaches Exec through this header, and undeclared it was an implicit int
   returning a pointer-sized value.  Guarded like FindTask above: the
   test-only AMIGA_EXEC_COMPAT_ENABLED build defines Signal to a shim that
   takes an APTR and declares itself. */
#ifndef Signal
void Signal(struct Task *task, ULONG signals);
#endif
ULONG SetSignal(ULONG set_mask, ULONG clear_mask);
ULONG CheckSignal(ULONG mask);
LONG AllocSignal(LONG signal_number);
void FreeSignal(LONG signal_number);

/* Host implementation of Exec's fast, non-registerized memory copy. */
void CopyMemQuick(CONST_APTR source, APTR destination, ULONG length);

#endif
