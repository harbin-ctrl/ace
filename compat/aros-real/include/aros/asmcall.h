#ifndef ACE_AROS_REAL_ASMCALL_H
#define ACE_AROS_REAL_ASMCALL_H

/* Registerized AROS calls become ordinary C calls in the host build. */

#define AROS_ASMCALL_H

/*
 * AROS generates <aros/asmcall.h> from its build configuration.  On m68k the
 * macros below place arguments in the named registers; on a host with the
 * ordinary C convention they place them in source order and drop the register
 * names.
 *
 * The argument macros expand to the whole (type, name, register) triple, which
 * is how AROS lets one annotation serve a definition, a prototype and a call.
 * A triple only splits into three arguments once it is handed to one of the
 * selectors below, so an AROS_UFHA(...) still counts as a single argument
 * where it is written.
 */
#define AROS_UFHA(type, name, reg) type, name, reg
#define AROS_UFPA(type, name, reg) type, name, reg
#define AROS_UFCA(type, name, reg) type, name, reg

#define __AROS_UFHA(type, name, reg) type name   /* definition parameter */
#define __AROS_UFPA(type, name, reg) type        /* prototype parameter */
#define __AROS_UFCA(type, name, reg) name        /* call argument */

/*
 * Function headers.  The AROS sources supply their own braces around the body,
 * so these end at the parameter list and the INIT/EXIT brackets are empty.
 */
#define AROS_UFH0(t, n)             t n(void)
#define AROS_UFH1(t, n, a1)         t n(__AROS_UFHA(a1))
#define AROS_UFH2(t, n, a1, a2)     t n(__AROS_UFHA(a1), __AROS_UFHA(a2))
#define AROS_UFH3(t, n, a1, a2, a3) \
    t n(__AROS_UFHA(a1), __AROS_UFHA(a2), __AROS_UFHA(a3))
#define AROS_UFH4(t, n, a1, a2, a3, a4) \
    t n(__AROS_UFHA(a1), __AROS_UFHA(a2), __AROS_UFHA(a3), __AROS_UFHA(a4))

#define AROS_UFH0S(t, n)             static t n(void)
#define AROS_UFH1S(t, n, a1)         static t n(__AROS_UFHA(a1))
#define AROS_UFH2S(t, n, a1, a2)     static t n(__AROS_UFHA(a1), __AROS_UFHA(a2))
#define AROS_UFH3S(t, n, a1, a2, a3) \
    static t n(__AROS_UFHA(a1), __AROS_UFHA(a2), __AROS_UFHA(a3))

/* Prototypes. */
#define AROS_UFP0(t, n)             t n(void)
#define AROS_UFP1(t, n, a1)         t n(__AROS_UFPA(a1))
#define AROS_UFP2(t, n, a1, a2)     t n(__AROS_UFPA(a1), __AROS_UFPA(a2))
#define AROS_UFP3(t, n, a1, a2, a3) \
    t n(__AROS_UFPA(a1), __AROS_UFPA(a2), __AROS_UFPA(a3))
#define AROS_UFP4(t, n, a1, a2, a3, a4) \
    t n(__AROS_UFPA(a1), __AROS_UFPA(a2), __AROS_UFPA(a3), __AROS_UFPA(a4))

/*
 * Calls through a function pointer.  This is the seam CALLHOOKPKT() in
 * <utility/hooks.h> is built from, and therefore the point where every BOOPSI
 * method dispatch crosses from AROS's calling convention to the host's.
 */
#define AROS_UFC0(t, n) (((t (*)(void))(n))())
#define AROS_UFC1(t, n, a1) \
    (((t (*)(__AROS_UFPA(a1)))(n))(__AROS_UFCA(a1)))
#define AROS_UFC2(t, n, a1, a2) \
    (((t (*)(__AROS_UFPA(a1), __AROS_UFPA(a2)))(n))( \
        __AROS_UFCA(a1), __AROS_UFCA(a2)))
#define AROS_UFC3(t, n, a1, a2, a3) \
    (((t (*)(__AROS_UFPA(a1), __AROS_UFPA(a2), __AROS_UFPA(a3)))(n))( \
        __AROS_UFCA(a1), __AROS_UFCA(a2), __AROS_UFCA(a3)))
#define AROS_UFC4(t, n, a1, a2, a3, a4) \
    (((t (*)(__AROS_UFPA(a1), __AROS_UFPA(a2), __AROS_UFPA(a3), \
             __AROS_UFPA(a4)))(n))( \
        __AROS_UFCA(a1), __AROS_UFCA(a2), __AROS_UFCA(a3), __AROS_UFCA(a4)))

#ifndef AROS_USERFUNC_INIT
#define AROS_USERFUNC_INIT
#endif
#ifndef AROS_USERFUNC_EXIT
#define AROS_USERFUNC_EXIT
#endif

#endif
