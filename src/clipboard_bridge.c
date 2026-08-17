#define _POSIX_C_SOURCE 200809L

#include "clipboard_bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ACE_CLIPBOARD_MAX_SIZE (64u * 1024u * 1024u)

static int make_directory_path(const char *path)
{
    char work[PATH_MAX];
    size_t length;

    if (!path || (length = strlen(path)) == 0 || length >= sizeof(work)) {
        errno = EINVAL;
        return -1;
    }
    strcpy(work, path);
    for (char *cursor = work + 1; *cursor; cursor++) {
        if (*cursor != '/')
            continue;
        *cursor = '\0';
        if (*work && mkdir(work, 0700) != 0 && errno != EEXIST)
            return -1;
        *cursor = '/';
    }
    if (mkdir(work, 0700) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

int ace_clipboard_store_root(char *result, size_t result_size)
{
    const char *configured = getenv("ACE_CLIPBOARD_DIR");
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    int written;

    if (!result || result_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (configured && *configured)
        written = snprintf(result, result_size, "%s", configured);
    else if (runtime_dir && *runtime_dir)
        written = snprintf(result, result_size, "%s/ace/clips", runtime_dir);
    else
        written = snprintf(result, result_size, "/tmp/aros");
    if (written < 0 || (size_t)written >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int ace_clipboard_store_prepare(void)
{
    char root[PATH_MAX];

    if (ace_clipboard_store_root(root, sizeof(root)) != 0)
        return -1;
    return make_directory_path(root);
}

static int unit_path(unsigned unit, const char *suffix, char *result,
                     size_t result_size)
{
    char root[PATH_MAX];
    int written;

    if (unit >= ACE_CLIPBOARD_UNIT_COUNT || ace_clipboard_store_root(
            root, sizeof(root)) != 0) {
        errno = EINVAL;
        return -1;
    }
    written = snprintf(result, result_size, "%s/clip%u%s", root, unit,
                       suffix ? suffix : "");
    if (written < 0 || (size_t)written >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int lock_unit(unsigned unit, int operation, int *lock_fd)
{
    char path[PATH_MAX];
    int fd;

    if (ace_clipboard_store_prepare() != 0 ||
        unit_path(unit, ".lock", path, sizeof(path)) != 0)
        return -1;
    fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, operation) != 0) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    *lock_fd = fd;
    return 0;
}

static void unlock_unit(int lock_fd)
{
    if (lock_fd >= 0) {
        (void)flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
}

static int read_all_fd(int fd, unsigned char *buffer, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t count = read(fd, buffer + offset, length - offset);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0) {
            errno = EIO;
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

static int write_all_fd(int fd, const unsigned char *buffer, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t count = write(fd, buffer + offset, length - offset);

        if (count < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (count == 0) {
            errno = EIO;
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

int ace_clipboard_store_load(unsigned unit, unsigned char **data, size_t *size)
{
    char path[PATH_MAX];
    struct stat information;
    unsigned char *loaded = NULL;
    int lock_fd = -1;
    int fd = -1;
    int error;

    if (!data || !size || unit_path(unit, NULL, path, sizeof(path)) != 0)
        return -1;
    *data = NULL;
    *size = 0;
    if (lock_unit(unit, LOCK_SH, &lock_fd) != 0)
        return -1;
    if (stat(path, &information) != 0) {
        error = errno;
        unlock_unit(lock_fd);
        errno = error;
        return -1;
    }
    if (!S_ISREG(information.st_mode) || information.st_size < 0 ||
        (uintmax_t)information.st_size > ACE_CLIPBOARD_MAX_SIZE) {
        unlock_unit(lock_fd);
        errno = EINVAL;
        return -1;
    }
    if (information.st_size != 0) {
        loaded = malloc((size_t)information.st_size);
        if (!loaded) {
            unlock_unit(lock_fd);
            errno = ENOMEM;
            return -1;
        }
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || (loaded && read_all_fd(fd, loaded,
                                         (size_t)information.st_size) != 0)) {
        error = errno;
        free(loaded);
        if (fd >= 0)
            close(fd);
        unlock_unit(lock_fd);
        errno = error;
        return -1;
    }
    close(fd);
    unlock_unit(lock_fd);
    *data = loaded;
    *size = (size_t)information.st_size;
    return 0;
}

static int store_commit_raw(unsigned unit, const void *data, size_t size)
{
    char path[PATH_MAX];
    char temporary[PATH_MAX];
    unsigned char zero = 0;
    int lock_fd = -1;
    int fd = -1;
    int error;

    if (size > ACE_CLIPBOARD_MAX_SIZE || (size != 0 && !data)) {
        errno = EINVAL;
        return -1;
    }
    if (unit_path(unit, NULL, path, sizeof(path)) != 0 ||
        unit_path(unit, ".XXXXXX", temporary, sizeof(temporary)) != 0)
        return -1;
    if (lock_unit(unit, LOCK_EX, &lock_fd) != 0)
        return -1;
    fd = mkstemp(temporary);
    if (fd < 0) {
        error = errno;
        unlock_unit(lock_fd);
        errno = error;
        return -1;
    }
    if (fchmod(fd, 0600) != 0 ||
        (size && write_all_fd(fd, data, size) != 0) ||
        (!size && write_all_fd(fd, &zero, 0) != 0) ||
        fsync(fd) != 0 || close(fd) != 0 || rename(temporary, path) != 0) {
        error = errno;
        close(fd);
        unlink(temporary);
        unlock_unit(lock_fd);
        errno = error;
        return -1;
    }
    unlock_unit(lock_fd);
    return 0;
}

/* Return a four-byte big-endian IFF length, or zero when the input is short. */
static uint32_t read_be32(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int id_is(const unsigned char *bytes, char a, char b, char c, char d)
{
    return bytes[0] == (unsigned char)a && bytes[1] == (unsigned char)b &&
           bytes[2] == (unsigned char)c && bytes[3] == (unsigned char)d;
}

/* Extract all FTXT/CHRS data, rejecting an opaque or malformed clipboard. */
static int extract_text(const unsigned char *data, size_t size,
                        unsigned char **text, size_t *text_size)
{
    size_t position = 0;
    size_t capacity = 0;
    unsigned char *result = NULL;
    int found = 0;

    if (!data || size < 12 || !id_is(data, 'F', 'O', 'R', 'M') ||
        read_be32(data + 4) < 4 ||
        (uintmax_t)read_be32(data + 4) + 8 > size ||
        !id_is(data + 8, 'F', 'T', 'X', 'T')) {
        errno = EINVAL;
        return -1;
    }
    position = 12;
    while (position < size) {
        uint32_t length;
        size_t end;

        if (size - position < 8) {
            free(result);
            errno = EINVAL;
            return -1;
        }
        length = read_be32(data + position + 4);
        if ((uintmax_t)length > size - position - 8) {
            free(result);
            errno = EINVAL;
            return -1;
        }
        end = position + 8 + (size_t)length;
        if (id_is(data + position, 'C', 'H', 'R', 'S')) {
            size_t needed = (text_size ? *text_size : 0) + length;

            if (needed < (text_size ? *text_size : 0)) {
                free(result);
                errno = EOVERFLOW;
                return -1;
            }
            if (needed > capacity) {
                size_t new_capacity = capacity ? capacity : 256;
                unsigned char *grown;

                while (new_capacity < needed) {
                    if (new_capacity > ACE_CLIPBOARD_MAX_SIZE / 2) {
                        new_capacity = needed;
                        break;
                    }
                    new_capacity *= 2;
                }
                grown = realloc(result, new_capacity);
                if (!grown) {
                    free(result);
                    errno = ENOMEM;
                    return -1;
                }
                result = grown;
                capacity = new_capacity;
            }
            memcpy(result + (text_size ? *text_size : 0), data + position + 8,
                   length);
            if (text_size)
                *text_size = needed;
            found = 1;
        }
        position = end + (length & 1u);
        if (position > size) {
            free(result);
            errno = EINVAL;
            return -1;
        }
    }
    if (!found) {
        free(result);
        errno = EINVAL;
        return -1;
    }
    *text = result;
    return 0;
}

static int make_text_clip(const unsigned char *text, size_t text_size,
                          unsigned char **data, size_t *size)
{
    size_t padding = text_size & 1u;
    size_t total = 20 + text_size + padding;
    unsigned char *result;

    if (text_size > ACE_CLIPBOARD_MAX_SIZE - 20 - padding) {
        errno = EOVERFLOW;
        return -1;
    }
    result = calloc(1, total);
    if (!result) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(result, "FORM", 4);
    result[4] = (unsigned char)((12 + text_size + padding) >> 24);
    result[5] = (unsigned char)((12 + text_size + padding) >> 16);
    result[6] = (unsigned char)((12 + text_size + padding) >> 8);
    result[7] = (unsigned char)(12 + text_size + padding);
    memcpy(result + 8, "FTXTCHRS", 8);
    result[16] = (unsigned char)(text_size >> 24);
    result[17] = (unsigned char)(text_size >> 16);
    result[18] = (unsigned char)(text_size >> 8);
    result[19] = (unsigned char)text_size;
    if (text_size)
        memcpy(result + 20, text, text_size);
    *data = result;
    *size = total;
    return 0;
}

static const char *find_executable(const char *name)
{
    static char result[PATH_MAX];
    const char *path = getenv("PATH");
    const char *cursor;

    if (!name || !*name)
        return NULL;
    if (strchr(name, '/'))
        return access(name, X_OK) == 0 ? name : NULL;
    if (!path)
        return NULL;
    cursor = path;
    while (*cursor) {
        const char *separator = strchr(cursor, ':');
        size_t length = separator ? (size_t)(separator - cursor) :
                                    strlen(cursor);
        size_t name_length = strlen(name);

        if (name_length + 2 <= sizeof(result) &&
            length <= sizeof(result) - name_length - 2) {
            memcpy(result, cursor, length);
            result[length] = '/';
            memcpy(result + length + 1, name, name_length + 1);
            if (access(result, X_OK) == 0)
                return result;
        }
        if (!separator)
            break;
        cursor = separator + 1;
    }
    return NULL;
}

struct host_command {
    const char *program;
    const char *first_argument;
    const char *second_argument;
    const char *third_argument;
};

static int choose_host_command(const char *override, int writing,
                               struct host_command *command)
{
    static const struct host_command readers[] = {
        {"wl-paste", "-n", NULL, NULL},
        {"xclip", "-selection", "clipboard", "-o"},
        {"xsel", "--clipboard", "--output", NULL},
    };
    static const struct host_command writers[] = {
        {"wl-copy", NULL, NULL, NULL},
        {"xclip", "-selection", "clipboard", "-in"},
        {"xsel", "--clipboard", "--input", NULL},
    };
    const struct host_command *choices = writing ? writers : readers;
    size_t count = writing ? sizeof(writers) / sizeof(writers[0]) :
                            sizeof(readers) / sizeof(readers[0]);

    if (override && *override) {
        command->program = override;
        command->first_argument = NULL;
        command->second_argument = NULL;
        command->third_argument = NULL;
        return access(override, X_OK) == 0 ? 0 : 1;
    }
    for (size_t index = 0; index < count; index++) {
        if (find_executable(choices[index].program)) {
            *command = choices[index];
            return 0;
        }
    }
    return 1;
}

static int host_file_read(unsigned char **data, size_t *size)
{
    const char *path = getenv("ACE_CLIPBOARD_HOST_FILE");
    struct stat information;
    int fd;
    unsigned char *buffer = NULL;

    if (!path || !*path)
        return 1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? 1 : -1;
    if (fstat(fd, &information) != 0 || information.st_size < 0 ||
        (uintmax_t)information.st_size > ACE_CLIPBOARD_MAX_SIZE) {
        int error = errno ? errno : EINVAL;
        close(fd);
        errno = error;
        return -1;
    }
    if (information.st_size) {
        buffer = malloc((size_t)information.st_size);
        if (!buffer || read_all_fd(fd, buffer, (size_t)information.st_size) != 0) {
            int error = errno ? errno : ENOMEM;
            free(buffer);
            close(fd);
            errno = error;
            return -1;
        }
    }
    close(fd);
    *data = buffer;
    *size = (size_t)information.st_size;
    return 0;
}

static int host_file_write(const unsigned char *data, size_t size)
{
    const char *path = getenv("ACE_CLIPBOARD_HOST_FILE");
    char temporary[PATH_MAX];
    int fd;
    int written;

    if (!path || !*path)
        return 1;
    written = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = mkstemp(temporary);
    if (fd < 0)
        return -1;
    if (fchmod(fd, 0600) != 0 ||
        (size && write_all_fd(fd, data, size) != 0) || fsync(fd) != 0 ||
        close(fd) != 0 || rename(temporary, path) != 0) {
        int error = errno;
        close(fd);
        unlink(temporary);
        errno = error;
        return -1;
    }
    return 0;
}

static int host_command_read(unsigned char **data, size_t *size)
{
    struct host_command command;
    unsigned char *result = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int pipe_fds[2];
    pid_t child;
    int status;

    if (choose_host_command(getenv("ACE_CLIPBOARD_GET"), 0, &command) != 0)
        return 1;
    if (pipe(pipe_fds) != 0)
        return -1;
    child = fork();
    if (child < 0) {
        int error = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        errno = error;
        return -1;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);

        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        if (command.first_argument && command.second_argument &&
            command.third_argument)
            execlp(command.program, command.program, command.first_argument,
                   command.second_argument, command.third_argument,
                   (char *)NULL);
        else if (command.first_argument && command.second_argument)
            execlp(command.program, command.program, command.first_argument,
                   command.second_argument, (char *)NULL);
        else if (command.first_argument)
            execlp(command.program, command.program, command.first_argument,
                   (char *)NULL);
        else
            execlp(command.program, command.program, (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    for (;;) {
        unsigned char buffer[4096];
        ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));

        if (count < 0) {
            if (errno == EINTR)
                continue;
            free(result);
            close(pipe_fds[0]);
            waitpid(child, NULL, 0);
            return -1;
        }
        if (count == 0)
            break;
        if ((size_t)count > ACE_CLIPBOARD_MAX_SIZE - length) {
            free(result);
            close(pipe_fds[0]);
            waitpid(child, NULL, 0);
            errno = EOVERFLOW;
            return -1;
        }
        if (length + (size_t)count > capacity) {
            size_t new_capacity = capacity ? capacity : 4096;
            unsigned char *grown;

            while (new_capacity < length + (size_t)count)
                new_capacity *= 2;
            grown = realloc(result, new_capacity);
            if (!grown) {
                free(result);
                close(pipe_fds[0]);
                waitpid(child, NULL, 0);
                errno = ENOMEM;
                return -1;
            }
            result = grown;
            capacity = new_capacity;
        }
        memcpy(result + length, buffer, (size_t)count);
        length += (size_t)count;
    }
    close(pipe_fds[0]);
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        free(result);
        errno = EIO;
        return -1;
    }
    *data = result;
    *size = length;
    return 0;
}

static int host_command_write(const unsigned char *data, size_t size)
{
    struct host_command command;
    int pipe_fds[2];
    pid_t child;
    int status;

    if (choose_host_command(getenv("ACE_CLIPBOARD_SET"), 1, &command) != 0)
        return 1;
    if (pipe(pipe_fds) != 0)
        return -1;
    child = fork();
    if (child < 0) {
        int error = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        errno = error;
        return -1;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);

        dup2(pipe_fds[0], STDIN_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        if (command.first_argument && command.second_argument &&
            command.third_argument)
            execlp(command.program, command.program, command.first_argument,
                   command.second_argument, command.third_argument,
                   (char *)NULL);
        else if (command.first_argument && command.second_argument)
            execlp(command.program, command.program, command.first_argument,
                   command.second_argument, (char *)NULL);
        else if (command.first_argument)
            execlp(command.program, command.program, command.first_argument,
                   (char *)NULL);
        else
            execlp(command.program, command.program, (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[0]);
    {
        struct sigaction ignore = {0};
        struct sigaction previous;
        int write_result;

        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        (void)sigaction(SIGPIPE, &ignore, &previous);
        write_result = size ? write_all_fd(pipe_fds[1], data, size) : 0;
        (void)sigaction(SIGPIPE, &previous, NULL);
        if (write_result != 0) {
            int error = errno;

            close(pipe_fds[1]);
            waitpid(child, NULL, 0);
            errno = error;
            return -1;
        }
    }
    close(pipe_fds[1]);
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

void ace_clipboard_store_deleted_path(const char *path)
{
    char clip0_path[PATH_MAX];
    char root[PATH_MAX];

    if (!path || getenv("ACE_CLIPBOARD_DISABLE_HOST") ||
        ace_clipboard_store_root(root, sizeof(root)) != 0 ||
        snprintf(clip0_path, sizeof(clip0_path), "%s/clip0", root) >=
            (int)sizeof(clip0_path) || strcmp(path, clip0_path) != 0)
        return;
    if (host_file_write(NULL, 0) == 1)
        (void)host_command_write(NULL, 0);
}

int ace_clipboard_store_commit(unsigned unit, const void *data, size_t size)
{
    unsigned char *text = NULL;
    size_t text_size = 0;

    if (store_commit_raw(unit, data, size) != 0)
        return -1;
    if (unit != 0 || getenv("ACE_CLIPBOARD_DISABLE_HOST"))
        return 0;
    if (extract_text(data, size, &text, &text_size) == 0) {
        int file_result = host_file_write(text, text_size);
        int command_result = file_result == 1 ? host_command_write(text,
                                                                    text_size) :
                                               1;

        free(text);
        /* No host clipboard is a valid headless configuration. A detected
         * host failure is deliberately not allowed to roll back the Amiga
         * clipboard transaction that has already been committed. */
        (void)command_result;
    }
    return 0;
}

int ace_clipboard_store_exists(unsigned unit)
{
    char path[PATH_MAX];
    struct stat information;

    if (unit_path(unit, NULL, path, sizeof(path)) != 0)
        return -1;
    return stat(path, &information) == 0 && S_ISREG(information.st_mode) ? 1 :
           (errno == ENOENT ? 0 : -1);
}

int ace_clipboard_store_count(void)
{
    int count = 0;

    if (ace_clipboard_store_prepare() != 0)
        return -1;
    for (unsigned unit = 0; unit < ACE_CLIPBOARD_UNIT_COUNT; unit++) {
        int exists = ace_clipboard_store_exists(unit);

        if (exists < 0)
            return -1;
        count += exists;
    }
    return count;
}

int ace_clipboard_host_refresh(void)
{
    unsigned char *host_text = NULL;
    unsigned char *old_clip = NULL;
    unsigned char *new_clip = NULL;
    size_t host_size = 0;
    size_t old_size = 0;
    size_t new_size = 0;
    size_t old_text_size = 0;
    unsigned char *old_text = NULL;
    int result;

    if (getenv("ACE_CLIPBOARD_DISABLE_HOST"))
        return 0;
    result = host_file_read(&host_text, &host_size);
    if (result == 1)
        result = host_command_read(&host_text, &host_size);
    if (result != 0)
        return result == 1 ? 0 : -1;
    result = ace_clipboard_store_load(0, &old_clip, &old_size);
    if (result == 0 && extract_text(old_clip, old_size, &old_text,
                                    &old_text_size) == 0 &&
        old_text_size == host_size &&
        (host_size == 0 || memcmp(old_text, host_text, host_size) == 0)) {
        free(host_text);
        free(old_clip);
        free(old_text);
        return 0;
    }
    if (make_text_clip(host_text, host_size, &new_clip, &new_size) != 0) {
        free(host_text);
        free(old_clip);
        free(old_text);
        return -1;
    }
    result = store_commit_raw(0, new_clip, new_size);
    free(host_text);
    free(old_clip);
    free(old_text);
    free(new_clip);
    return result == 0 ? 1 : -1;
}
