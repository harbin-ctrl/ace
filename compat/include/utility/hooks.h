#ifndef AMIGA_SHELL_UTILITY_HOOKS_H
#define AMIGA_SHELL_UTILITY_HOOKS_H

#include <exec/types.h>
#include <exec/nodes.h>

/* Real, from utility/hooks.h. exall.c declares eac_MatchFunc as a
   struct Hook *; see aros/asmcall.h's CALLHOOKPKT(). */
struct Hook {
    struct MinNode h_MinNode;
    APTR h_Entry;
    APTR h_SubEntry;
    APTR h_Data;
};

#endif
