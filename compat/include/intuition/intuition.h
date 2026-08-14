#ifndef AMIGA_SHELL_INTUITION_INTUITION_H
#define AMIGA_SHELL_INTUITION_INTUITION_H

/* Vim's os_amiga.c includes this unconditionally, but everything it
   declares from real Intuition -- struct IntuitionBase, an autoopen'd
   OpenLibrary("intuition.library") -- is itself guarded by
   "#if !defined(__AROS__)": AROS uses autoopen libraries instead, the branch
   that would need real Intuition types never compiles under __AROS__, and
   ACE always defines that macro. Empty on purpose. See proto/intuition.h for
   the one Intuition call this translation unit does reach. */

#endif
