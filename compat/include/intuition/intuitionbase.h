#ifndef ACE_INTUITION_INTUITIONBASE_H
#define ACE_INTUITION_INTUITIONBASE_H

#include <exec/libraries.h>

/* Beep only needs the public library node so the unmodified AROS command can
 * close the library exactly as it does on AmigaOS. */
struct IntuitionBase {
    struct Library LibNode;
};

#endif
