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

#endif
