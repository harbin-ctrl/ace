#ifndef AMIGA_SHELL_PROTO_INTUITION_H
#define AMIGA_SHELL_PROTO_INTUITION_H

/* Vim's os_amiga.c includes this unconditionally alongside proto/exec.h and
   proto/dos.h, but the only things it actually uses from it are a
   Workbench-launch window handle (wb_window) that stays NULL on every path
   ACE exercises -- ACE always starts a command from a CLI, never from a
   Workbench icon double-click -- and the one call gated behind it,
   SetWindowTitles(). Real Intuition is out of scope here on purpose: it
   belongs to the BOOPSI/graphics seam in compat/aros-real, which cannot
   coexist in a translation unit with glibc headers this one also needs. */

#include <exec/types.h>

struct Window;

void SetWindowTitles(struct Window *window, CONST_STRPTR title,
                     CONST_STRPTR screen_title);

#endif
