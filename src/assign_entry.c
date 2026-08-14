#include <exec/execbase.h>
#include <dos/dos.h>

#include "broker_client.h"

SIPTR Start(STRPTR argument_string, ULONG argument_size,
            struct ExecBase *exec_base);
void ace_command_arguments_from_argv(int argc, char **argv);
extern struct ExecBase *SysBase;

int main(int argc, char **argv)
{
    /* Assign.c reads its arguments with ReadArgs(), which takes them from the
       input stream rather than from this vector. */
    ace_command_arguments_from_argv(argc, argv);
    if (native_broker_ensure() != 0)
        return RETURN_FAIL;
    return (int)Start(NULL, 0, SysBase);
}
