#ifndef AMIGA_SHELL_DOS_VAR_H
#define AMIGA_SHELL_DOS_VAR_H

#include <exec/types.h>
#include <exec/nodes.h>

struct LocalVar {
    struct Node lv_Node;
    UWORD lv_Flags;
    UBYTE *lv_Value;
    ULONG lv_Len;
};

#define LV_VAR           0
#define LV_ALIAS         1
#define GVB_GLOBAL_ONLY  8
#define GVB_LOCAL_ONLY   9
#define GVB_SAVE_VAR     12
#define GVF_GLOBAL_ONLY  (1L << GVB_GLOBAL_ONLY)
#define GVF_LOCAL_ONLY   (1L << GVB_LOCAL_ONLY)
#define GVF_SAVE_VAR     (1L << GVB_SAVE_VAR)

LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, LONG flags);
struct LocalVar *FindVar(CONST_STRPTR name, LONG type);
BOOL SetVar(CONST_STRPTR name, CONST_STRPTR value, LONG size, LONG flags);
BOOL DeleteVar(CONST_STRPTR name, LONG flags);

#endif
