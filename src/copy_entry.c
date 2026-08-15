/* Host entry point for AROS Copy.c.

   Copy is one of the older AROS commands: unlike the ordinary commands that
   use AROS_SHn, it enters through an AROS process-start routine. The fetched
   source remains unchanged; its Makefile translation unit makes that routine
   visible and this small host shim supplies the process argument line. */

#include <string.h>

#include <dos/dos.h>
#include <exec/execbase.h>

void ace_command_arguments_from_argv(int argc, char **argv);
const char *ace_command_argument_line(void);
struct ExecBase *native_exec_base_pointer(void);

SIPTR Start(STRPTR argument_line, ULONG argument_size,
            struct ExecBase *exec_base);

int main(int argc, char **argv)
{
    const char *line;

    ace_command_arguments_from_argv(argc, argv);
    line = ace_command_argument_line();
    return (int)Start((STRPTR)line, (ULONG)strlen(line),
                      native_exec_base_pointer());
}
