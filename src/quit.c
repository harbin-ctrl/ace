#define _POSIX_C_SOURCE 200809L

#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include "dos_commanderrors.h"

#include <aros/shcommands.h>

#include "native_host.h"

/*
 * This is the AROS C:Quit command with one host-specific seam.  On AmigaDOS
 * it writes cli_CurrentInput's FileHandle position past the end of the
 * script.  ACE represents that input as a shared stdio file description, so
 * native_quit_script() performs the equivalent seek in a form the parent
 * shell can observe after this command process exits.
 */
AROS_SH1(Quit, 41.1,
AROS_SHA(LONG *, ,RC,/N,NULL))
{
    AROS_SHCOMMAND_INIT

    struct CommandLineInterface *cli = Cli();
    int retval = RETURN_OK;

    if (cli && native_cli_script_input() &&
        cli->cli_CurrentInput != cli->cli_StandardInput)
    {
        if (native_quit_script() != 0)
            retval = RETURN_FAIL;
        else if (SHArg(RC) != NULL)
            retval = (int)*SHArg(RC);
    }
    else
    {
        PrintFault(ERROR_SCRIPT_ONLY, "Quit");
        retval = RETURN_FAIL;
    }

    return retval;

    AROS_SHCOMMAND_EXIT
}
