#ifndef AMIGA_SHELL_DOS_EXALL_H
#define AMIGA_SHELL_DOS_EXALL_H

#include <exec/types.h>
#include <dos/dos.h>

/* Real, from dos/exall.h. AROS ships both a 32-bit and a 64-bit variant
   (ExAllData32/64, selected by __DOS64); ACE has one host, so there is one
   struct. */
struct ExAllData {
    struct ExAllData *ed_Next;

    UBYTE *ed_Name;
    LONG   ed_Type;
    LONG   ed_Size;
    ULONG  ed_Prot;

    ULONG ed_Days;
    ULONG ed_Mins;
    ULONG ed_Ticks;

    UBYTE *ed_Comment;

    UWORD ed_OwnerUID;
    UWORD ed_OwnerGID;
};

struct Hook;

#define ED_NAME       1
#define ED_TYPE       2
#define ED_SIZE       3
#define ED_PROTECTION 4
#define ED_DATE       5
#define ED_COMMENT    6
#define ED_OWNER      7

/* Real, from dos/exall.h. Allocated by AllocDosObject(DOS_EXALLCONTROL,...)
   only; every field must start zeroed, which AllocDosObject() does. */
struct ExAllControl {
    ULONG  eac_Entries;
    IPTR   eac_LastKey;
    UBYTE *eac_MatchString;
    struct Hook *eac_MatchFunc;
};

/* Real, from the private rom/dos/dos_intern.h: AllocDosObject(DOS_EXALLCONTROL)
   actually allocates this, not the bare public struct ExAllControl above --
   exall.c's own real ExAll() emulation path casts its `control` argument
   to this and uses the trailing `fib` field to remember the FileInfoBlock
   it allocated across repeated calls (LastKey == 0 means "first call").
   Kept here instead of in a shadowed dos_intern.h so it is visible to both
   exall.c and native_dos.c's AllocDosObject() without a second forced
   include on native_dos.c, which every native command links. */
struct InternalExAllControl {
    struct ExAllControl eac;
    struct FileInfoBlock *fib;
};

#endif
