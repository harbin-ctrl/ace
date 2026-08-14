#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "broker_client.h"

/* Vim's untouched Amiga headers map mch_open(), stat(), and friends to the
   host C calls. These ACE-side entry points give those calls the same broker
   path translation as DOS Open(), without changing a Vim source line. */
static int vim_host_path(const char *name, char *result, size_t result_size)
{
    char executable[PATH_MAX];
    ssize_t executable_length;
    const char *relative;
    size_t prefix_length;
    int use_runtime;

    if (!name) {
        errno = EINVAL;
        return -1;
    }
    if (strncasecmp(name, "PROGDIR:", 8) == 0) {
        prefix_length = 8;
        use_runtime = 0;
    } else if (strncasecmp(name, "VIM:", 4) == 0) {
        prefix_length = 4;
        use_runtime = 1;
    } else if (strncasecmp(name, "VIMRUNTIME:", 11) == 0) {
        prefix_length = 11;
        use_runtime = 1;
    } else {
        prefix_length = 0;
        use_runtime = 0;
    }
    if (prefix_length != 0) {
        executable_length = readlink("/proc/self/exe", executable,
                                    sizeof(executable) - 1);
        if (executable_length < 0 ||
            (size_t)executable_length >= sizeof(executable) - 1) {
            errno = ENOENT;
            return -1;
        }
        executable[executable_length] = '\0';
        {
            char *slash = strrchr(executable, '/');
            relative = name + prefix_length;

            if (!slash || snprintf(result, result_size,
                                   use_runtime ? "%.*s/runtime/%s" :
                                                 "%.*s/%s",
                                   (int)(slash - executable), executable,
                                   relative) >= (int)result_size) {
                errno = ENAMETOOLONG;
                return -1;
            }
        }
        return 0;
    }
    /* A path passed to the ACE executable by the host launcher is already a
       host path. AmigaDOS paths (assigns, PROGDIR:, and relative names) still
       go through the broker. */
    if (name[0] == '/' && !strchr(name + 1, ':')) {
        if (snprintf(result, result_size, "%s", name) >=
            (int)result_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        return 0;
    }
    return native_broker_resolve_path(name, result, result_size);
}

int ace_vim_open(const char *name, int flags, ...)
{
    char resolved[PATH_MAX];
    va_list arguments;
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    if (vim_host_path(name, resolved, sizeof(resolved)) != 0)
        return -1;
    return flags & O_CREAT ? open(resolved, flags, mode) : open(resolved, flags);
}

FILE *ace_vim_fopen(const char *name, const char *mode)
{
    char resolved[PATH_MAX];

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0)
        return NULL;
    return fopen(resolved, mode);
}

int ace_vim_stat(const char *name, struct stat *information)
{
    char resolved[PATH_MAX];

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0)
        return -1;
    return stat(resolved, information);
}

int ace_vim_lstat(const char *name, struct stat *information)
{
    char resolved[PATH_MAX];

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0)
        return -1;
    return lstat(resolved, information);
}

int ace_vim_access(const char *name, int mode)
{
    char resolved[PATH_MAX];

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0)
        return -1;
    return access(resolved, mode);
}

/* os_amiga.c uses DOS locks for these probes. Keep that untouched backend
   intact and redirect only the Vim build's filesystem queries through the
   same ACE path adapter used by fopen/stat. */
int ace_vim_isdir(char *name)
{
    char resolved[PATH_MAX];
    struct stat information;

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0 ||
        stat(resolved, &information) != 0)
        return 0;
    return S_ISDIR(information.st_mode);
}

long ace_vim_getperm(char *name)
{
    char resolved[PATH_MAX];
    struct stat information;

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0 ||
        stat(resolved, &information) != 0)
        return -1;
    return (long)information.st_mode;
}

int ace_vim_remove(const char *name)
{
    char resolved[PATH_MAX];

    if (vim_host_path(name, resolved, sizeof(resolved)) != 0)
        return -1;
    return remove(resolved);
}

int ace_vim_rename(const char *old_name, const char *new_name)
{
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];

    if (vim_host_path(old_name, old_path, sizeof(old_path)) != 0 ||
        vim_host_path(new_name, new_path, sizeof(new_path)) != 0)
        return -1;
    return rename(old_path, new_path);
}
