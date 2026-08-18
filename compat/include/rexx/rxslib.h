#ifndef REXX_RXSLIB_H
#define REXX_RXSLIB_H

/* Public AROS compatibility definition of rexxsyslib.library's base. */
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <rexx/storage.h>

#define RXSNAME  "rexxsyslib.library"
#define RXSDIR   "REXX"
#define RXSTNAME "ARexx"

struct RxsLib {
    struct Library rl_Node;
    UBYTE rl_Flags;
    UBYTE rl_Shadow;
    struct ExecBase *rl_SysBase;
    struct DOSBase *rl_DOSBase;
    struct Library *rl_Unused1;
    BPTR rl_SegList;
    struct FileHandle *rl_Unused2;
    LONG rl_Unused3;
    LONG rl_Unused4;
    APTR rl_Unused5;
    APTR rl_Unused6;
    APTR rl_Unused7;
    APTR rl_Unused8;
    APTR rl_Unused9;
    APTR rl_Unused10;
    APTR rl_Unused11;
    APTR rl_Unused12;
    STRPTR rl_Version;
    STRPTR rl_Unused13;
    LONG rl_Unused14;
    LONG rl_Unused15;
    LONG rl_Unused16;
    STRPTR rl_Unused17;
    STRPTR rl_Unused18;
    STRPTR rl_Notice;
    struct MsgPort rl_Unused19;
    UWORD rl_Unused20;
    LONG rl_Unused21;
    struct List rl_Unused22;
    WORD rl_Unused23;
    struct List rl_LibList;
    WORD rl_NumLib;
    struct List rl_ClipList;
    WORD rl_NumClip;
    struct List rl_Unused24;
    WORD rl_Unused25;
    struct List rl_Unused26;
    WORD rl_Unused27;
    UWORD rl_Unused28;
    WORD rl_Unused29;
};

#endif
