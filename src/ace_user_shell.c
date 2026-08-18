#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dos/dos.h>
#include <dos/stdio.h>

#include "broker_client.h"
#include "broker_protocol.h"
#include "ace_shell_break.h"
#include "native_host.h"

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
    publish_shell_path();
    ace_shell_break_init();
    if (native_broker_ensure() != 0) {
        fputs("ace-user-shell: broker unavailable\n", stderr);
        return 20;
    }
    if (native_broker_attach() != 0) {
        fputs("ace-user-shell: broker session attach failed\n", stderr);
        return 20;
    }
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
    return ace_aros_shell_main(argc, argv);
}
