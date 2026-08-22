#define _POSIX_C_SOURCE 200809L

#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <aros/shcommands.h>

#include "broker_protocol.h"
#include "broker_client.h"

#include <string.h>
#include <strings.h>

/*
 * Tally: say how much of what just happened needed root.
 *
 * ACE reaches the CRM when an operation the user attempted was refused, and
 * it does so quietly -- which is the right default, because a session that
 * announced every escalation would be unreadable, and because the whole point
 * of the design is that privilege is a detail of how an operation completed
 * rather than a mode the user is in.  It is still worth being able to ask.
 * With the tally on, the shell says how many operations needed root before
 * each prompt, so the answer arrives beside the work it describes.
 *
 * The count lives in the broker, not here.  Every ACE command is its own
 * process and exits with whatever it learned, so no command can see what the
 * one before it did; the broker is the single point every privileged request
 * already passes through, and the only place in a session that outlives them.
 *
 * MODE is a positional argument, so all of these say the same thing:
 *
 *     Tally ON        Tally MODE ON        Tally MODE=ON
 *
 * and Tally on its own reports which way it is set rather than toggling it.
 * Toggling would make the outcome depend on a state the user asked about
 * precisely because they did not know it.
 */
AROS_SH1(Tally, 41.1,
AROS_SHA(CONST_STRPTR, ,MODE, , NULL))
{
    AROS_SHCOMMAND_INIT

    CONST_STRPTR mode = SHArg(MODE);
    char answer[64];

    if (!mode) {
        if (native_broker_tally(AMIGA_BROKER_TALLY_STATE, answer,
                                sizeof(answer)) != 0) {
            PrintFault(IoErr(), "Tally");
            return RETURN_FAIL;
        }
        Printf("Tally is %s\n", (IPTR)(strcmp(answer, "ON") == 0 ? "on" : "off"));
        return RETURN_OK;
    }

    if (strcasecmp(mode, "ON") == 0) {
        if (native_broker_tally(AMIGA_BROKER_TALLY_ON, answer,
                                sizeof(answer)) != 0) {
            PrintFault(IoErr(), "Tally");
            return RETURN_FAIL;
        }
        return RETURN_OK;
    }
    if (strcasecmp(mode, "OFF") == 0) {
        if (native_broker_tally(AMIGA_BROKER_TALLY_OFF, answer,
                                sizeof(answer)) != 0) {
            PrintFault(IoErr(), "Tally");
            return RETURN_FAIL;
        }
        return RETURN_OK;
    }

    /* Named rather than left to a bare template error: the argument is not
       missing or malformed, it is a word this command does not know, and the
       two are different things to be told. */
    Printf("Tally: %s is not ON or OFF\n", (IPTR)mode);
    SetIoErr(ERROR_BAD_NUMBER);
    return RETURN_ERROR;

    AROS_SHCOMMAND_EXIT
}
