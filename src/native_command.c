#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dos/dos.h>

#include "broker_client.h"
#include "broker_protocol.h"
#include "native_host.h"

struct ace_command_segment {
    char path[PATH_MAX];
};

static int executable_file(const char *path)
{
    return access(path, X_OK) == 0;
}

static int directory_command(const char *directory, const char *name,
                             char *result, size_t result_size)
{
    DIR *stream = opendir(directory);
    struct dirent *entry;

    if (!stream)
        return -1;
    while ((entry = readdir(stream)) != NULL) {
        int written;

        if (strcasecmp(entry->d_name, name) != 0)
            continue;
        written = snprintf(result, result_size, "%s/%s", directory,
                           entry->d_name);
        if (written >= 0 && (size_t)written < result_size &&
            executable_file(result)) {
            closedir(stream);
            return 0;
        }
    }
    closedir(stream);
    return -1;
}

static int companion_command(const char *path)
{
    char executable[PATH_MAX];
    char candidate[PATH_MAX];
    char *slash;
    ssize_t length;

    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1)
        return 0;
    executable[length] = '\0';
    slash = strrchr(executable, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    if (!realpath(path, candidate))
        return 0;
    {
        size_t directory_length = strlen(executable);

        return strlen(candidate) > directory_length &&
               strncmp(candidate, executable, directory_length) == 0 &&
               candidate[directory_length] == '/';
    }
}

int native_command_path(const char *name, char *result, size_t result_size)
{
    char resolved[PATH_MAX];
    char executable[PATH_MAX];
    char *slash;
    ssize_t length;

    if (!name || !*name || !result || result_size == 0)
        return -1;
    if (strchr(name, '/') || strchr(name, ':')) {
        if (native_broker_resolve_path(name, resolved, sizeof(resolved)) == 0 &&
            companion_command(resolved) && executable_file(resolved) &&
            strlen(resolved) < result_size) {
            strcpy(result, resolved);
            return 0;
        }
        return -1;
    }
    if (native_broker_resolve_path(name, resolved, sizeof(resolved)) == 0 &&
        companion_command(resolved) && executable_file(resolved) &&
        strlen(resolved) < result_size) {
        strcpy(result, resolved);
        return 0;
    }

    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length >= 0 && (size_t)length < sizeof(executable) - 1) {
        executable[length] = '\0';
        slash = strrchr(executable, '/');
        if (slash) {
            *slash = '\0';
            if (directory_command(executable, name, result, result_size) == 0)
                return 0;
        }
    }

    /* The host PATH is deliberately not an AmigaDOS command path. Linux
       programs must be invoked explicitly through LNX. */
    return -1;
}

BPTR LoadSeg(CONST_STRPTR name)
{
    struct ace_command_segment *segment;

    segment = calloc(1, sizeof(*segment));
    if (!segment) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return BNULL;
    }
    if (native_command_path(name, segment->path, sizeof(segment->path)) != 0) {
        free(segment);
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return BNULL;
    }
    return segment;
}

void UnLoadSeg(BPTR value)
{
    free(value);
}

struct Segment *FindSegment(CONST_STRPTR name, struct Segment *last,
                            BOOL system)
{
    (void)name;
    (void)last;
    (void)system;
    return NULL;
}

static int split_arguments(const char *input, size_t length, char *storage,
                           size_t storage_size, char **argv, size_t capacity)
{
    size_t input_index = 0;
    size_t storage_index = 0;
    size_t argument_count = 0;

    while (input_index < length) {
        int quoted = 0;

        while (input_index < length &&
               (input[input_index] == ' ' || input[input_index] == '\t' ||
                input[input_index] == '\r' || input[input_index] == '\n'))
            input_index++;
        if (input_index == length)
            break;
        if (argument_count + 1 >= capacity)
            return -1;
        argv[++argument_count] = storage + storage_index;
        while (input_index < length) {
            char character = input[input_index++];

            if (character == '"') {
                quoted = !quoted;
                continue;
            }
            if (!quoted && (character == ' ' || character == '\t' ||
                            character == '\r' || character == '\n'))
                break;
            if ((character == '*' || character == '\\') &&
                input_index < length)
                character = input[input_index++];
            if (storage_index + 1 >= storage_size)
                return -1;
            storage[storage_index++] = character;
        }
        if (storage_index + 1 >= storage_size)
            return -1;
        storage[storage_index++] = '\0';
    }
    argv[argument_count + 1] = NULL;
    return (int)argument_count;
}

LONG RunCommand(BPTR value, ULONG stack, STRPTR arguments, LONG length)
{
    struct ace_command_segment *segment = value;
    char storage[4096];
    char *argv[128] = {0};
    char cwd[PATH_MAX];
    pid_t child;
    int status;

    (void)stack;
    if (!segment || !arguments || length < 0 ||
        (size_t)length >= sizeof(storage) ||
        split_arguments(arguments, (size_t)length, storage, sizeof(storage),
                         argv, sizeof(argv) / sizeof(argv[0])) < 0) {
        SetIoErr(ERROR_BAD_TEMPLATE);
        return RETURN_FAIL;
    }
    argv[0] = segment->path;
    child = fork();
    if (child < 0) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return RETURN_FAIL;
    }
    if (child == 0) {
        if (native_broker_getcwd(cwd, sizeof(cwd)) == 0)
            (void)chdir(cwd);
        execv(segment->path, argv);
        _exit(RETURN_FAIL);
    }
    if (waitpid(child, &status, 0) < 0) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return RETURN_FAIL;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == NATIVE_ENDCLI_STATUS) {
        native_request_endcli();
        return RETURN_OK;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == RETURN_OK) {
        char requested[8];

        if (native_broker_getvar("__ACE_ENDCLI", AMIGA_BROKER_VAR_LOCAL,
                                 requested, sizeof(requested)) == 0) {
            (void)native_broker_deletevar("__ACE_ENDCLI",
                                          AMIGA_BROKER_VAR_LOCAL);
            native_request_endcli();
        }
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return RETURN_FAIL;
}
