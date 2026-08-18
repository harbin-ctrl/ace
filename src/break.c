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

static int find_named_task(const char *name, uint64_t *task_id)
{
    char tasks[AMIGA_BROKER_MAX_PAYLOAD];
    char *line;

    if (native_broker_task_list(tasks, sizeof(tasks)) != 0)
        return -1;
    for (line = strtok(tasks, "\n"); line; line = strtok(NULL, "\n")) {
        char *pid = strchr(line, '\t');
        char *task_name;

        if (!pid)
            continue;
        task_name = strchr(pid + 1, '\t');
        if (!task_name)
            continue;
        *task_name++ = '\0';
        if (strcasecmp(task_name, name) == 0) {
            *task_id = strtoull(line, NULL, 10);
            return 0;
        }
    }
    return -1;
}

int ace_command_entry_main(void)
{
    char *arguments = getenv("ACE_COMMAND_ARGUMENTS");
    char *word;
    uint64_t task_id = 0;
    const char *name = NULL;
    uint32_t signals = 0;

    (void)FindTask(NULL); /* include this command in the live task registry */
    if (arguments) {
        for (word = strtok(arguments, " \t\r\n"); word;
             word = strtok(NULL, " \t\r\n")) {
            if (isdigit((unsigned char)word[0]) && !task_id)
                task_id = strtoull(word, NULL, 10);
            else if (strncasecmp(word, "PROCESS=", 8) == 0)
                task_id = strtoull(word + 8, NULL, 10);
            else if (strncasecmp(word, "NAME=", 5) == 0)
                name = word + 5;
            else if (strcasecmp(word, "C") == 0)
                signals |= SIGBREAKF_CTRL_C;
            else if (strcasecmp(word, "D") == 0)
                signals |= SIGBREAKF_CTRL_D;
            else if (strcasecmp(word, "E") == 0)
                signals |= SIGBREAKF_CTRL_E;
            else if (strcasecmp(word, "F") == 0)
                signals |= SIGBREAKF_CTRL_F;
            else if (strcasecmp(word, "ALL") == 0)
                signals |= SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_D |
                           SIGBREAKF_CTRL_E | SIGBREAKF_CTRL_F;
            else if (strcmp(word, "?") == 0) {
                Printf("PROCESS/N,NAME/K,C/S,D/S,E/S,F/S,ALL/S\n");
                return RETURN_OK;
            } else {
                Printf("Break: bad arguments\n");
                return RETURN_FAIL;
            }
        }
    }
    if (!task_id && name && find_named_task(name, &task_id) != 0) {
        Printf("Break: Task %s does not exist.\n", name);
        return RETURN_FAIL;
    }
    if (!task_id) {
        Printf("Break: Either PROCESS or NAME is required.\n");
        return RETURN_FAIL;
    }
    if (!signals)
        signals = SIGBREAKF_CTRL_C;
    if (native_broker_task_signal(task_id, signals) != 0) {
        Printf("Break: Process %llu does not exist.\n",
               (unsigned long long)task_id);
        return RETURN_FAIL;
    }
    return RETURN_OK;
}
