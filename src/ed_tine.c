#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* ED is an Amiga command from the user's point of view.  Its implementation
   is the TINE guest executable installed beside ACE's host programs. */

static void
usage(void)
{
    fputs("usage: ED [FROM] FILE [SIZE n] [WITH file] [WINDOW spec] "
          "[TABS n] [WIDTH|COLS n] [HEIGHT|ROWS n]\n", stderr);
}

static void
template(void)
{
    puts("FROM/A,SIZE/N,WITH/K,WINDOW/K,TABS/N,WIDTH=COLS/N,HEIGHT=ROWS/N");
}

static bool
number(const char *s)
{
    if (!s || !*s)
        return false;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return false;
    return true;
}

static bool
keyword(const char *s, const char *name)
{
    return strcasecmp(s, name) == 0;
}

int main(int argc, char **argv)
{
    char executable[PATH_MAX];
    char tine[PATH_MAX] = {0};
    const char *configured = getenv("ACE_TINE_BINARY");
    ssize_t length;
    char *slash;
    char **tine_argv = NULL;
    const char *file;
    const char *with = NULL;
    const char *size = NULL;
    const char *tabs = NULL;
    const char *width = NULL;
    const char *height = NULL;
    const char *window = NULL;
    int input = 1;

    if (argc == 2 && keyword(argv[1], "?")) {
        template();
        return 0;
    }
    if (input < argc && keyword(argv[input], "FROM"))
        input++;
    if (input >= argc) {
        usage();
        return 20;
    }
    file = argv[input++];

    while (input < argc) {
        const char *option = argv[input++];
        const char **value = NULL;
        bool numeric = false;

        if (keyword(option, "SIZE")) {
            value = &size;
            numeric = true;
        } else if (keyword(option, "WITH")) {
            value = &with;
        } else if (keyword(option, "WINDOW")) {
            value = &window;
        } else if (keyword(option, "TABS")) {
            value = &tabs;
            numeric = true;
        } else if (keyword(option, "WIDTH") || keyword(option, "COLS")) {
            value = &width;
            numeric = true;
        } else if (keyword(option, "HEIGHT") || keyword(option, "ROWS")) {
            value = &height;
            numeric = true;
        } else {
            usage();
            return 20;
        }

        if (input >= argc || (numeric && !number(argv[input])) || *value) {
            usage();
            return 20;
        }
        *value = argv[input++];
    }

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

    tine_argv = calloc((size_t)argc + 16, sizeof(*tine_argv));
    if (!tine_argv) {
        errno = ENOMEM;
        goto fail;
    }

    int output = 0;
    tine_argv[output++] = tine;
    tine_argv[output++] = "-E";
    if (size) {
        tine_argv[output++] = "-S";
        tine_argv[output++] = (char *)size;
    }
    if (tabs) {
        tine_argv[output++] = "-T";
        tine_argv[output++] = (char *)tabs;
    }
    if (width) {
        tine_argv[output++] = "-W";
        tine_argv[output++] = (char *)width;
    }
    if (height) {
        tine_argv[output++] = "-H";
        tine_argv[output++] = (char *)height;
    }
    if (window) {
        tine_argv[output++] = "-G";
        tine_argv[output++] = (char *)window;
    }
    if (with) {
        tine_argv[output++] = "-C";
        tine_argv[output++] = (char *)with;
    }
    tine_argv[output++] = (char *)file;
    execv(tine, tine_argv);

fail:
    free(tine_argv);
    fprintf(stderr, "ED: %s: %s\n", tine[0] ? tine : "tine",
            strerror(errno));
    return 20;
}
