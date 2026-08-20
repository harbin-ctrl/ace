#ifndef AMIGA_SHELL_LIBRARIES_H
#define AMIGA_SHELL_LIBRARIES_H

#include <exec/nodes.h>

/* Keep the public base layout compatible with the AROS headers used when
 * Regina's unchanged amifuncs.c is compiled.  A four-byte placeholder was
 * enough for code that only passed Library * around, but it put RxsLib's
 * rl_LibList at the wrong offset: ACE could populate the resource list while
 * Regina walked a different address and concluded that no library existed. */
struct Library {
    struct Node lib_Node;
    UBYTE lib_Flags;
    UBYTE lib_pad;
    UWORD lib_NegSize;
    UWORD lib_PosSize;
    UWORD lib_Version;
    UWORD lib_Revision;
    APTR lib_IdString;
    ULONG lib_Sum;
    UWORD lib_OpenCnt;
};

#endif
