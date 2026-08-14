#ifndef AMIGA_SHELL_AROS_SYSTEM_H
#define AMIGA_SHELL_AROS_SYSTEM_H

/* A real AROS build generates aros/system.h as part of configuring itself:
   it is the compiler and CPU glue for the target being built, and there is
   no copy of it in the source tree to compile against. Nothing ACE compiles
   through this compat tree reads anything out of it -- the AROS sources that
   include it take their types from <exec/types.h>, which they include too --
   so it exists only to satisfy the include. */

#endif
