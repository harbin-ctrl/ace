#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "broker_client.h"

#define LHA_CORE_NAME "ace-lha-core"

static char *copy_string(const char *value)
{
    char *copy;

    copy = malloc(strlen(value) + 1);
    if (!copy)
        return NULL;
    strcpy(copy, value);
    return copy;
}

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

static int command_kind(const char *token)
{
    const char *command = token;

    if (!command || !*command)
        return 0;
    if (command[0] == '-')
        command++;
    if (strchr("axelvudmcpt", tolower((unsigned char)command[0])) == NULL)
        return 0;
    return tolower((unsigned char)command[0]);
}

static int option_takes_argument(const char *argument)
{
    return strcmp(argument, "-w") == 0 || strcmp(argument, "-x") == 0 ||
           strcmp(argument, "--system-kanji-code") == 0 ||
           strcmp(argument, "--archive-kanji-code") == 0 ||
           strcmp(argument, "--debug") == 0;
}

static int contains_pattern(const char *value)
{
    return strchr(value, '*') != NULL || strchr(value, '?') != NULL ||
           strchr(value, '#') != NULL;
}

static char *amiga_pattern_to_unix(const char *value)
{
    size_t length = strlen(value);
    size_t output_length = 0;
    char *output;

    for (size_t index = 0; index < length; index++) {
        if (value[index] == '#' && index + 1 < length &&
            value[index + 1] == '?') {
            output_length++;
            index++;
        } else {
            output_length++;
        }
    }
    output = malloc(output_length + 1);
    if (!output)
        return NULL;
    output_length = 0;
    for (size_t index = 0; index < length; index++) {
        if (value[index] == '#' && index + 1 < length &&
            value[index + 1] == '?') {
            output[output_length++] = '*';
            index++;
        } else {
            output[output_length++] = value[index];
        }
    }
    output[output_length] = '\0';
    return output;
}

static int relative_to(const char *path, const char *cwd, char *result,
                       size_t result_size)
{
    size_t cwd_length = strlen(cwd);
    const char *relative = NULL;

    if (strcmp(path, cwd) == 0)
        relative = ".";
    else if (strcmp(cwd, "/") == 0 && path[0] == '/')
        relative = path + 1;
    else if (strncmp(path, cwd, cwd_length) == 0 &&
             path[cwd_length] == '/')
        relative = path + cwd_length + 1;
    if (!relative)
        relative = path;
    if (strlen(relative) >= result_size)
        return -1;
    strcpy(result, relative);
    return 0;
}

static int parent_directory(const char *path, char *result, size_t result_size)
{
    const char *slash = strrchr(path, '/');
    size_t length;

    if (!slash)
        return -1;
    length = slash == path ? 1 : (size_t)(slash - path);
    if (length >= result_size)
        return -1;
    memcpy(result, path, length);
    result[length] = '\0';
    return 0;
}

static void common_directory(char *base, const char *candidate)
{
    size_t base_length = strlen(base);
    size_t candidate_length = strlen(candidate);
    size_t common = 0;

    if (strncmp(base, candidate, candidate_length) == 0 &&
        (base[candidate_length] == '\0' || base[candidate_length] == '/')) {
        strcpy(base, candidate);
        return;
    }
    if (strncmp(candidate, base, base_length) == 0 &&
        (candidate[base_length] == '\0' || candidate[base_length] == '/'))
        return;
    while (common < base_length && common < candidate_length &&
           base[common] == candidate[common])
        common++;
    while (common > 1 && base[common - 1] != '/')
        common--;
    if (common > 1 && base[common - 1] == '/')
        common--;
    if (common == 0)
        common = 1;
    base[common] = '\0';
}

static int resolve_argument(const char *argument, const char *cwd,
                             int make_relative, char *result,
                             size_t result_size)
{
    char resolved[PATH_MAX];

    if (strcmp(argument, "-") == 0) {
        if (strlen(argument) >= result_size)
            return -1;
        strcpy(result, argument);
        return 0;
    }
    if (native_broker_resolve_path(argument, resolved, sizeof(resolved)) != 0)
        return -1;
    if (make_relative)
        return relative_to(resolved, cwd, result, result_size);
    if (strlen(resolved) >= result_size)
        return -1;
    strcpy(result, resolved);
    return 0;
}

