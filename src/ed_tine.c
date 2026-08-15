#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ED is an Amiga command from the user's point of view.  Its implementation
   is the TINE guest executable installed beside ACE's host programs. */
int main(int argc, char **argv)
{
    char executable[PATH_MAX];
    char tine[PATH_MAX] = {0};
    const char *configured = getenv("ACE_TINE_BINARY");
    ssize_t length;
    char *slash;

    if (configured && *configured) {
        if (strlen(configured) >= sizeof(tine)) {
            errno = ENAMETOOLONG;
            goto fail;
        }
        strcpy(tine, configured);
    } else {
        length = readlink("/proc/self/exe", executable,
                          sizeof(executable) - 1);
        if (length < 0 || (size_t)length >= sizeof(executable) - 1)
            goto fail;
        executable[length] = '\0';
        slash = strrchr(executable, '/');
        if (!slash || snprintf(tine, sizeof(tine), "%.*s/tine",
                               (int)(slash - executable), executable) >=
                       (int)sizeof(tine)) {
            errno = ENOENT;
            goto fail;
        }
    }

    argv[0] = tine;
    execv(tine, argv);

fail:
    fprintf(stderr, "ED: %s: %s\n", tine[0] ? tine : "tine",
            strerror(errno));
    return 20;
}
