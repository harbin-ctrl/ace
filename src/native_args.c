#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aros/shcommands.h>

static int equal_name(const char *left, const char *right)
{
    while (*left && *right) {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right))
            return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static NativeArgSpec *find_spec(NativeArgSpec *specs, size_t count,
                                const char *name)
{
    for (size_t i = 0; i < count; i++)
        if (specs[i].name[0] && equal_name(specs[i].name, name))
            return &specs[i];
    return NULL;
}

static int is_keyword(NativeArgSpec *specs, size_t count, const char *token)
{
    char key[128];
    const char *separator = strchr(token, '=');
    size_t length = separator ? (size_t)(separator - token) : strlen(token);

    if (length == 0 || length >= sizeof(key))
        return 0;
    memcpy(key, token, length);
    key[length] = '\0';
    return find_spec(specs, count, key) != NULL;
}

static int has_modifier(const NativeArgSpec *spec, char modifier)
{
    for (const char *p = spec->modifier; *p; p++)
        if (*p == modifier)
            return 1;
    return 0;
}

static int is_numeric(const NativeArgSpec *spec)
{
    return strstr(spec->type_name, "ULONG") != NULL ||
           strstr(spec->type_name, "LONG") != NULL;
}

static int is_numeric_list(const NativeArgSpec *spec)
{
    return is_numeric(spec) && strstr(spec->type_name, "**") != NULL;
}

static int has_rest_argument(const NativeArgSpec *spec)
{
    return has_modifier(spec, 'F');
}

static void assign_value(NativeArgSpec *spec, const char *value)
{
    if (is_numeric(spec)) {
        ULONG *number = malloc(sizeof(*number));
        if (number)
            *number = (ULONG)strtoul(value, NULL, 0);
        *(ULONG **)spec->destination = number;
    } else {
        char *copy = strdup(value);
        *(char **)spec->destination = copy;
    }
}

static int split_help_line(char *line, char **arguments, size_t capacity)
{
    char *read = line;
    char *write = line;
    size_t count = 1;
    int quoted = 0;

    arguments[0] = (char *)"interactive";
    while (*read) {
        while (*read == ' ' || *read == '\t' || *read == '\n' || *read == '\r')
            read++;
        if (!*read)
            break;
        if (count >= capacity)
            return -1;
        arguments[count++] = write;
        while (*read) {
            if (*read == '"') {
                quoted = !quoted;
                read++;
            } else if (!quoted && (*read == ' ' || *read == '\t' ||
                                   *read == '\n' || *read == '\r')) {
                break;
            } else {
                *write++ = *read++;
            }
        }
        int had_separator = *read != '\0';
        *write++ = '\0';
        if (had_separator)
            read++;
    }
    return (int)count;
}

static int has_help_argument(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "?") == 0)
            return 1;
    return 0;
}

