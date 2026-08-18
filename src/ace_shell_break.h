#ifndef ACE_SHELL_BREAK_H
#define ACE_SHELL_BREAK_H

#include <sys/types.h>

/* SIGUSR1 is ACE's private console-to-shell Ctrl-C notification. */
void ace_shell_break_init(void);
void ace_shell_break_set_foreground(pid_t child);

#endif
