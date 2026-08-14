#define AddTail ace_native_inline_addtail
#define Remove ace_native_inline_remove
#include <exec/lists.h>
#undef AddTail
#undef Remove

/* AROS console-handler objects are compiled against external Exec list
   calls, while ACE's compat header uses inline implementations for its own
   callers. Keep the external symbols in a separate translation unit so the
   inline definitions do not collide with them in native_dos.c. */
void AddTail(struct List *list, struct Node *node)
{
    node->ln_Succ = (struct Node *)&list->lh_Tail;
    node->ln_Pred = list->lh_TailPred;
    list->lh_TailPred->ln_Succ = node;
    list->lh_TailPred = node;
}

void Remove(struct Node *node)
{
    if (!node || !node->ln_Pred || !node->ln_Succ)
        return;
    node->ln_Pred->ln_Succ = node->ln_Succ;
    node->ln_Succ->ln_Pred = node->ln_Pred;
    node->ln_Succ = NULL;
    node->ln_Pred = NULL;
}
