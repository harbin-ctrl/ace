#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <dos/dos.h>

static int resolve_executable_path(const char *argv0, char *path,
                                   size_t path_size)
{
    ssize_t length;

    if (realpath(argv0, path))
        return 0;
    length = readlink("/proc/self/exe", path, path_size - 1);
    if (length < 0 || (size_t)length >= path_size - 1)
        return -1;
    path[length] = '\0';
    return 0;
}

int main(int argc, char **argv)
{
    char executable[PATH_MAX];
    char console_path[PATH_MAX];
    char *slash;
    const char *session = getenv("ACE_SESSION");
    pid_t child;

    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return RETURN_FAIL;
    }
    if (!session || !*session)
        session = "default";
    if (resolve_executable_path(argv[0], executable, sizeof(executable)) != 0) {
        fputs("ace-shell: console unavailable\n", stderr);
        return RETURN_FAIL;
    }
    slash = strrchr(executable, '/');
    if (!slash) {
        fputs("ace-shell: console unavailable\n", stderr);
        return RETURN_FAIL;
    }
    *slash = '\0';
    if (snprintf(console_path, sizeof(console_path), "%s/ace-console",
                 executable) >= (int)sizeof(console_path)) {
        fputs("ace-shell: console unavailable\n", stderr);
        return RETURN_FAIL;
    }
    child = fork();
    if (child < 0) {
        fputs("ace-shell: console unavailable\n", stderr);
        return RETURN_FAIL;
    }
    if (child == 0) {
        (void)setsid();
        execl(console_path, console_path, "--session", session,
              (char *)NULL);
        _exit(RETURN_FAIL);
    }
    return RETURN_OK;
}
