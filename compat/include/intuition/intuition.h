#ifndef AMIGA_SHELL_INTUITION_INTUITION_H
#define AMIGA_SHELL_INTUITION_INTUITION_H

#include <exec/types.h>

/* Vim's os_amiga.c includes this unconditionally, but everything it
   declares from real Intuition -- struct IntuitionBase, an autoopen'd
   OpenLibrary("intuition.library") -- is itself guarded by
   "#if !defined(__AROS__)": AROS uses autoopen libraries instead. ACE
   supplies the small EasyRequest surface directly. */

struct Screen;
struct Window;

struct EasyStruct
{
    ULONG        es_StructSize;
    ULONG        es_Flags;
    CONST_STRPTR es_Title;
    CONST_STRPTR es_TextFormat;
    CONST_STRPTR es_GadgetFormat;
};

#endif
