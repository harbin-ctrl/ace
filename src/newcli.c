#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "broker_client.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int executable_directory(const char *argv0, char *directory,
                                size_t directory_size)
{
    char path[PATH_MAX];
    char *slash;

    if (!realpath(argv0, path))
        return -1;
    slash = strrchr(path, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (strlen(path) >= directory_size)
        return -1;
    strcpy(directory, path);
    return 0;
}

int main(int argc, char **argv)
{
    char directory[PATH_MAX];
    char console_path[PATH_MAX];
    char child_session[128];
    const char *parent_session;
    struct timespec now;
    pid_t child;

    (void)argc;
    (void)argv;
    if (executable_directory(argv[0], directory, sizeof(directory)) != 0 ||
        snprintf(console_path, sizeof(console_path), "%s/amiga-console", directory) >=
        (int)sizeof(console_path)) {
        fputs("NewCLI: console unavailable\n", stderr);
        return 20;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 20;
    parent_session = getenv("AMIGA_SHELL_SESSION");
    if (!parent_session || !*parent_session)
        parent_session = "default";
    snprintf(child_session, sizeof(child_session), "%s-child-%ld-%ld",
             parent_session, (long)getpid(),
             (long)now.tv_nsec);
    if (native_broker_clone_session(child_session) != 0) {
        fputs("NewCLI: broker unavailable\n", stderr);
        return 20;
    }

    child = fork();
    if (child < 0)
        return 20;
    if (child == 0) {
        (void)setsid();
        execl(console_path, console_path, "--session", child_session,
              (char *)NULL);
        _exit(20);
    }
    return 0;
}
