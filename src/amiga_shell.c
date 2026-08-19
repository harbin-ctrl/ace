#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "broker_client.h"
#include "broker_protocol.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_WORDS 128
#define MAX_LINE 4096
#define MAX_ALIAS_DEPTH 8

static char shell_directory[PATH_MAX];

static int split_words(const char *line, char **words, size_t capacity)
{
    char *copy = strdup(line);
    char *read = copy;
    size_t count = 0;

    if (!copy)
        return -1;
    while (*read) {
        char *write;
        int quoted = 0;

        while (isspace((unsigned char)*read))
            read++;
        if (!*read)
            break;
        if (count + 1 >= capacity) {
            free(copy);
            return -1;
        }
        words[count++] = read;
        write = read;
        while (*read) {
            if (*read == '\\' && read[1]) {
                *write++ = *++read;
                read++;
            } else if (*read == '"') {
                quoted = !quoted;
                read++;
            } else if (!quoted && isspace((unsigned char)*read)) {
                read++;
                break;
            } else {
                *write++ = *read++;
            }
        }
        if (quoted) {
            free(copy);
            return -1;
        }
        *write = '\0';
    }
    words[count] = NULL;
    return (int)count;
}

static void free_words(char **words)
{
    if (words[0])
        free(words[0]);
}

static char *join_words(char **words, int first, int count)
{
    size_t size = 1;
    char *result;

    for (int i = first; i < count; i++)
        size += strlen(words[i]) + 1;
    result = calloc(1, size);
    if (!result)
        return NULL;
    for (int i = first; i < count; i++) {
        if (i != first)
            strcat(result, " ");
        strcat(result, words[i]);
    }
    return result;
}

static char *expand_alias(const char *line, int depth)
{
    char *words[MAX_WORDS] = {0};
    char alias[AMIGA_BROKER_MAX_PAYLOAD];
    char *arguments = NULL;
    char *result = NULL;
    const char *placeholder;
    int count;

    if (depth >= MAX_ALIAS_DEPTH)
        return NULL;
    count = split_words(line, words, MAX_WORDS);
    if (count <= 0)
        return NULL;
    if (native_broker_getvar(words[0], AMIGA_BROKER_VAR_ALIAS,
                             alias, sizeof(alias)) != 0)
        goto done;

    arguments = join_words(words, 1, count);
    if (!arguments)
        goto done;
    placeholder = strstr(alias, "[]");
    if (placeholder) {
        size_t prefix = (size_t)(placeholder - alias);
        size_t length = prefix + strlen(arguments) + strlen(placeholder + 2) + 1;
        result = calloc(1, length);
        if (result) {
            memcpy(result, alias, prefix);
            strcat(result, arguments);
            strcat(result, placeholder + 2);
        }
    } else {
        size_t length = strlen(alias) + (arguments[0] ? strlen(arguments) + 1 : 0) + 1;
        result = calloc(1, length);
        if (result) {
            strcpy(result, alias);
            if (arguments[0]) {
                strcat(result, " ");
                strcat(result, arguments);
            }
        }
    }

done:
    free(arguments);
    free_words(words);
    return result;
}

