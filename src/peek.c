#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>

#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include "broker_client.h"
#include "broker_protocol.h"

static LONG peek_io_error(int error)
{
    switch (error) {
    case ENOENT:
        return ERROR_OBJECT_NOT_FOUND;
    case ENODEV:
    case EEXIST:
        return ERROR_DEVICE_NOT_MOUNTED;
    case ENOTDIR:
    case EINVAL:
        return ERROR_OBJECT_WRONG_TYPE;
    case EACCES:
    case EPERM:
        return ERROR_WRITE_PROTECTED;
    case ENAMETOOLONG:
        return ERROR_LINE_TOO_LONG;
    case ENOMEM:
        return ERROR_NO_FREE_STORE;
    default:
        return ERROR_ACTION_NOT_KNOWN;
    }
}

static int peek_error(const char *name)
{
    SetIoErr(peek_io_error(errno));
    PrintFault(IoErr(), name);
    return RETURN_FAIL;
}

#define PEEK_MATCH_PATH_LENGTH 4096

static int peek_matches(const char *pattern)
{
    struct AnchorPath *anchor;
    LONG match;
    int found = 0;
    int status = RETURN_OK;

    anchor = AllocVec(sizeof(*anchor) + PEEK_MATCH_PATH_LENGTH,
                      MEMF_CLEAR);
    if (!anchor) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return RETURN_FAIL;
    }
    anchor->ap_Strlen = PEEK_MATCH_PATH_LENGTH;
    anchor->ap_BreakBits = SIGBREAKF_CTRL_C;
    match = MatchFirst(pattern, anchor);
    while (match == 0) {
        char linux_path[AMIGA_BROKER_MAX_PAYLOAD];

        if (native_broker_resolve_path((const char *)anchor->ap_Buf,
                                        linux_path, sizeof(linux_path)) != 0) {
            status = peek_error((const char *)anchor->ap_Buf);
            break;
        }
        puts(linux_path);
        found = 1;
        anchor->ap_Strlen = PEEK_MATCH_PATH_LENGTH;
        match = MatchNext(anchor);
    }
    if (anchor->ap_Base)
        MatchEnd(anchor);
    FreeVec(anchor);

    if (status == RETURN_OK && !found) {
        if (match == ERROR_NO_MORE_ENTRIES)
            status = RETURN_FAIL;
        else {
            SetIoErr(match);
            status = RETURN_FAIL;
        }
    } else if (status == RETURN_OK && match != ERROR_NO_MORE_ENTRIES) {
        SetIoErr(match);
        status = RETURN_FAIL;
    }
    return status;
}

int main(void)
{
    IPTR arguments[2] = {0};
    struct RDArgs *rdargs;
    const char *name;
    char result[AMIGA_BROKER_MAX_PAYLOAD];
    int host;
    int status;

    rdargs = ReadArgs("NAME/A,HOST=LINUX/S", arguments, NULL);
    if (!rdargs) {
        PrintFault(IoErr(), "Peek");
        return RETURN_ERROR;
    }
    name = (const char *)arguments[0];
    host = arguments[1] != 0;
    if (!host) {
        status = peek_matches(name);
        if (status != RETURN_OK) {
            PrintFault(IoErr(), "Peek");
            FreeArgs(rdargs);
            return RETURN_FAIL;
        }
    } else {
        status = native_broker_name_from_host(name, result, sizeof(result));
        if (status != 0) {
            status = peek_error(name);
            FreeArgs(rdargs);
            return status;
        }
        puts(result);
    }
    FreeArgs(rdargs);
    return RETURN_OK;
}
