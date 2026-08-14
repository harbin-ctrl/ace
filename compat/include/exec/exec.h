#ifndef AMIGA_SHELL_EXEC_EXEC_H
#define AMIGA_SHELL_EXEC_EXEC_H

/* Real AROS's <exec/exec.h> is the master header for the whole exec.library
   subsystem -- interrupts, memory pools, every device and task structure --
   and pulls in the architecture-specific CPU context headers along with it.
   ACE's DOS-side seam models none of that; it is built one call at a time,
   the way the rest of this compat tree is. This exists only so that a
   source file written against real AmigaOS headers -- one that includes
   <exec/exec.h> directly rather than through <proto/exec.h>, as Vim's
   os_amiga.c does -- has something to find. */

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/tasks.h>

#endif
