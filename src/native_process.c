#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>

#include "broker_client.h"
#include "native_host.h"

static int executable_directory(char *directory, size_t directory_size)
{
    char executable[PATH_MAX];
    char *slash;
    ssize_t length;

    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1)
        return -1;
    executable[length] = '\0';
    slash = strrchr(executable, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (strlen(executable) >= directory_size)
        return -1;
    strcpy(directory, executable);
    return 0;
}

static int child_session_name(char *session, size_t session_size)
{
    const char *parent = getenv("ACE_SESSION");
    struct timespec now;

    if (!parent || !*parent)
        parent = "default";
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    if (snprintf(session, session_size, "%s-child-%ld-%ld", parent,
                 (long)getpid(), (long)now.tv_nsec) >= (int)session_size)
        return -1;
    return 0;
}

static int launch_console(BPTR input)
{
    char directory[PATH_MAX];
    char console_path[PATH_MAX];
    char session[128];
    pid_t child;

    if (!native_console_is_handle(input) ||
        executable_directory(directory, sizeof(directory)) != 0 ||
        snprintf(console_path, sizeof(console_path), "%s/ace-console",
                 directory) >= (int)sizeof(console_path) ||
        child_session_name(session, sizeof(session)) != 0 ||
        native_broker_ensure() != 0 ||
        native_broker_clone_session(session) != 0) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return -1;
    }

    child = fork();
    if (child < 0) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return -1;
    }
    if (child == 0) {
        (void)setsid();
        execl(console_path, console_path, "--session", session,
              (char *)NULL);
        _exit(RETURN_FAIL);
    }
    return 0;
}

LONG SystemTagList(CONST_STRPTR command, const struct TagItem *tags)
{
    const struct TagItem *tag;
    BPTR input = BNULL;

    (void)command;
    for (tag = tags; tag && tag->ti_Tag != TAG_DONE; tag++) {
        if (tag->ti_Tag == SYS_Input)
            input = (BPTR)(uintptr_t)tag->ti_Data;
    }
    if (launch_console(input) != 0)
        return -1;
    native_console_close(input);
    return 0;
}
