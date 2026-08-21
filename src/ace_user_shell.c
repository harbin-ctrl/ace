#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dos/dos.h>
#include <dos/stdio.h>

#include "broker_client.h"
#include "broker_protocol.h"
#include "ace_shell_break.h"
#include "ace_modes.h"
#include "native_host.h"

/*
 * A Shell exits with the return code of the last command it ran. That is what
 * makes RC mean anything to whoever started the Shell: System() hands it to a
 * Rexx script's RC after ADDRESS COMMAND, and Execute hands it to the script
 * that ran. This process used to exit 0 no matter how the session went, so a
 * command that failed was indistinguishable from one that worked -- the error
 * was printed and the caller was told everything was fine.
 *
 * ace_aros_shell_main() is the unmodified AROS Shell and does not return the
 * code, so it is read from the broker, which is where every ACE command
 * records it and where the Cli() structure gets cli_ReturnCode from. Its
 * GETCLI payload is newline-separated with the return code first.
 *
 * Not an error if the broker cannot be asked: a Shell that got this far has
 * run, and reporting a failure that did not happen is worse than reporting
 * the success that most sessions end in.
 */
static LONG shell_return_code(void)
{
    char state[AMIGA_BROKER_MAX_PAYLOAD];
    char *newline;

    if (native_broker_getcli(state, sizeof(state)) != 0)
        return RETURN_OK;
    newline = strchr(state, '\n');
    if (newline)
        *newline = '\0';
    return (LONG)strtol(state, NULL, 10);
}

static void publish_shell_path(void)
{
    char executable[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable) - 1);

    if (length < 0 || (size_t)length >= sizeof(executable) - 1)
        return;
    executable[length] = '\0';
    (void)setenv("ACE_USER_SHELL", executable, 1);
}

/* The unmodified AROS Shell.c is compiled with its main symbol renamed by
 * the Makefile. This tiny host entry point is the ACE-owned seam around it:
 * the process that owns the shell session claims the broker connection before
 * AROS starts reading commands. */
int ace_aros_shell_main(int argc, char **argv);

/*
 * The scripts a starting shell runs, in order.
 *
 * On a real Amiga these have two different owners. S:Startup-Sequence is the
 * boot script, run once by the Initial CLI because rom/dos/boot.c opens it
 * and passes it as SYS_ScriptInput; S:Shell-Startup is the per-shell script,
 * run by every new shell after that. ACE has one kind of shell and no boot
 * to distinguish, so it runs both, and S:ACE-Startup after them for whatever
 * is true of this system and not of an Amiga.
 *
 * Missing files are skipped. A system with none of them starts interactively
 * with no ceremony, which is what ACE did before it had any of this.
 */
static const char *const startup_scripts[] = {
    "S:Startup-Sequence",
    "S:Shell-Startup",
    "S:ACE-Startup",
};

/*
 * Splice the scripts that exist into one stream and hand it to the Shell as
 * cli_CurrentInput, which is exactly what boot.c does with SYS_ScriptInput:
 * Shell.c reads commands from there instead of the keyboard, and when it runs
 * out it closes the handle, falls back to cli_StandardInput and carries on
 * interactively.
 *
 * One file rather than three because the Shell has one cli_CurrentInput and
 * no way to be handed a second when the first ends. Concatenating is how
 * AROS's own Execute.c splices a script into a running shell, for the same
 * reason.
 */
