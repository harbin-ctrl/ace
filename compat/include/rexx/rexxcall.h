#ifndef REXX_REXXCALL_H
#define REXX_REXXCALL_H

#include <exec/types.h>
#include <exec/libraries.h>

struct RexxMsg;

/* ACE has no Amiga vector table.  Regina still needs the AROS query-call
 * contract, so route it through the host-side library dispatcher instead of
 * pretending that a numeric vector offset is callable machine code. */
ULONG ace_rexx_call_query_lib_func(struct RexxMsg *rexxmsg,
                                   struct Library *libbase,
                                   SIPTR offset,
                                   UBYTE **retargstringptr);

/* AROS uses vector calls for resource callbacks.  ACE's host ABI has no
 * Amiga vector table.  Resource cleanup remains a separate follow-up seam;
 * the query call used by ordinary Rexx functions is fully host-dispatched. */
#ifndef AROS_LCA
#define AROS_LCA(type, name, reg) type name
#endif
#ifndef AROS_LVO_CALL1NR
#define AROS_LVO_CALL1NR(return_type, arg, base_type, base, offset, name) ((void)0)
#endif
#ifndef AROS_LVO_CALL2
#define AROS_LVO_CALL2(return_type, arg1, arg2, base_type, base, offset, name) ((return_type)0)
#endif

#define RexxCallQueryLibFunc(rexxmsg, libbase, offset, retargstringptr) \
    ace_rexx_call_query_lib_func((rexxmsg), (libbase), (offset), \
                                  (retargstringptr))

#define AROS_AREXXLIBQUERYFUNC(f,m,lt,l,o,p) \
    AROS_LH2(ULONG, f, AROS_LHA(struct RexxMsg *, m, A0), \
             AROS_LHA(STRPTR *, _retargstringptr, A1), lt, l, o, p) { AROS_LIBFUNC_INIT
#define AROS_AREXXLIBQUERYFUNC_END AROS_LIBFUNC_EXIT }
#define ReturnRexxQuery(rc,arg) ({ *_retargstringptr = arg; return rc; })

#endif
