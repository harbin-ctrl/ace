/*
    Copyright (C) 1995-2014, The AROS Development Team. All rights reserved.

    Desc: MakeLink CLI command

    ACE keeps this small command in-repository so its AmigaDOS behavior does
    not depend on a manually patched external AROS checkout. The source is
    forked from AROS workbench/c/MakeLink.c; ACE-specific changes are marked
    below.

    The bug this fork exists to fix: a hard link to a directory is legal in
    AmigaDOS and impossible on Linux, where the kernel refuses it at every
    privilege level because a directory with two parents is no longer a tree.
    Upstream has no way to say so. It either prints the FORCE hint -- advice
    that leads nowhere, since supplying FORCE fails identically -- or, once
    FORCE is given, fails through a path that prints nothing at all. Both
    leave the user to guess, and the second is silent about a command that
    did not do what it was asked. The whole difference is in reporting; the
    operation itself was always going to fail.
*/

/******************************************************************************

    NAME

        MakeLink

    SYNOPSIS

        FROM/A, TO/A, HARD/S, FORCE/S

    LOCATION

        C:

    FUNCTION

        Create a link to a file.

    INPUTS

        FROM   --  The name of the link
        TO     --  The name of the file or directory to link to
        HARD   --  If specified, the link will be a hard link; default is
                   to create a soft link
        FORCE  --  Allow a hard-link to point to a directory

    RESULT

        Standard DOS error codes.

    NOTES

        Not all file systems support links.

    EXAMPLE

        Makelink ls C:List
         Creates an 'ls' file with a soft link to the 'List' command in C:.

    BUGS

    SEE ALSO

    INTERNALS

******************************************************************************/

/* ACE-specific: report a refusal the host cannot carry out, and a soft-link
   failure upstream passes over in silence. */
#define ACE_REPORT_UNSUPPORTED_DIRECTORY_LINK 1
#define ACE_REPORT_SOFT_LINK_FAILURE          1

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <aros/debug.h>

const TEXT version[] = "$VER: MakeLink 41.2 (3.4.2014)\n";

enum { ARG_FROM = 0, ARG_TO, ARG_HARD, ARG_FORCE };

int __nocommandline;

int main(void)
{
    int  retval = RETURN_FAIL;
    LONG error = 0;
    IPTR args[] = { (IPTR)NULL, (IPTR)NULL, (IPTR)FALSE, (IPTR)FALSE };
    struct RDArgs *rda;

    rda = ReadArgs("FROM/A,TO/A,HARD/S,FORCE/S", args, NULL);

    if(rda != NULL)
    {
        if(args[ARG_HARD])
        {
            BPTR lock;

            lock = Lock((STRPTR)args[ARG_TO], SHARED_LOCK);

            if(lock != BNULL)
            {
                struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);

                if(fib != NULL)
                {
                    if(Examine(lock, fib))
                    {
                        /* Directories may only be hard-linked to if FORCE is
                           specified */
                        if(fib->fib_DirEntryType >= 0 && !(BOOL)args[ARG_FORCE])
                        {
                            PutStr("Hard links to directories require the"
                                " FORCE keyword\n");
                        }
                        else
                        {
                            /* Check loops? */
                            if(MakeLink((STRPTR)args[ARG_FROM], (SIPTR)lock, FALSE))
                                retval = RETURN_OK;
                            else
                            {
                                error = IoErr();
#if ACE_REPORT_UNSUPPORTED_DIRECTORY_LINK
                                /* ACE-specific: say which of the two it is.
                                   The filesystem was asked and refused; that
                                   is a different fact from "you may not", and
                                   only one of them tells the user to stop
                                   rather than to try harder. */
                                if(error == ERROR_NOT_IMPLEMENTED
                                   && fib->fib_DirEntryType >= 0)
                                {
                                    PutStr("This filesystem cannot hard-link"
                                        " directories\n");
                                }
                                else
#endif
                                PrintFault(error, "MakeLink");
                            }
                        }
                    }

                    FreeDosObject(DOS_FIB, fib);
                }

                UnLock(lock);
            }
            else
            {
                error = IoErr();
                PutStr((STRPTR)args[ARG_TO]);
                PrintFault(error, "");
            }
        }
        else
        {
            if(MakeLink((STRPTR)args[ARG_FROM], (SIPTR)args[ARG_TO], TRUE))
                retval = RETURN_OK;
#if ACE_REPORT_SOFT_LINK_FAILURE
            else
            {
                /* ACE-specific: upstream returns RETURN_FAIL here and prints
                   nothing, so a soft link that was refused looks exactly like
                   one that was made.  Every other failure in this command
                   says something; this one should too. */
                error = IoErr();
                PrintFault(error, "MakeLink");
            }
#endif
        }
    }
    else
    {
        error = IoErr();
        PrintFault(error, "MakeLink");
        retval = RETURN_FAIL;
    }

    FreeArgs(rda);

    SetIoErr(error);
    return retval;
}
