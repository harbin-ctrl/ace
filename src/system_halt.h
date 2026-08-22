#ifndef ACE_SYSTEM_HALT_H
#define ACE_SYSTEM_HALT_H

/*
 * Shutdown and Reboot, which are one command with two words.
 *
 * `reboot` selects which; `confirmed` is the CONFIRM switch, meaning the
 * caller has already answered the question and does not want to be asked.
 * Returns an AmigaDOS RETURN_* code.
 */
int ace_system_halt(int reboot, int confirmed);

#endif
