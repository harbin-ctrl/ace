#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dos/dos.h>
#include <proto/exec.h>

#include "broker_client.h"
#include "broker_protocol.h"

/* ACE's broker is the live process registry.  Unlike the original command's
   RootNode CLI list, it spans the separate Linux processes that make up an
   ACE session. */
int ace_command_entry_main(void)
{
    char tasks[AMIGA_BROKER_MAX_PAYLOAD];
    char *arguments = getenv("ACE_COMMAND_ARGUMENTS");
    char *cursor;
    unsigned long requested = 0;
    const char *command = NULL;
    int full = 0;
    int tcb = 0;

    /* Register this short-lived command before taking the broker snapshot. */
    (void)FindTask(NULL);

    if (arguments) {
        for (cursor = strtok(arguments, " \t"); cursor;
             cursor = strtok(NULL, " \t")) {
            if (strcasecmp(cursor, "FULL") == 0)
                full = 1;
            else if (strcasecmp(cursor, "TCB") == 0)
                tcb = 1;
            else if (strncasecmp(cursor, "PROCESS=", 8) == 0)
                requested = strtoul(cursor + 8, NULL, 10);
            else if (strncasecmp(cursor, "COM=", 4) == 0)
                command = cursor + 4;
            else if (isdigit((unsigned char)cursor[0]))
                requested = strtoul(cursor, NULL, 10);
            else if (strcasecmp(cursor, "CLI=ALL") != 0) {
                Printf("Status: bad arguments\n");
                return RETURN_FAIL;
            }
        }
    }
    if (native_broker_task_list(tasks, sizeof(tasks)) != 0) {
        Printf("Status: broker unavailable\n");
        return RETURN_FAIL;
    }
    for (cursor = strtok(tasks, "\n"); cursor; cursor = strtok(NULL, "\n")) {
        char *pid = strchr(cursor, '\t');
        char *name;
        unsigned long id;

        if (!pid)
            continue;
        *pid++ = '\0';
        name = strchr(pid, '\t');
        if (!name)
            continue;
        *name++ = '\0';
        id = strtoul(cursor, NULL, 10);
        if ((requested && id != requested) ||
            (command && strcasecmp(command, name) != 0))
            continue;
        Printf("Process %lu ", id);
        if (full || tcb)
            Printf("stk 8192, pri 0 ");
        if (!tcb || full)
            Printf("Loaded as command: %s", name);
        Printf("\n");
    }
    return RETURN_OK;
}
