#ifndef EXEC_LISTS_H
#define EXEC_LISTS_H

#include <exec/nodes.h>

struct List {
    struct Node *lh_Head;
    struct Node *lh_Tail;
    struct Node *lh_TailPred;
    UBYTE lh_Type;
    UBYTE l_pad;
};

struct MinList {
    struct MinNode *mlh_Head;
    struct MinNode *mlh_Tail;
    struct MinNode *mlh_TailPred;
};

static inline void ace_newlist(struct List *list)
{
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)list;
    list->lh_Type = 0;
    list->l_pad = 0;
}

static inline void ace_newminlist(struct MinList *list)
{
    list->mlh_Head = (struct MinNode *)&list->mlh_Tail;
    list->mlh_Tail = NULL;
    list->mlh_TailPred = (struct MinNode *)list;
}

#define NEWLIST(list) _Generic((list), \
    struct MinList *: ace_newminlist, \
    default: ace_newlist)((list))
#define NewList(list) ace_newlist((list))

static inline void ace_addtail_minlist(struct MinList *list,
                                       struct MinNode *node)
{
    node->mln_Succ = (struct MinNode *)&list->mlh_Tail;
    node->mln_Pred = list->mlh_TailPred;
    list->mlh_TailPred->mln_Succ = node;
    list->mlh_TailPred = node;
}

static inline void ace_addtail_list(struct List *list, struct Node *node)
{
    node->ln_Succ = (struct Node *)&list->lh_Tail;
    node->ln_Pred = list->lh_TailPred;
    list->lh_TailPred->ln_Succ = node;
    list->lh_TailPred = node;
}

#define ADDTAIL(list, node) _Generic((list), \
    struct MinList *: ace_addtail_minlist, \
    default: ace_addtail_list)((list), (void *)(node))

#define ADDHEAD(list, node) \
    do { \
        struct List *_list = (struct List *)(list); \
        struct Node *_node = (struct Node *)(node); \
        _node->ln_Succ = _list->lh_Head; \
        _node->ln_Pred = (struct Node *)&_list->lh_Head; \
        _list->lh_Head->ln_Pred = _node; \
        _list->lh_Head = _node; \
    } while (0)

#define ForeachNode(list, node) \
    for ((node) = (void *)((struct List *)(list))->lh_Head; \
         ((struct Node *)(node))->ln_Succ; \
         (node) = (void *)((struct Node *)(node))->ln_Succ)

#define ForeachNodeSafe(list, current, next) \
    for ((current) = (void *)((struct List *)(list))->lh_Head; \
         ((next) = (void *)((struct Node *)(current))->ln_Succ), \
         ((struct Node *)(current))->ln_Succ; \
         (current) = (void *)(next))

#define REMOVE(node) \
    ({ struct Node *_node = (struct Node *)(node); \
       _node->ln_Pred->ln_Succ = _node->ln_Succ; \
       _node->ln_Succ->ln_Pred = _node->ln_Pred; \
       _node; })

#define GetHead(list) \
    (((struct List *)(list))->lh_Head->ln_Succ ? \
     ((struct List *)(list))->lh_Head : NULL)

#define GetSucc(node) \
    (((struct Node *)(node))->ln_Succ && \
     ((struct Node *)(node))->ln_Succ->ln_Succ ? \
     ((struct Node *)(node))->ln_Succ : NULL)

static inline int ace_node_name_equal(const char *left, const char *right)
{
    if (!left || !right)
        return 0;
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static inline struct Node *FindName(struct List *list, const char *name)
{
    struct Node *node;
    if (!list || !name)
        return NULL;
    for (node = GetHead(list); node; node = GetSucc(node)) {
        if (ace_node_name_equal(node->ln_Name, name))
            return node;
    }
    return NULL;
}

#define IsListEmpty(list) \
    (((struct List *)(list))->lh_TailPred == (struct Node *)(list))

static inline void *ace_gettail_minlist(struct MinList *list)
{
    return list->mlh_TailPred == (struct MinNode *)list ?
           NULL : (void *)list->mlh_TailPred;
}

#define GetTail(list) ace_gettail_minlist((list))

static inline struct Node *ace_remhead_list(struct List *list)
{
    struct Node *node = list->lh_Head;
    if (node == (struct Node *)&list->lh_Tail)
        return NULL;
    list->lh_Head = node->ln_Succ;
    node->ln_Succ->ln_Pred = (struct Node *)&list->lh_Head;
    node->ln_Succ = NULL;
    node->ln_Pred = NULL;
    return node;
}

static inline struct MinNode *ace_remhead_minlist(struct MinList *list)
{
    struct MinNode *node = list->mlh_Head;
    if (node == (struct MinNode *)&list->mlh_Tail)
        return NULL;
    list->mlh_Head = node->mln_Succ;
    node->mln_Succ->mln_Pred = (struct MinNode *)&list->mlh_Head;
    node->mln_Succ = NULL;
    node->mln_Pred = NULL;
    return node;
}

#define REMHEAD(list) _Generic((list), \
    struct MinList *: ace_remhead_minlist, \
    default: ace_remhead_list)((list))

#endif