static int replace_path(char **argument, const char *cwd, int make_relative)
{
    char resolved[PATH_MAX];
    char *replacement;

    if (contains_pattern(*argument))
        return 0;
    if (resolve_argument(*argument, cwd, make_relative, resolved,
                         sizeof(resolved)) != 0)
        return -1;
    replacement = copy_string(resolved);
    if (!replacement)
        return -1;
    free(*argument);
    *argument = replacement;
    return 0;
}

static int replace_work_option(char **argument, const char *cwd)
{
    const char *value;
    char resolved[PATH_MAX];
    char *replacement;
    size_t prefix_length;

    if (strncmp(*argument, "-w=", 3) == 0) {
        prefix_length = 3;
        value = *argument + prefix_length;
    } else if (strncmp(*argument, "-w", 2) == 0 && (*argument)[2] != '\0') {
        prefix_length = 2;
        value = *argument + prefix_length;
    } else {
        return 0;
    }
    if (resolve_argument(value, cwd, 0, resolved, sizeof(resolved)) != 0)
        return -1;
    replacement = malloc(prefix_length + strlen(resolved) + 1);
    if (!replacement)
        return -1;
    memcpy(replacement, *argument, prefix_length);
    strcpy(replacement + prefix_length, resolved);
    free(*argument);
    *argument = replacement;
    return 0;
}

static int is_destination(const char *argument)
{
    size_t length = strlen(argument);

    return length != 0 && (argument[length - 1] == ':' ||
                           argument[length - 1] == '/');
}

static void free_arguments(char **arguments, int count)
{
    for (int index = 0; index < count; index++)
        free(arguments[index]);
    free(arguments);
}

