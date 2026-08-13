#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dos/dos.h>

/*
 * LNX is the deliberate escape hatch from the AmigaDOS command world.
 * It executes one Linux program directly. No shell is involved: arguments
 * are already separated by the AROS command line machinery, and the
 * inherited standard descriptors lead back to ACE's CON: stream.
 */
static int exec_linux_program(const char *program, char **arguments)
{
    const char *path;
    const char *cursor;
    int saved_error = ENOENT;

    if (strchr(program, '/')) {
        execv(program, arguments);
        return -1;
    }

    path = getenv("PATH");
    if (!path || !*path)
        path = ".";
    cursor = path;
    while (1) {
        const char *separator = strchr(cursor, ':');
        size_t directory_length = separator ?
                                  (size_t)(separator - cursor) : strlen(cursor);
        char candidate[PATH_MAX];
        int written;

        written = snprintf(candidate, sizeof(candidate), "%.*s%s%s",
                           (int)directory_length,
                           directory_length ? cursor : ".",
                           directory_length ? "/" : "",
                           program);
        if (written >= 0 && (size_t)written < sizeof(candidate)) {
            execv(candidate, arguments);
            if (errno != ENOENT && errno != ENOTDIR)
                saved_error = errno;
        } else {
            saved_error = ENAMETOOLONG;
        }
        if (!separator)
            break;
        cursor = separator + 1;
    }
    errno = saved_error;
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "LNX: command required\n");
        return RETURN_FAIL;
    }

    if (exec_linux_program(argv[1], &argv[1]) == 0)
        return RETURN_OK;
    fprintf(stderr, "LNX: %s: %s\n", argv[1], strerror(errno));
    return RETURN_FAIL;
}
