#include <exec/execbase.h>
#include <dos/dos.h>

#include "broker_client.h"

SIPTR Start(STRPTR argument_string, ULONG argument_size,
            struct ExecBase *exec_base);
extern struct ExecBase *SysBase;

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (native_broker_ensure() != 0)
        return RETURN_FAIL;
    return (int)Start(NULL, 0, SysBase);
}
