#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <string.h>

#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/dos.h>

#include "broker_client.h"
#include "broker_protocol.h"

/* AROS's Path.c owns the user-visible semantics. Its cli_CommandDir list is
 * process-local, though, while ACE commands are fork/exec children. This
 * entry point keeps the same argument and ordering contract and stores the
 * locks in the broker-backed session that the shell refreshes into its own
 * cli_CommandDir. */

static LONG path_io_error(int error)
{
    switch (error) {
    case ENOENT:
        return ERROR_OBJECT_NOT_FOUND;
    case ENOTDIR:
    case EINVAL:
        return ERROR_OBJECT_WRONG_TYPE;
    case EACCES:
    case EPERM:
        return ERROR_WRITE_PROTECTED;
    case ENOMEM:
        return ERROR_NO_FREE_STORE;
    case ENOSPC:
        return ERROR_DISK_FULL;
    case ENAMETOOLONG:
        return ERROR_LINE_TOO_LONG;
    default:
        return ERROR_ACTION_NOT_KNOWN;
    }
}

static void report_path_error(const char *path)
{
    SetIoErr(path_io_error(errno));
    PrintFault(IoErr(), path);
}

static int path_names(IPTR *arguments, STRPTR **names)
{
    *names = (STRPTR *)arguments[0];
    return *names != NULL && **names != NULL;
}

static int show_path(void)
{
    char paths[AMIGA_BROKER_MAX_PAYLOAD];
    char *save = NULL;
    char *path;

    if (native_broker_listpath(paths, sizeof(paths)) != 0) {
        report_path_error("Path");
        return -1;
    }
    PutStr("Current Directory\n");
    path = strtok_r(paths, "\n", &save);
    while (path) {
        char name[AMIGA_BROKER_MAX_PAYLOAD];

        if (native_broker_name_from_host(path, name, sizeof(name)) == 0) {
            PutStr(name);
            PutStr("\n");
        } else {
            /* A path may have been removed after Path accepted it. Keep the
             * list observable even if its DOS spelling is unavailable. */
            PutStr(path);
            PutStr("\n");
        }
        path = strtok_r(NULL, "\n", &save);
    }
    PutStr("C:\n");
    return 0;
}

static void update_paths(STRPTR *names, BOOL remove, BOOL quiet,
                         BOOL prepend)
{
    for (; names && *names; names++) {
        if (native_broker_path(*names,
                               remove ? AMIGA_BROKER_PATH_REMOVE :
                               (prepend ? AMIGA_BROKER_PATH_PREPEND :
                                          AMIGA_BROKER_PATH_ADD)) != 0 &&
            !quiet)
            report_path_error(*names);
    }
}

int main(void)
{
    IPTR arguments[7] = {0};
    struct RDArgs *rdargs;
    STRPTR *names;
    BOOL has_names;
    BOOL add;
    BOOL show;
    BOOL reset;
    BOOL remove;
    BOOL quiet;
    BOOL head;

    rdargs = ReadArgs("PATH/M,ADD/S,SHOW/S,RESET/S,REMOVE/S,QUIET/S,HEAD/S",
                      arguments, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Path");
        return RETURN_ERROR;
    }
    has_names = path_names(arguments, &names);
    add = (BOOL)arguments[1];
    show = (BOOL)arguments[2];
    reset = (BOOL)arguments[3];
    remove = (BOOL)arguments[4];
    quiet = (BOOL)arguments[5];
    head = (BOOL)arguments[6];
    (void)add; /* Names imply add in AROS; ADD documents that default. */

    if (reset && native_broker_path(NULL, AMIGA_BROKER_PATH_RESET) != 0) {
        report_path_error("Path");
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }
    if (has_names) {
        update_paths(names, remove && !reset, quiet, head);
    } else {
        show = !reset;
    }
    if (show && show_path() != 0) {
        FreeArgs(rdargs);
        return RETURN_ERROR;
    }
    FreeArgs(rdargs);
    return RETURN_OK;
}
