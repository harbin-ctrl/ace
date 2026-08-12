#ifndef AMIGA_SHELL_MEMORY_H
#define AMIGA_SHELL_MEMORY_H

#include <exec/types.h>

#define MEMF_PUBLIC 0
#define MEMF_ANY    0
#define MEMF_LOCAL  0
#define MEMF_CLEAR  1u

APTR AllocVec(ULONG size, ULONG flags);
void FreeVec(APTR memory);
APTR AllocMem(ULONG size, ULONG flags);
void FreeMem(APTR memory, ULONG size);
ULONG AvailMem(ULONG flags);

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#include <exec/compat.h>
#define AllocMem amiga_exec_compat_alloc_mem
#define FreeMem amiga_exec_compat_free_mem
#define AllocVec amiga_exec_compat_alloc_vec
#define FreeVec amiga_exec_compat_free_vec
#endif

#endif
