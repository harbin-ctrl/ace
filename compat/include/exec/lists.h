#ifndef AMIGA_SHELL_EXEC_LISTS_H
#define AMIGA_SHELL_EXEC_LISTS_H

#include <exec/nodes.h>

struct List {
    struct Node *lh_Head;
    struct Node *lh_Tail;
    struct Node *lh_TailPred;
    UBYTE lh_Type;
    UBYTE l_pad;
};

#define NEWLIST(list) \
    do { \
        (list)->lh_Head = (struct Node *)&(list)->lh_Tail; \
        (list)->lh_Tail = NULL; \
        (list)->lh_TailPred = (struct Node *)(list); \
        (list)->lh_Type = 0; \
        (list)->l_pad = 0; \
    } while (0)

#define NewList(list) NEWLIST(list)

#define ForeachNode(list, node) \
    for ((node) = (void *)((list)->lh_Head); \
         ((struct Node *)(node))->ln_Succ; \
         (node) = (void *)((struct Node *)(node))->ln_Succ)

static inline void AddTail(struct List *list, struct Node *node)
{
    node->ln_Succ = (struct Node *)&list->lh_Tail;
    node->ln_Pred = list->lh_TailPred;
    list->lh_TailPred->ln_Succ = node;
    list->lh_TailPred = node;
}

static inline void Remove(struct Node *node)
{
    if (node && node->ln_Pred && node->ln_Succ) {
        node->ln_Pred->ln_Succ = node->ln_Succ;
        node->ln_Succ->ln_Pred = node->ln_Pred;
        node->ln_Succ = NULL;
        node->ln_Pred = NULL;
    }
}

#endif