int main(int argc, char **argv)
{
    char cwd[PATH_MAX];
    char directory[PATH_MAX];
    char core[PATH_MAX];
    char run_directory[PATH_MAX];
    char **source_paths;
    char **child;
    int kind;
    int archive_index;
    int destination_index = -1;
    int child_count;
    int option_value = 0;
    int direct_option = argc > 1 &&
                        (strcmp(argv[1], "--help") == 0 ||
                         strcmp(argv[1], "--version") == 0);

    if (native_broker_ensure() != 0 ||
        native_broker_getcwd(cwd, sizeof(cwd)) != 0 ||
        executable_directory(directory, sizeof(directory)) != 0 ||
        snprintf(core, sizeof(core), "%s/%s", directory, LHA_CORE_NAME) >=
            (int)sizeof(core)) {
        fprintf(stderr, "LhA: cannot establish the ACE pathname environment\n");
        return 2;
    }

    child = calloc((size_t)argc + 1, sizeof(*child));
    if (!child)
        return 2;
    for (int index = 0; index < argc; index++) {
        child[index] = copy_string(argv[index]);
        if (!child[index]) {
            free_arguments(child, argc);
            return 2;
        }
    }
    child_count = argc;
    strcpy(run_directory, cwd);
    source_paths = calloc((size_t)argc + 1, sizeof(*source_paths));
    if (!source_paths) {
        free_arguments(child, child_count);
        return 2;
    }
    free(child[0]);
    child[0] = copy_string(core);
    if (!child[0]) {
        free(source_paths);
        free_arguments(child, child_count);
        return 2;
    }

    kind = argc > 1 ? command_kind(argv[1]) : 0;
    archive_index = argc > 1 ? (kind ? 2 : 1) : argc;
    if (direct_option)
        archive_index = argc;
    if (kind) {
        for (int index = 2; index < argc; index++) {
            if (strcmp(argv[index], "--") == 0) {
                archive_index = index + 1;
                break;
            }
            if (option_takes_argument(argv[index])) {
                option_value = 1;
                continue;
            }
            if (option_value) {
                option_value = 0;
                continue;
            }
            if (argv[index][0] != '-') {
                archive_index = index;
                break;
            }
        }
    }

    if (archive_index < argc &&
        resolve_argument(child[archive_index], cwd,
                         0,
                         core, sizeof(core)) != 0) {
        fprintf(stderr, "LhA: cannot resolve archive path \"%s\": %s\n",
                argv[archive_index], strerror(errno));
        free(source_paths);
        free_arguments(child, child_count);
        return 2;
    }
    if (archive_index < argc) {
        free(child[archive_index]);
        child[archive_index] = copy_string(core);
        if (!child[archive_index]) {
            free(source_paths);
            free_arguments(child, child_count);
            return 2;
        }
    }

    if (kind == 'a' || kind == 'c' || kind == 'u' || kind == 'm') {
        int source_seen = 0;

        option_value = 0;
        for (int index = archive_index + 1; index < child_count; index++) {
            char resolved[PATH_MAX];
            char parent[PATH_MAX];

            if (option_value) {
                option_value = 0;
                continue;
            }
            if (option_takes_argument(child[index])) {
                option_value = 1;
                continue;
            }
            if (child[index][0] == '-' || contains_pattern(child[index]))
                continue;
            if (resolve_argument(child[index], cwd, 0, resolved,
                                 sizeof(resolved)) != 0 ||
                parent_directory(resolved, parent, sizeof(parent)) != 0) {
                fprintf(stderr, "LhA: cannot resolve file path \"%s\": %s\n",
                        argv[index], strerror(errno));
                free(source_paths);
                free_arguments(child, child_count);
                return 2;
            }
            source_paths[index] = copy_string(resolved);
            if (!source_paths[index]) {
                free(source_paths);
                free_arguments(child, child_count);
                return 2;
            }
            if (!source_seen) {
                strcpy(run_directory, parent);
                source_seen = 1;
            } else {
                common_directory(run_directory, parent);
            }
        }
    }

    option_value = 0;
    for (int index = kind ? 2 : 1; index < child_count; index++) {
        if (index == archive_index)
            continue;
        if (option_value) {
            if (replace_path(&child[index], cwd, 0) != 0) {
                fprintf(stderr, "LhA: cannot resolve working directory \"%s\": %s\n",
                        argv[index], strerror(errno));
                free_arguments(child, child_count);
                return 2;
            }
            option_value = 0;
            continue;
        }
        if (strcmp(argv[index], "-w") == 0) {
            option_value = 1;
            continue;
        }
        if (replace_work_option(&child[index], cwd) != 0) {
            fprintf(stderr, "LhA: cannot resolve working directory option \"%s\": %s\n",
                    argv[index], strerror(errno));
            free_arguments(child, child_count);
            return 2;
        }
        if ((kind == 'a' || kind == 'c' || kind == 'u' || kind == 'm') &&
            child[index][0] != '-') {
            char resolved[PATH_MAX];

            if (source_paths[index]) {
                if (relative_to(source_paths[index], run_directory, resolved,
                                sizeof(resolved)) != 0) {
                    fprintf(stderr, "LhA: file path is too long \"%s\"\n",
                            argv[index]);
                    free(source_paths);
                    free_arguments(child, child_count);
                    return 2;
                }
                free(child[index]);
                child[index] = copy_string(resolved);
                if (!child[index]) {
                    free(source_paths);
                    free_arguments(child, child_count);
                    return 2;
                }
            } else if (replace_path(&child[index], cwd, 1) != 0) {
            fprintf(stderr, "LhA: cannot resolve file path \"%s\": %s\n",
                    argv[index], strerror(errno));
            free(source_paths);
            free_arguments(child, child_count);
            return 2;
            }
        }
        if (kind != 'a' && kind != 'c' && kind != 'u' && kind != 'm' &&
            kind != 0 && child[index][0] != '-') {
            char *translated = amiga_pattern_to_unix(child[index]);
            if (!translated) {
                free(source_paths);
                free_arguments(child, child_count);
                return 2;
            }
            free(child[index]);
            child[index] = translated;
        }
    }

    if (kind == 'x' || kind == 'e') {
        for (int index = archive_index + 1; index < child_count; index++) {
            if (child[index][0] == '-') {
                if (strcmp(child[index], "-w") == 0)
                    index++;
                continue;
            }
            if (is_destination(child[index]))
                destination_index = index;
        }
        if (destination_index >= 0) {
            if (resolve_argument(child[destination_index], cwd, 0,
                                 run_directory, sizeof(run_directory)) != 0) {
                fprintf(stderr, "LhA: cannot enter extraction directory \"%s\": %s\n",
                        argv[destination_index], strerror(errno));
                free(source_paths);
                free_arguments(child, child_count);
                return 2;
            }
            free(child[destination_index]);
            for (int index = destination_index; index < child_count; index++)
                child[index] = child[index + 1];
            child_count--;
        }
    }

    if (strcmp(run_directory, cwd) != 0 && chdir(run_directory) != 0) {
        fprintf(stderr, "LhA: cannot enter working directory \"%s\": %s\n",
                run_directory, strerror(errno));
        free(source_paths);
        free_arguments(child, child_count);
        return 2;
    }
    child[child_count] = NULL;
    execv(child[0], child);
    fprintf(stderr, "LhA: cannot run %s: %s\n", child[0], strerror(errno));
    free(source_paths);
    free_arguments(child, child_count);
    return 2;
}
