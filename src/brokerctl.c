#include "broker_client.h"
#include "broker_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(const char *program)
{
    fprintf(stderr, "usage: %s pwd | cd PATH | assign NAME PATH | resolve PATH | "
                    "getvar NAME | setvar NAME VALUE | setgvar NAME VALUE | "
                    "delvar NAME | result | cli | setresult RC RESULT2\n", program);
    return 2;
}

int main(int argc, char **argv)
{
    char result[4096];

    if (argc == 2 && strcmp(argv[1], "pwd") == 0) {
        if (native_broker_getcwd(result, sizeof(result)) != 0)
            return 1;
        puts(result);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "cd") == 0)
        return native_broker_setcwd(argv[2]) == 0 ? 0 : 1;
    if (argc == 4 && strcmp(argv[1], "assign") == 0)
        return native_broker_assign(argv[2], argv[3]) == 0 ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "resolve") == 0) {
        if (native_broker_resolve_path(argv[2], result, sizeof(result)) != 0)
            return 1;
        puts(result);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "getvar") == 0) {
        if (native_broker_getvar(argv[2], 0, result, sizeof(result)) != 0)
            return 1;
        puts(result);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "setvar") == 0)
        return native_broker_setvar(argv[2], argv[3],
                                    AMIGA_BROKER_VAR_LOCAL) == 0 ? 0 : 1;
    if (argc == 4 && strcmp(argv[1], "setgvar") == 0)
        return native_broker_setvar(argv[2], argv[3],
                                    AMIGA_BROKER_VAR_GLOBAL) == 0 ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "delvar") == 0)
        return native_broker_deletevar(argv[2], 0) == 0 ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "result") == 0) {
        if (native_broker_getresult(result, sizeof(result)) != 0)
            return 1;
        puts(result);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "cli") == 0) {
        if (native_broker_getcli(result, sizeof(result)) != 0)
            return 1;
        fputs(result, stdout);
        putchar('\n');
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "setresult") == 0)
        return native_broker_setresult((int32_t)strtol(argv[2], NULL, 10),
                                       (int32_t)strtol(argv[3], NULL, 10)) == 0 ? 0 : 1;
    return usage(argv[0]);
}
