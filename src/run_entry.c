#include <dos/dos.h>
#include <dos/rdargs.h>

#include <stdint.h>

#include "native_host.h"

int main(void)
{
    IPTR arguments[2] = {0, 0};
    struct RDArgs *rdargs;
    LONG result = RETURN_OK;
    uint64_t task_id;

    rdargs = ReadArgs("QUIET/S,COMMAND/F", arguments, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Run");
        return RETURN_ERROR;
    }
    if (arguments[1] && *(const char *)arguments[1] != '\0' &&
        native_run_background((const char *)arguments[1], &task_id) != 0) {
        PrintFault(IoErr(), "Run");
        result = RETURN_FAIL;
    } else if (arguments[1] && *(const char *)arguments[1] != '\0' &&
               !arguments[0] && task_id) {
        Printf("[%llu]\n", (unsigned long long)task_id);
    }
    FreeArgs(rdargs);
    return result;
}
