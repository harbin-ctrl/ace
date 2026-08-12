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

#endif