static void run_startup_scripts(void)
{
    const char *only = getenv(ACE_STARTUP_SCRIPT_VARIABLE);
    const char *script_descriptor = getenv(ACE_SCRIPT_INPUT_VARIABLE);
    const char *const *scripts = startup_scripts;
    size_t script_count = sizeof(startup_scripts) /
                          sizeof(startup_scripts[0]);
    char name[64];
    BPTR combined;
    BPTR script;
    size_t index;
    int wrote = 0;

    /* Started by Execute to run one script and stop, rather than by a user
       to be a shell. The startup set belongs to a shell somebody is going to
       type at; this one runs what it was given and ends. */
    if (only && *only) {
        scripts = &only;
        script_count = 1;
        native_set_interactive(0);
    }
    /* SYS_ScriptInput is already an open CLI input stream. NewCLI supplies
       it this way so the requested FROM file runs before the new shell's
       interactive input. It takes precedence over ACE's startup bundle;
       otherwise the bundle would replace the handler-selected script. */
    if ((!only || !*only) && script_descriptor && *script_descriptor) {
        if (native_cli_script_input())
            return;
    }
    snprintf(name, sizeof(name), "T:Shell-Startup-%ld", (long)getpid());
    combined = Open(name, MODE_NEWFILE);
    if (!combined)
        return;
    for (index = 0; index < script_count; index++) {
        LONG character;

        script = Open(scripts[index], MODE_OLDFILE);
        if (!script)
            continue;
        while ((character = FGetC(script)) != ENDSTREAMCH)
            FPutC(combined, character);
        Close(script);
        /* A script that does not end in a newline would otherwise run its
           last line into the first line of the next one. */
        FPutC(combined, '\n');
        wrote = 1;
    }
    Close(combined);
    if (!wrote) {
        DeleteFile(name);
        return;
    }
    /* Read-write, because Execute splices a script into this file ahead of
       the part the shell has not read yet -- which is how a command in
       another process gets commands into this one's input. */
    combined = Open(name, MODE_READWRITE);
    /* Unlinked while open: the stream stays readable, and nothing is left in
       T: if this shell dies before reaching the end of it. */
    DeleteFile(name);
    if (combined)
        native_cli_set_script_input((FILE *)combined);
}

int main(int argc, char **argv)
{
    struct ace_mode_options modes;
    int status;

    if (ace_mode_parse(&argc, argv, &modes) != 0 || argc != 1) {
        fprintf(stderr, "usage: %s [--root]\n", argv[0]);
        return 20;
    }
    if (ace_mode_configure(&modes) != 0) {
        /* Being root is the one failure worth explaining rather than
           reporting: the user did something reasonable and needs to know
           that ACE gets its privilege elsewhere. */
        if (errno == EPERM)
            fprintf(stderr,
                    "ace-user-shell: ACE must be started as a normal user; privileged "
                    "operations\nare provided by the ACE fmm.\n");
        else
            fprintf(stderr, "ace-user-shell: requested mode is unavailable: %s\n",
                    strerror(errno));
        return 20;
    }

    publish_shell_path();
    if (native_broker_ensure() != 0) {
        fputs("ace-user-shell: broker unavailable\n", stderr);
        return 20;
    }
    if (native_broker_attach() != 0) {
        fputs("ace-user-shell: broker session attach failed\n", stderr);
        return 20;
    }
    /* A device-view shell must join the broker's mount namespace before it
     * creates the break-dispatch thread: Linux does not permit a
     * multithreaded process to change mount namespaces. */
    ace_shell_break_init();
    /* Vim's unchanged Amiga backend asks AmigaDOS GetVar("TERM"), rather
       than POSIX getenv(), when FEAT_ARP is present.  The ACE GUI process
       also exports TERM for host-side consumers, but the broker-backed DOS
       environment is the authoritative one for AROS programs. */
    if (native_broker_setvar("TERM", "amiga", AMIGA_BROKER_VAR_LOCAL) != 0) {
        fputs("ace-user-shell: cannot establish Amiga terminal environment\n",
              stderr);
        return 20;
    }
    run_startup_scripts();
    status = ace_aros_shell_main(argc, argv);
    native_publish_shell_result();
    /* A non-zero here is the Shell itself failing, and is not a command's
       return code to be second-guessed. */
    if (status != 0)
        return status;
    /*
     * Only a Shell started to run one command on somebody's behalf reports
     * that command's code. That is the System() case: launch_command() writes
     * the command to a script, names it in ACE_STARTUP_SCRIPT, and waits for
     * this process, so this exit status is the only way the code gets back to
     * the caller -- and it is what a Rexx script's RC after ADDRESS COMMAND
     * ends up reading.
     *
     * A Shell somebody is typing at exits 0 as it always has. Its last
     * command's code is answered by Why and by the RC variable, and is not the
     * exit status of the session: a window closed after a command that failed
     * has not itself failed, and anything waiting on the process -- the
     * console does waitpid() on it -- would start reading a failure into it.
     */
    if (!getenv(ACE_STARTUP_SCRIPT_VARIABLE))
        return 0;
    return (int)shell_return_code();
}
