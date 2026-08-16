/*
 * ACE's CD command.
 *
 * This follows the AROS command, with the AmigaDOS 3.1 behavior that CD
 * accepts a pattern when it is written explicitly.  A pattern may identify
 * one directory, but changing directory is deliberately refused when it
 * identifies more than one directory.
 */

#include <exec/memory.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include <string.h>

#include <aros/shcommands.h>

#define CD_MATCH_PATH_LENGTH 2048

static STRPTR GetLockName(BPTR lock, struct ExecBase *SysBase,
    struct DosLibrary *DOSBase);

static BOOL cd_has_pattern(CONST_STRPTR pattern)
{
    BOOL quoted = FALSE;

    while (*pattern)
    {
        if (quoted)
            quoted = FALSE;
        else if (*pattern == '\'')
            quoted = TRUE;
        else if (strchr("#~[]?*()|", *pattern) != NULL)
            return TRUE;
        pattern++;
    }
    return FALSE;
}

static LONG cd_find_directory(CONST_STRPTR pattern, BPTR *result,
                              BOOL *multiple)
{
    struct AnchorPath *anchor;
    LONG match;
    LONG error = ERROR_OBJECT_NOT_FOUND;
    STRPTR selected = NULL;

    *result = BNULL;
    *multiple = FALSE;

    /* Keep ordinary paths on Lock(), which knows how to advance through an
       ACE multi-assign. MatchFirst() is needed only for actual patterns. */
    if (!cd_has_pattern(pattern))
    {
        *result = Lock(pattern, SHARED_LOCK);
        return *result ? 0 : IoErr();
    }

    anchor = AllocVec(sizeof(struct AnchorPath) + CD_MATCH_PATH_LENGTH,
                      MEMF_CLEAR);
    if (!anchor)
        return ERROR_NO_FREE_STORE;

    anchor->ap_Strlen = CD_MATCH_PATH_LENGTH;
    anchor->ap_BreakBits = SIGBREAKF_CTRL_C;
    match = MatchFirst(pattern, anchor);
    error = match;
    while (match == 0)
    {
        if (anchor->ap_Info.fib_DirEntryType > 0)
        {
            if (selected)
            {
                *multiple = TRUE;
                error = 0;
                break;
            }
            selected = AllocVec(CD_MATCH_PATH_LENGTH, MEMF_CLEAR);
            if (!selected)
            {
                error = ERROR_NO_FREE_STORE;
                break;
            }
            if (strlen((char *)anchor->ap_Buf) >= CD_MATCH_PATH_LENGTH)
            {
                error = ERROR_BUFFER_OVERFLOW;
                break;
            }
            strcpy(selected, (char *)anchor->ap_Buf);
        }
        match = MatchNext(anchor);
        error = match;
    }

    if (!*multiple && error == ERROR_NO_MORE_ENTRIES && selected)
    {
        *result = Lock(selected, SHARED_LOCK);
        error = *result ? 0 : IoErr();
    }

    if (anchor->ap_Base)
        MatchEnd(anchor);
    FreeVec(selected);
    FreeVec(anchor);
    return error;
}

AROS_SH1(CD, 41.2,
    AROS_SHA(STRPTR, , DIR, , NULL))
{
    AROS_SHCOMMAND_INIT

    BPTR dir, newdir;
    STRPTR buf;
    struct FileInfoBlock *fib;
    LONG return_code = 0, error = 0;
    BOOL multiple = FALSE;

    if (SHArg(DIR))
    {
        error = cd_find_directory(SHArg(DIR), &dir, &multiple);
        if (multiple)
        {
            FPuts(Output(), "More than one directory matches\n");
            return_code = RETURN_ERROR;
        }
        else if (dir)
        {
            fib = AllocDosObject(DOS_FIB, NULL);

            if (fib != NULL)
            {
                if (Examine(dir, fib))
                {
                    if (fib->fib_DirEntryType > 0)
                    {
                        newdir = dir;
                        dir = CurrentDir(newdir);

                        buf = GetLockName(newdir, SysBase, DOSBase);
                        if (buf != NULL)
                        {
                            SetCurrentDirName(buf);
                            FreeVec(buf);
                        }
                    }
                    else
                        error = ERROR_OBJECT_WRONG_TYPE;
                }
                else
                    error = IoErr();

                FreeDosObject(DOS_FIB, fib);
            }
            else
                error = IoErr();

            UnLock(dir);
        }
    }
    else
    {
        dir = CurrentDir(BNULL);

        buf = GetLockName(dir, SysBase, DOSBase);
        if (buf != NULL)
        {
            if (FPuts(Output(), buf) < 0 || FPuts(Output(), "\n") < 0)
                error = IoErr();
        }
        else
            error = IoErr();
        FreeVec(buf);
        CurrentDir(dir);
    }

    if (error != 0 && !multiple)
    {
        PrintFault(error, "CD");
        return_code = RETURN_ERROR;
    }

    return return_code;

    AROS_SHCOMMAND_EXIT
}


static STRPTR GetLockName(BPTR lock, struct ExecBase *SysBase, struct DosLibrary *DOSBase)
{
    LONG error = 0;
    STRPTR buf = NULL;
    ULONG i;

    for(i = 256; buf == NULL && error == 0; i += 256)
    {
        buf = AllocVec(i, MEMF_PUBLIC);
        if (buf != NULL)
        {
            if (!NameFromLock(lock, buf, i))
            {
                error = IoErr();
                if (error == ERROR_LINE_TOO_LONG)
                {
                    error = 0;
                    FreeVec(buf);
                    buf = NULL;
                }
            }
        }
        else
            error = IoErr();
    }

    if (error != 0)
        SetIoErr(error);
    return buf;
}
