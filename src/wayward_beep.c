#define _POSIX_C_SOURCE 200809L

#include "wayward_beep.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include <exec/libraries.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>

#include "proto/intuition.h"

/* This is the private signal shared with onscreen-windows/labwc. */
#define ACE_WAYWARD_BEEP_SIGNAL SIGUSR1

static int wayward_labwc_pid(pid_t *result)
{
    const char *text = getenv("LABWC_PID");
    char *end;
    long value;

    if (!text || !*text)
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno || end == text || *end || value <= 0 ||
        value > INT_MAX)
        return -1;

#ifdef __linux__
    {
        char path[64];
        char comm[64] = {0};
        FILE *stream;

        if (snprintf(path, sizeof(path), "/proc/%ld/comm", value) >=
            (int)sizeof(path))
            return -1;
        stream = fopen(path, "r");
        if (!stream)
            return -1;
        if (!fgets(comm, sizeof(comm), stream)) {
            fclose(stream);
            return -1;
        }
        fclose(stream);
        if (strncmp(comm, "labwc", strlen("labwc")) != 0)
            return -1;
    }
#endif

    *result = (pid_t)value;
    return 0;
}

void WaywardBeep(struct Screen *screen)
{
	pid_t pid;
	union sigval value;
	uint32_t screen_mask = screen ? (uint32_t)(uintptr_t)screen
		: ACE_WAYWARD_BEEP_ALL_SCREENS;

	screen_mask &= ACE_WAYWARD_BEEP_SCREEN_MASK;
    if (!screen_mask || wayward_labwc_pid(&pid) != 0)
        return;

    value.sival_int = (int)screen_mask;
    (void)sigqueue(pid, ACE_WAYWARD_BEEP_SIGNAL, value);
}

/* ACE's host Intuition library is deliberately only a tiny boundary here:
 * Beep opens the library, calls DisplayBeep(), and closes it.  The public
 * base is real enough for the AROS command to take &LibNode, while the actual
 * display work belongs to Wayward. */
static struct IntuitionBase ace_intuition_base;

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    (void)version;
    if (name && strcasecmp(name, "intuition.library") == 0)
        return &ace_intuition_base.LibNode;
    return NULL;
}

void CloseLibrary(struct Library *library)
{
    (void)library;
}

void DisplayBeep(struct Screen *screen)
{
	WaywardBeep(screen);
}
