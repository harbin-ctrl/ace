#ifndef AMIGA_SHELL_PROTO_AROSSUPPORT_H
#define AMIGA_SHELL_PROTO_AROSSUPPORT_H

#include <exec/types.h>

/* Implemented by AROS's own compiler/arossupport/isdosentrya.c, compiled
   against this compat tree. Protect and Filenote use it to refuse to change
   a whole volume or device rather than an object inside one. */
BOOL IsDosEntryA(STRPTR name, ULONG flags);

#endif
