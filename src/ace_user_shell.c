#include <stdio.h>

#include "broker_client.h"

/* The unmodified AROS Shell.c is compiled with its main symbol renamed by
 * the Makefile. This tiny host entry point is the ACE-owned seam around it:
 * the process that owns the shell session claims the broker connection before
 * AROS starts reading commands. */
int ace_aros_shell_main(int argc, char **argv);

int main(int argc, char **argv)
{
    if (native_broker_ensure() != 0) {
        fputs("ace-user-shell: broker unavailable\n", stderr);
        return 20;
    }
    if (native_broker_attach() != 0) {
        fputs("ace-user-shell: broker session attach failed\n", stderr);
        return 20;
    }
    return ace_aros_shell_main(argc, argv);
}
