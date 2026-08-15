#ifndef AMIGA_SHELL_PROTO_ALIB_H
#define AMIGA_SHELL_PROTO_ALIB_H

#include <exec/lists.h>

STRPTR StrDup(CONST_STRPTR string);
void __sprintf(UBYTE *buffer, const UBYTE *format, ...);

#endif
