#ifndef ACE_ASSIGN_COMPAT_H
#define ACE_ASSIGN_COMPAT_H

/* Host-side ABI glue for compiling the unmodified AROS Assign.c. */
#include <stdarg.h>
#include <string.h>
#include <exec/types.h>
#include <aros/asmcall.h>

#ifndef AROS_ASMSYMNAME
#define AROS_ASMSYMNAME(name) name
#endif
#ifndef AROS_PROCH
#define AROS_PROCH(name, argptr, argsize, sysbase) \
    SIPTR name(STRPTR argptr, ULONG argsize, struct ExecBase *sysbase)
#endif
#ifndef AROS_PROCFUNC_INIT
#define AROS_PROCFUNC_INIT
#endif
#ifndef AROS_PROCFUNC_EXIT
#define AROS_PROCFUNC_EXIT
#endif

#ifndef __startup
#define __startup
#endif

#define AROS_SLOWSTACKFORMAT_PRE(format) \
    va_list ace_assign_format_arguments; \
    va_start(ace_assign_format_arguments, format)
#define AROS_SLOWSTACKFORMAT_ARG(format) ace_assign_format_arguments
#define AROS_SLOWSTACKFORMAT_POST(format) va_end(ace_assign_format_arguments)

#define SetMem(destination, value, length) \
    memset((destination), (value), (length))

struct DosList *LockDosList(ULONG flags);
struct DosList *AttemptLockDosList(ULONG flags);
struct DosList *NextDosEntry(struct DosList *entry, ULONG flags);
struct DosList *FindDosEntry(struct DosList *list, CONST_STRPTR name,
                             ULONG flags);
void UnLockDosList(ULONG flags);
BOOL AddDosEntry(struct DosList *entry);
BOOL RemDosEntry(struct DosList *entry);
struct DosList *MakeDosEntry(CONST_STRPTR name, LONG type);
void FreeDosEntry(struct DosList *entry);

BOOL AssignLock(CONST_STRPTR name, BPTR lock);
BOOL AssignAdd(CONST_STRPTR name, BPTR lock);
BOOL AssignAddToList(CONST_STRPTR name, BPTR lock, LONG flags);
BOOL AssignPath(CONST_STRPTR name, CONST_STRPTR path);
BOOL AssignLate(CONST_STRPTR name, CONST_STRPTR path);
LONG RemAssignList(CONST_STRPTR name, BPTR lock);

void RawDoFmt(CONST_STRPTR format, va_list arguments,
              void (*put_character)(void), APTR data);
LONG Write(BPTR file, CONST_APTR buffer, LONG length);

#endif
