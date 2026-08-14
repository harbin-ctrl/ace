/* Host entry point for an AROS command that calls ReadArgs() itself and
   keeps its own main(), which the Makefile renames out of the way with
   -Dmain=ace_command_entry_main.

   Commands whose arguments are declared with the AROS_SHn macros do not use
   this file: AROS's own macro header gives them an entry point, and
   compat/include/ace_shcommand_host.h calls it. Both routes end up in
   src/native_shcommand.c, so a command reaches its arguments and publishes
   its result the same way whichever form its author chose. */

#include <dos/dos.h>

int ace_command_entry_main(void);
int ace_command_start(int argc, char **argv, int (*entry)(void));

int main(int argc, char **argv)
{
    return ace_command_start(argc, argv, ace_command_entry_main);
}
