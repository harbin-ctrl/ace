#ifndef DOS_INTERN_H
#define DOS_INTERN_H

/*
 * ACE compiles real AROS dos.library sources (rom/dos/{exall,matchfirst,
 * matchnext,matchend,match_misc,patternmatching,matchpattern,parsepattern,
 * matchpatternnocase,parsepatternnocase}.c) against the ACE DOS seam in
 * src/native_dos.c. Every one of them includes "dos_intern.h" -- a local,
 * same-directory include that gcc would otherwise find at
 * rom/dos/dos_intern.h itself, a 335-line private header pulling in
 * dos/filehandler.h and "fs_driver.h": the real filesystem-handler-process
 * packet protocol. ACE's Lock()/Examine() have no handler process at all
 * (see native_dos.c) and deliberately do not implement that protocol, so
 * this claims dos_intern.h's own include guard via -include on this compile
 * group (forced in ahead of the source, the same way
 * compat/aros-real/include/ace_boopsi_intern.h and ace_graphics_intern.h
 * shadow their own private headers) and supplies only what these ten files
 * actually reference from it: patternmatching.c's own internal marker
 * stack and macros, match_misc.c's four entry points the other three real
 * files call directly (ordinary C linkage, not a packet or a library
 * vector), and one packet-protocol escape hatch in exall.c (below). None
 * of the marker/Match_* content is packet/handler-process machinery -- it
 * is the pattern-matching engine's own private bookkeeping, restated here
 * verbatim for the same reason con_libdefs.h and the DrawInfo pen values
 * were in earlier phases: real, portable content this checkout's build
 * does not place on any -I path this compile group reaches.
 */

#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/debug.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <utility/hooks.h>

/* Real AROS build config constant (pointer alignment), used by exall.c to
   round each packed ExAllData entry up to a valid alignment boundary. */
#ifndef AROS_PTRALIGN
#define AROS_PTRALIGN sizeof(IPTR)
#endif

/* Real, from dos/dosextens.h: the DOS packet action-type number exall.c
   passes to the dopacket5() fast path above. A stable numeric identifier,
   not part of the packet machinery itself. */
#define ACTION_EXAMINE_ALL 1033

struct marker {
    UBYTE type;
    CONST_STRPTR pat;
    CONST_STRPTR str;
};

struct markerarray {
    struct markerarray *next;
    struct markerarray *prev;
    struct marker marker[128];
};

#define PUSH(t,p,s)                                                     \
{                                                                       \
    if(macnt==128)                                                     \
    {                                                                  \
        if(macur->next==NULL)                                          \
        {                                                              \
            macur->next=AllocMem(sizeof(struct markerarray),MEMF_ANY); \
            if(macur->next==NULL)                                      \
                ERROR(ERROR_NO_FREE_STORE);                            \
            macur->next->prev=macur;                                   \
        }                                                              \
        macur=macur->next;                                             \
        macnt=0;                                                       \
    }                                                                  \
    macur->marker[macnt].type=(t);                                     \
    macur->marker[macnt].pat=(p);                                      \
    macur->marker[macnt].str=(s);                                      \
    macnt++;                                                           \
}

#define POP(t,p,s)                      \
{                                       \
    macnt--;                            \
    if(macnt<0)                         \
    {                                   \
        macnt=127;                      \
        macur=macur->prev;              \
        if(macur==NULL)                 \
            ERROR(0);                   \
    }                                   \
    (t)=macur->marker[macnt].type;      \
    (p)=macur->marker[macnt].pat;       \
    (s)=macur->marker[macnt].str;       \
}

#define MP_ESCAPE   0x81
#define MP_MULT     0x82
#define MP_MULT_END 0x83
#define MP_NOT      0x84
#define MP_NOT_END  0x85
#define MP_OR       0x86
#define MP_OR_NEXT  0x87
#define MP_OR_END   0x88
#define MP_SINGLE   0x89
#define MP_ALL      0x8a
#define MP_SET      0x8b
#define MP_NOT_SET  0x8c
#define MP_DASH     0x8d
#define MP_SET_END  0x8e

/* Real: whether MatchFirst()/MatchNext()/MatchEnd() duplicate the base
   AChain's lock with DupLock() (0, the real default) or just reuse the
   caller's lock pointer. ACE's DupLock() is real (see native_dos.c), so
   there is no reason to take the second, lock-lifetime-riskier option. */
#define MATCHFUNCS_NO_DUPLOCK 0

struct AChain *Match_AllocAChain(LONG extrasize, struct DosLibrary *DOSBase);
void Match_FreeAChain(struct AChain *ac, struct DosLibrary *DOSBase);
LONG Match_BuildAChainList(CONST_STRPTR pattern, struct AnchorPath *ap,
                          struct AChain **retac, struct DosLibrary *DOSBase);
LONG Match_MakeResult(struct AnchorPath *ap, struct DosLibrary *DOSBase);

/*
 * exall.c's real ExAll() tries a fast path first -- a single
 * ACTION_EXAMINE_ALL packet to the handler process, real AROS's
 * dopacket5() macro -- before falling back to the Examine()/ExNext() loop
 * this seam is actually built on. This always answers ERROR_ACTION_NOT_KNOWN,
 * precisely the real response a filesystem gives when it does not support
 * ExAll() directly, sending exall.c down its own real fallback path
 * unconditionally. The fallback *is* real AROS logic, the same one AROS
 * itself uses whenever a filesystem doesn't implement the fast path either.
 *
 * A statement expression rather than a typed helper function: the real
 * call site declares its result-code pointer as SIPTR, matching real
 * AROS's own dopacket()/dopacket5() signature, and a fixed LONG* parameter
 * here would be an incompatible-pointer-type mismatch under -Werror. The
 * (void) casts reference every discarded argument, including port (which
 * the real call site reads as fl->fl_Task): otherwise that FileLock local
 * would be genuinely unused once this macro no longer references it,
 * tripping -Werror=unused-variable on unmodified real source.
 */
#define dopacket5(base, res2, port, action, arg1, arg2, arg3, arg4, arg5) \
    ({ (void)(port); (void)(action); (void)(arg1); (void)(arg2); \
       (void)(arg3); (void)(arg4); (void)(arg5); \
       *(res2) = ERROR_ACTION_NOT_KNOWN; (LONG)DOSFALSE; })

#endif
