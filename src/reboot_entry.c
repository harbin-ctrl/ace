#include <dos/dos.h>
#include <dos/rdargs.h>

#include <proto/dos.h>

#include "system_halt.h"

/*
 * Reboot [CONFIRM]
 *
 * CONFIRM says the question has already been answered.  Without it the
 * command asks, and only where there is somebody to ask.
 */
int main(void)
{
    IPTR arguments[1] = {0};
    struct RDArgs *rdargs;
    int result;

    rdargs = ReadArgs("CONFIRM/S", arguments, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Reboot");
        return RETURN_ERROR;
    }
    result = ace_system_halt(1, arguments[0] != 0);
    FreeArgs(rdargs);
    return result;
}