int native_parse_args(int argc, char **argv, NativeArgSpec *specs,
                      size_t spec_count, const char *template)
{
    NativeArgSpec *multi = NULL;
    NativeArgSpec *default_multi = NULL;
    char **multi_values = NULL;
    LONG **multi_numbers = NULL;
    size_t multi_count = 0;

    if (has_help_argument(argc, argv)) {
        char line[4096];
        char *interactive_arguments[256];
        int interactive_argc;

        do {
            fputs(template, stdout);
            fputs(": ", stdout);
            fflush(stdout);
            if (!fgets(line, sizeof(line), stdin))
                return -1;
            interactive_argc = split_help_line(line, interactive_arguments,
                                               sizeof(interactive_arguments) /
                                               sizeof(interactive_arguments[0]));
            if (interactive_argc < 0)
                return -1;
        } while (interactive_argc == 2 &&
                 strcmp(interactive_arguments[1], "?") == 0);

        return native_parse_args(interactive_argc, interactive_arguments,
                                 specs, spec_count, template);
    }

    unsigned char *positional_used = calloc(spec_count, 1);

    if (!positional_used)
        return -1;

    for (size_t i = 0; i < spec_count; i++) {
        if (has_modifier(&specs[i], 'M') &&
            (!specs[i].name[0] || !has_modifier(&specs[i], 'K'))) {
            default_multi = &specs[i];
            break;
        }
    }

    for (int i = 1; i < argc; i++) {
        char *token = argv[i];
        char *separator = strchr(token, '=');
        char key[128];
        const char *value = NULL;
        NativeArgSpec *spec;

        if (separator && (size_t)(separator - token) < sizeof(key)) {
            size_t length = (size_t)(separator - token);
            memcpy(key, token, length);
            key[length] = '\0';
            value = separator + 1;
            spec = find_spec(specs, spec_count, key);
        } else {
            spec = find_spec(specs, spec_count, token);
        }

        if (spec) {
            if (has_modifier(spec, 'S')) {
                *(BOOL *)spec->destination = TRUE;
                multi = NULL;
            } else if (has_modifier(spec, 'M')) {
                multi = spec;
                if (value) {
                    if (is_numeric_list(spec)) {
                        LONG **expanded = realloc(multi_numbers,
                                                  (multi_count + 1) * sizeof(*multi_numbers));
                        LONG *number;
                        if (!expanded) {
                            free(positional_used);
                            return -1;
                        }
                        number = malloc(sizeof(*number));
                        if (!number) {
                            free(expanded);
                            free(positional_used);
                            return -1;
                        }
                        *number = (LONG)strtol(value, NULL, 0);
                        multi_numbers = expanded;
                        multi_numbers[multi_count++] = number;
                    } else {
                        char **expanded = realloc(multi_values,
                                                  (multi_count + 1) * sizeof(*multi_values));
                        if (!expanded) {
                            free(positional_used);
                            return -1;
                        }
                        multi_values = expanded;
                        multi_values[multi_count++] = strdup(value);
                    }
                }
            } else {
                if (!value) {
                    if (++i >= argc) {
                        fprintf(stderr, "%s requires a value\n", token);
                        return -1;
                    }
                    value = argv[i];
                }
                assign_value(spec, value);
                multi = NULL;
            }
            continue;
        }

        if (multi && is_keyword(specs, spec_count, token)) {
            multi = NULL;
        }

        if (!multi)
            multi = default_multi;

        if (multi) {
            if (is_numeric_list(multi)) {
                LONG **expanded = realloc(multi_numbers,
                                          (multi_count + 1) * sizeof(*multi_numbers));
                LONG *number;
                if (!expanded) {
                    free(positional_used);
                    return -1;
                }
                number = malloc(sizeof(*number));
                if (!number) {
                    free(expanded);
                    free(positional_used);
                    return -1;
                }
                *number = (LONG)strtol(token, NULL, 0);
                multi_numbers = expanded;
                multi_numbers[multi_count++] = number;
            } else {
                char **expanded = realloc(multi_values,
                                          (multi_count + 1) * sizeof(*multi_values));
                if (!expanded) {
                    free(positional_used);
                    return -1;
                }
                multi_values = expanded;
                multi_values[multi_count++] = strdup(token);
            }
            continue;
        }

        bool assigned_positionally = false;
        for (size_t j = 0; j < spec_count; j++) {
            if (specs[j].name[0] && !has_modifier(&specs[j], 'K') &&
                !has_modifier(&specs[j], 'S') &&
                !has_modifier(&specs[j], 'M') &&
                !positional_used[j]) {
                if (has_rest_argument(&specs[j])) {
                    size_t rest_length = 0;
                    char *rest;

                    for (int k = i; k < argc; k++)
                        rest_length += strlen(argv[k]) + (k + 1 < argc ? 1 : 0);
                    rest = malloc(rest_length + 1);
                    if (!rest) {
                        free(positional_used);
                        return -1;
                    }
                    rest[0] = '\0';
                    for (int k = i; k < argc; k++) {
                        if (k != i)
                            strcat(rest, " ");
                        strcat(rest, argv[k]);
                    }
                    assign_value(&specs[j], rest);
                    free(rest);
                    i = argc;
                } else {
                    assign_value(&specs[j], token);
                }
                positional_used[j] = 1;
                assigned_positionally = true;
                break;
            }
        }
        if (assigned_positionally)
            continue;

        fprintf(stderr, "unknown argument: %s\n", token);
        return -1;
    }

    for (size_t i = 0; i < spec_count; i++) {
        if (has_modifier(&specs[i], 'A') &&
            *(char **)specs[i].destination == NULL) {
            fprintf(stderr, "%s requires a value\n", specs[i].name);
            free(positional_used);
            return -1;
        }
    }

    if (multi) {
        if (is_numeric_list(multi)) {
            LONG **values = realloc(multi_numbers,
                                    (multi_count + 1) * sizeof(*multi_numbers));
            if (!values) {
                free(positional_used);
                return -1;
            }
            values[multi_count] = NULL;
            *(LONG ***)multi->destination = values;
        } else {
            char **values = realloc(multi_values,
                                    (multi_count + 1) * sizeof(*multi_values));
            if (!values) {
                free(positional_used);
                return -1;
            }
            values[multi_count] = NULL;
            *(char ***)multi->destination = values;
        }
    } else {
        for (size_t i = 0; i < multi_count; i++)
            free(multi_values[i]);
        free(multi_values);
        free(multi_numbers);
    }
    free(positional_used);
    return 0;
}

void native_free_args(NativeArgSpec *specs, size_t spec_count)
{
    (void)specs;
    (void)spec_count;
}
