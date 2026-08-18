#ifndef REXX_STORAGE_H
#define REXX_STORAGE_H

/*
 * AROS ARexx data structures.  This is the public storage.h supplied by
 * AROS, kept here because ACE builds AROS sources with its host ABI.
 */
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/lists.h>
#include <dos/dosextens.h>

struct RexxMsg {
    struct Message rm_Node;
    IPTR rm_Private1;
    IPTR rm_Private2;
    LONG rm_Action;
    LONG rm_Result1;
    IPTR rm_Result2;
    IPTR rm_Args[16];
    struct MsgPort *rm_PassPort;
    STRPTR rm_CommAddr;
    STRPTR rm_FileExt;
    BPTR rm_Stdin;
    BPTR rm_Stdout;
    LONG rm_Unused1;
};

#define ARG0(msg) ((UBYTE *)(msg)->rm_Args[0])
#define ARG1(msg) ((UBYTE *)(msg)->rm_Args[1])
#define ARG2(msg) ((UBYTE *)(msg)->rm_Args[2])
#define RXARG(msg,n) ((UBYTE *)(msg)->rm_Args[n])
#define MAXRMARG 15

#define RXCOMM   0x01000000
#define RXFUNC   0x02000000
#define RXCLOSE  0x03000000
#define RXQUERY  0x04000000
#define RXADDFH  0x07000000
#define RXADDLIB 0x08000000
#define RXREMLIB 0x09000000
#define RXADDCON 0x0A000000
#define RXREMCON 0x0B000000
#define RXTCOPN  0x0C000000
#define RXTCCLS  0x0D000000
#define RXADDRSRC  0xF0000000
#define RXREMRSRC  0xF1000000
#define RXCHECKMSG 0xF2000000
#define RXSETVAR   0xF3000000
#define RXGETVAR   0xF4000000
#define RXCODEMASK 0xFF000000
#define RXARGMASK  0x0000000F

#define RXFB_NOIO 16
#define RXFB_RESULT 17
#define RXFB_STRING 18
#define RXFB_TOKEN 19
#define RXFB_NONRET 20
#define RXFB_FUNCLIST 5
#define RXFF_NOIO (1 << RXFB_NOIO)
#define RXFF_RESULT (1 << RXFB_RESULT)
#define RXFF_STRING (1 << RXFB_STRING)
#define RXFF_TOKEN (1 << RXFB_TOKEN)
#define RXFF_NONRET (1 << RXFB_NONRET)

struct RexxArg {
    LONG ra_Size;
    UWORD ra_Length;
    UBYTE ra_Deprecated1;
    UBYTE ra_Deprecated2;
    BYTE ra_Buff[8];
};

struct RexxRsrc {
    struct Node rr_Node;
    WORD rr_Func;
    APTR rr_Base;
    LONG rr_Size;
    SIPTR rr_Arg1;
    SIPTR rr_Arg2;
};

#define RRT_ANY 0
#define RRT_LIB 1
#define RRT_HOST 4
#define RRT_CLIP 5

#endif