static int find_in_directory(const char *directory, const char *command,
                             char *result, size_t result_size)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;

    if (!dir)
        return -1;
    while ((entry = readdir(dir)) != NULL) {
        int written;
        if (strcasecmp(entry->d_name, command) != 0)
            continue;
        written = snprintf(result, result_size, "%s/%s", directory,
                           entry->d_name);
        if (written >= 0 && (size_t)written < result_size &&
            access(result, X_OK) == 0) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static int find_command(const char *command, char *result, size_t result_size)
{
    if (strchr(command, '/')) {
        if (strlen(command) >= result_size || access(command, X_OK) != 0)
            return -1;
        strcpy(result, command);
        return 0;
    }
    if (shell_directory[0]) {
        if (find_in_directory(shell_directory, command, result, result_size) == 0)
            return 0;
    }
    /* Do not inherit Linux command discovery. Use LNX for host programs. */
    return -1;
}

static void print_prompt(void)
{
    char state[AMIGA_BROKER_MAX_PAYLOAD];
    char cwd[PATH_MAX];
    char prompt[PATH_MAX];
    char *save = NULL;
    char *return_code;
    char *result2;
    char *fail_level;
    char *format;
    size_t used = 0;

    if (native_broker_getcli(state, sizeof(state)) != 0 ||
        native_broker_getcwd(cwd, sizeof(cwd)) != 0) {
        fputs("AMIGA> ", stdout);
        fflush(stdout);
        return;
    }
    return_code = strtok_r(state, "\n", &save);
    result2 = strtok_r(NULL, "\n", &save);
    fail_level = strtok_r(NULL, "\n", &save);
    format = strtok_r(NULL, "\n", &save);
    (void)return_code;
    (void)result2;
    (void)fail_level;
    if (!format)
        format = "AMIGA> ";

    for (const char *p = format; *p && used + 1 < sizeof(prompt); p++) {
        if (*p != '%') {
            prompt[used++] = *p;
            continue;
        }
        p++;
        if (!*p)
            break;
        if (*p == 'S') {
            size_t length = strlen(cwd);
            if (used + length >= sizeof(prompt))
                length = sizeof(prompt) - used - 1;
            memcpy(prompt + used, cwd, length);
            used += length;
        } else if (*p == 'R') {
            long rc = strtol(return_code ? return_code : "0", NULL, 10);
            used += (size_t)snprintf(prompt + used, sizeof(prompt) - used,
                                     "%ld", rc);
        } else if (*p == 'N') {
            prompt[used++] = '1';
        } else if (*p == '%') {
            prompt[used++] = '%';
        } else {
            prompt[used++] = '%';
            if (used + 1 < sizeof(prompt))
                prompt[used++] = *p;
        }
    }
    prompt[used] = '\0';
    fputs(prompt, stdout);
    fflush(stdout);
}

static int run_command(char **argv, const char *line)
{
    char command_path[PATH_MAX];
    pid_t child;
    int status;
    int native = find_command(argv[0], command_path, sizeof(command_path)) == 0;

    child = fork();
    if (child < 0)
        return 20;
    if (child == 0) {
        char cwd[PATH_MAX];
        native_broker_reset_after_fork();
        if (native) {
            char *native_argv[MAX_WORDS];
            size_t index = 0;
            while (argv[index] && index + 1 < MAX_WORDS) {
                native_argv[index] = argv[index];
                index++;
            }
            native_argv[index] = NULL;
            native_argv[0] = command_path;
            execv(command_path, native_argv);
            _exit(20);
        }
        if (native_broker_getcwd(cwd, sizeof(cwd)) == 0)
            (void)chdir(cwd);
        _exit(20);
    }
    if (waitpid(child, &status, 0) < 0)
        return 20;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 20;
}

int main(int argc, char **argv)
{
    char line[MAX_LINE];

    if (argc > 1 && argv[1][0] == '-') {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 20;
    }
    {
        char executable[PATH_MAX];
        if (realpath(argv[0], executable)) {
            char *slash = strrchr(executable, '/');
            if (slash) {
                *slash = '\0';
                snprintf(shell_directory, sizeof(shell_directory), "%s", executable);
            }
        }
    }

    for (;;) {
        char *words[MAX_WORDS] = {0};
        char *expanded;
        int count;

        print_prompt();
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        if (strcasecmp(line, "ENDCLI") == 0 ||
            strcasecmp(line, "ENDSHELL") == 0)
            break;

        expanded = expand_alias(line, 0);
        if (expanded) {
            strncpy(line, expanded, sizeof(line) - 1);
            line[sizeof(line) - 1] = '\0';
            free(expanded);
        }
        count = split_words(line, words, MAX_WORDS);
        if (count < 0) {
            fputs("Shell: bad command line\n", stderr);
            native_broker_setresult(10, 114);
            continue;
        }
        if (count == 0)
            continue;
        run_command(words, line);
        free_words(words);
    }
    return 0;
}
