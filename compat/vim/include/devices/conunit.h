#ifndef ACE_VIM_DEVICES_CONUNIT_H
#define ACE_VIM_DEVICES_CONUNIT_H

/* Vim includes devices/conunit.h unconditionally. Its __AROS__ shell-size
   path uses the console escape query and never dereferences struct ConUnit,
   so a forward declaration keeps the untouched Vim source independent of
   AROS's architecture-specific keymap/timer headers. */
struct ConUnit;

#endif
