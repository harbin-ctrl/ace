#include <dos/dos.h>
#include <dos/rdargs.h>

#include "native_host.h"

int main(void)
{
    IPTR arguments[2] = {0, 0};
    struct RDArgs *rdargs;
    LONG result = RETURN_OK;

    rdargs = ReadArgs("QUIET/S,COMMAND/F", arguments, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Run");
        return RETURN_ERROR;
    }
    (void)arguments[0];
    if (arguments[1] && *(const char *)arguments[1] != '\0' &&
        native_run_background((const char *)arguments[1]) != 0) {
        PrintFault(IoErr(), "Run");
        result = RETURN_FAIL;
    }
    FreeArgs(rdargs);
    return result;
}
