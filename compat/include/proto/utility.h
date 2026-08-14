#ifndef AMIGA_SHELL_PROTO_UTILITY_H
#define AMIGA_SHELL_PROTO_UTILITY_H

#include <exec/types.h>

LONG Stricmp(CONST_STRPTR left, CONST_STRPTR right);
LONG Strnicmp(CONST_STRPTR left, CONST_STRPTR right, LONG length);
UBYTE ToUpper(ULONG character);

#endif
