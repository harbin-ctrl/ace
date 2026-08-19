#ifndef AMIGA_SHELL_EXEC_NODES_H
#define AMIGA_SHELL_EXEC_NODES_H

#include <exec/types.h>

struct Node {
    struct Node *ln_Succ;
    struct Node *ln_Pred;
    UBYTE ln_Type;
    BYTE ln_Pri;
    char *ln_Name;
};

struct MinNode {
    struct MinNode *mln_Succ;
    struct MinNode *mln_Pred;
};

/* ln_Type values.  Only the ones ACE has a use for: these are ABI numbers
   read by code ACE did not write -- a RexxMsg is stamped NT_MESSAGE and any
   ARexx client may check it -- so they carry AROS's values from
   compiler/include/exec/nodes.h rather than a local numbering. */
#define NT_UNKNOWN      0
#define NT_TASK         1
#define NT_MSGPORT      4
#define NT_MESSAGE      5
#define NT_PROCESS      13

#endif
