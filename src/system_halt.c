#define _POSIX_C_SOURCE 200809L

#include "system_halt.h"

#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include "native_host.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Turning the machine off, and turning it off and on again.
 *
 * Deliberately not through the CRM. Stopping a machine needs privilege, and
 * ACE has a privileged service sitting right there, so the obvious move is to
 * add an opcode for it -- but the FMM/CRM protocol has no operation that
 * executes anything, and that is a decision the security design records as
 * one not to regress: an operation that runs a program is a root shell
 * reachable from any ARexx script, whatever it is nominally for, and the
 * narrow opcode surface is the whole boundary in a session-scoped
 * authorization model.
 *
 * It does not need one either. Stopping the machine is something the person
 * at the keyboard is already allowed to do: systemd asks polkit, polkit sees
 * an active local session, and the answer is yes without anybody becoming
 * root along the way. So this runs systemctl as the user and reports what it
 * says. A session that is refused is refused honestly, which is the right
 * outcome on a machine where the administrator meant that.
 */

/* The program that stops the machine, overridable for tests.
 *
 * A test that ran the real one would take the machine down with it, so this
 * is the seam that lets the question be asked without the answer arriving.
 * It grants nothing: the command runs as the user either way, so anyone who
 * can set this could equally have run the program themselves. */
static const char *halt_program(void)
{
    const char *configured = getenv("ACE_HALT_COMMAND");

    return configured && *configured ? configured : "systemctl";
}

/*
 * Ask, when the caller has not already answered.
 *
 * Only where there is somebody to ask. A script with no CONFIRM is refused
 * rather than prompted: reading the answer would take the next line of the
 * script and run the rest of it against a question it never meant to answer,
 * and a machine that shut down because a script had a stray "y" in it would
 * be a poor advertisement for asking at all.
 */
static int confirmed_interactively(int reboot)
{
    char answer[64];
    BPTR input = Input();
    BPTR output = Output();

    /* Running inside a script, asked the way If, Else and Quit ask it: the
       shell hands its commands the script's own descriptor, and a command
       that has one is a command nobody is watching.  Not cli_Background --
       an ACE command is its own process and cannot see the shell's copy of
       that -- and not IsInteractive() either, because a script's input is an
       ordinary readable file and IsInteractive() quite correctly says so. */
    if (!input || native_cli_script_input() || !IsInteractive(input)) {
        Printf("%s: not confirmed, and there is nobody to ask."
               "  Use CONFIRM.\n",
               (IPTR)(reboot ? "Reboot" : "Shutdown"));
        SetIoErr(ERROR_REQUIRED_ARG_MISSING);
        return 0;
    }

    Printf("%s the system?  (y/N) ", (IPTR)(reboot ? "Reboot" : "Shut down"));
    if (output)
        Flush(output);

    if (!FGets(input, answer, (LONG)sizeof(answer))) {
        /* End of input where a person was expected: no answer is not yes. */
        Printf("\n");
        return 0;
    }
    answer[strcspn(answer, "\r\n")] = '\0';

    /* "y" and "yes", and nothing else. Anything a person might type by
       accident has to mean no, because only one of the two answers can be
       taken back. */
    return strcasecmp(answer, "y") == 0 || strcasecmp(answer, "yes") == 0;
}

int ace_system_halt(int reboot, int confirmed)
{
    const char *name = reboot ? "Reboot" : "Shutdown";
    const char *action = reboot ? "reboot" : "poweroff";
    const char *program = halt_program();
    pid_t child;
    int state;

    if (!confirmed && !confirmed_interactively(reboot)) {
        Printf("%s cancelled\n", (IPTR)name);
        return RETURN_WARN;
    }

    /* Said before it is asked for, because after this there may be no more
       output: the machine can go down while this line is still on its way to
       the screen. */
    Printf("%s...\n", (IPTR)(reboot ? "Rebooting" : "Shutting down"));
    if (Output())
        Flush(Output());

    child = fork();
    if (child < 0) {
        SetIoErr(errno);
        PrintFault(IoErr(), (STRPTR)name);
        return RETURN_FAIL;
    }
    if (child == 0) {
        /* execvp, not a shell: there is no command line here to be parsed,
           and nothing the caller supplied goes into it. */
        execlp(program, program, action, (char *)NULL);
        _exit(127);
    }
    while (waitpid(child, &state, 0) < 0 && errno == EINTR)
        ;

    if (WIFEXITED(state) && WEXITSTATUS(state) == 0)
        return RETURN_OK;

    if (WIFEXITED(state) && WEXITSTATUS(state) == 127) {
        Printf("%s: could not run %s\n", (IPTR)name, (IPTR)program);
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
    } else {
        /* systemctl has already said why on its own error stream -- polkit
           refused, most likely -- so this does not invent a second reason. */
        Printf("%s: the system refused\n", (IPTR)name);
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
    }
    return RETURN_FAIL;
}
