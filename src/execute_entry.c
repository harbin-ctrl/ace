#include <dos/dos.h>
#include <dos/rdargs.h>

#include "native_host.h"

/* Execute's AmigaDOS template. AROS's own carries more -- the argument line
   a script can refer to as <n> -- and that waits on ACE having somewhere to
   put it that survives the command's process. */
int main(void)
{
    IPTR arguments[1] = {0};
    struct RDArgs *rdargs;
    LONG result;

    rdargs = ReadArgs("NAME/A", arguments, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Execute");
        return RETURN_ERROR;
    }
    result = native_execute_script((const char *)arguments[0]);
    if (result != RETURN_OK)
        PrintFault(IoErr(), "Execute");
    FreeArgs(rdargs);
    return result;
}
