#ifndef AMIGA_SHELL_UTILITY_TAGITEM_H
#define AMIGA_SHELL_UTILITY_TAGITEM_H

#include <exec/types.h>

struct TagItem {
    ULONG ti_Tag;
    IPTR ti_Data;
};

#define TAG_DONE 0
#define TAG_END TAG_DONE
#define TAG_USER 0x80000000UL

#endif
