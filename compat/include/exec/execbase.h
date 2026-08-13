#ifndef AMIGA_SHELL_EXECBASE_H
#define AMIGA_SHELL_EXECBASE_H

#include <exec/types.h>

struct ExecBase {
    ULONG ex_DebugFlags;
};

#define EXECDEBUGF_SHELL (1u << 0)

#endif
