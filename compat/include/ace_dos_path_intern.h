#ifndef DOS_INTERN_H
#define DOS_INTERN_H

#include <stdio.h>

/*
 * Compatibility boundary for the real AROS DOS path dispatcher.  The
 * imported getdeviceproc.c/freedeviceproc.c sources need the DOS-private
 * header only for the surrounding packet/filesystem machinery.  ACE keeps
 * that machinery at the host boundary, while retaining AROS's DosList and
 * multi-assign traversal algorithm above it.
 */
#include <aros/asmcall.h>
#include <aros/debug.h>
#include <aros/libcall.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

struct DAList {
    STRPTR *ArgBuf;
    UBYTE *StrBuf;
    STRPTR *MultVec;
    BOOL FreeRDA;
};

#define ASSERT_VALID_PROCESS(process) do { (void)(process); } while (0)

struct DeviceNode;
struct IORequest;

#define REPORT_INSERT 1
#define REPORT_VOLUME 2

STRPTR ResolveSoftlink(BPTR current, struct DevProc *device,
                       CONST_STRPTR name, struct DosLibrary *dos_base);
LONG RootDir(struct DevProc *device, struct DosLibrary *dos_base);

/* The host has no separate filesystem-handler process to start.  Device
 * discovery has already populated the DosList before the AROS dispatcher
 * sees it. */
#define RunHandler(device_node, path, dos_base) ((struct MsgPort *)NULL)

/* AROS's diagnostic-only branch is disabled in ACE builds. */
#define kprintf(...) ((void)0)

/* The compatibility DOS list has no blocking read lock, but it still needs
 * the same public entry points that real DOS code calls. */
struct DosList *LockDosList(ULONG flags);
struct DosList *FindDosEntry(struct DosList *list, CONST_STRPTR name,
                             ULONG flags);
void UnLockDosList(ULONG flags);

BOOL ErrorReport(LONG error, ULONG type, IPTR object, APTR requester);

#endif
