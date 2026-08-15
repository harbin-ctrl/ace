#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "ace_amiga_posix.h"

#include "broker_client.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

static int host_path(const char *path, char result[PATH_MAX])
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    return native_broker_resolve_path(path, result, PATH_MAX);
}

FILE *ace_amiga_posix_fopen(const char *path, const char *mode)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return NULL;
    return fopen(resolved, mode);
}

int ace_amiga_posix_open(const char *path, int flags, ...)
{
    char resolved[PATH_MAX];
    mode_t mode = 0;
    va_list arguments;
    int result;

    if (flags & O_CREAT) {
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    if (host_path(path, resolved) != 0)
        return -1;
    if (flags & O_CREAT)
        result = open(resolved, flags, mode);
    else
        result = open(resolved, flags);
    return result;
}

int ace_amiga_posix_mkstemp(char *template_name)
{
    char resolved[PATH_MAX];
    char original_template[PATH_MAX];
    const char *host_basename;
    const char *amiga_slash;
    const char *amiga_colon;
    size_t prefix_length = 0;
    int descriptor;
    int saved_error = 0;

    if (!template_name || strlen(template_name) >= sizeof(original_template)) {
        errno = template_name ? ENAMETOOLONG : EINVAL;
        return -1;
    }
    strcpy(original_template, template_name);
    if (host_path(original_template, resolved) != 0)
        return -1;
    descriptor = mkstemp(resolved);
    if (descriptor < 0)
        return -1;

    /* mkstemp() changes the template in place. Keep the caller's original
       AmigaDOS directory spelling and replace only the generated basename;
       this avoids needing an un-sized host-to-Amiga copy into the caller's
       buffer. */
    host_basename = strrchr(resolved, '/');
    host_basename = host_basename ? host_basename + 1 : resolved;
    amiga_slash = strrchr(original_template, '/');
    amiga_colon = strrchr(original_template, ':');
    if (amiga_slash && (!amiga_colon || amiga_slash > amiga_colon))
        prefix_length = (size_t)(amiga_slash - original_template) + 1;
    else if (amiga_colon)
        prefix_length = (size_t)(amiga_colon - original_template) + 1;
    if (prefix_length + strlen(host_basename) >= PATH_MAX) {
        saved_error = ENAMETOOLONG;
        close(descriptor);
        unlink(resolved);
        errno = saved_error;
        return -1;
    }
    memcpy(template_name, original_template, prefix_length);
    strcpy(template_name + prefix_length, host_basename);
    return descriptor;
}

int ace_amiga_posix_stat(const char *path, struct stat *information)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return stat(resolved, information);
}

int ace_amiga_posix_lstat(const char *path, struct stat *information)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return lstat(resolved, information);
}

int ace_amiga_posix_access(const char *path, int mode)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return access(resolved, mode);
}

int ace_amiga_posix_mkdir(const char *path, mode_t mode)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return mkdir(resolved, mode);
}

DIR *ace_amiga_posix_opendir(const char *path)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return NULL;
    return opendir(resolved);
}

int ace_amiga_posix_rename(const char *old_path, const char *new_path)
{
    char old_resolved[PATH_MAX];
    char new_resolved[PATH_MAX];

    if (host_path(old_path, old_resolved) != 0 ||
        host_path(new_path, new_resolved) != 0)
        return -1;
    return rename(old_resolved, new_resolved);
}

int ace_amiga_posix_unlink(const char *path)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return unlink(resolved);
}

int ace_amiga_posix_remove(const char *path)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return remove(resolved);
}

int ace_amiga_posix_rmdir(const char *path)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return rmdir(resolved);
}

int ace_amiga_posix_chmod(const char *path, mode_t mode)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return chmod(resolved, mode);
}

int ace_amiga_posix_utime(const char *path, const struct utimbuf *times)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return utime(resolved, times);
}

int ace_amiga_posix_utimes(const char *path,
                           const struct timeval times[2])
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return utimes(resolved, times);
}

int ace_amiga_posix_symlink(const char *target, const char *link_path)
{
    char link_resolved[PATH_MAX];

    if (host_path(link_path, link_resolved) != 0)
        return -1;
    /* The target is data in a symlink and may intentionally be relative to
       the link. Only the link pathname itself is an AmigaDOS pathname. */
    return symlink(target, link_resolved);
}

ssize_t ace_amiga_posix_readlink(const char *path, char *buffer,
                                 size_t buffer_size)
{
    char resolved[PATH_MAX];

    if (host_path(path, resolved) != 0)
        return -1;
    return readlink(resolved, buffer, buffer_size);
}
