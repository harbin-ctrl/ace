#ifndef AMIGA_SHELL_AROS_ASMCALL_H
#define AMIGA_SHELL_AROS_ASMCALL_H

/* Registerized AROS calls are ordinary C calls in the host build. */

#define __AROS_UFHA(type, name, reg) type name
#define __AROS_UFPA(type, name, reg) type
#define __AROS_UFCA(type, name, reg) name

#define AROS_UFHA(type, name, reg) type, name, reg
#define AROS_UFPA(type, name, reg) type, name, reg
#define AROS_UFCA(type, name, reg) type, name, reg

#define AROS_UFH0(t, n) t n(void)
#define AROS_UFH1(t, n, a1) t n(__AROS_UFHA(a1))
#define AROS_UFH2(t, n, a1, a2) t n(__AROS_UFHA(a1), __AROS_UFHA(a2))
#define AROS_UFH3(t, n, a1, a2, a3) \
    t n(__AROS_UFHA(a1), __AROS_UFHA(a2), __AROS_UFHA(a3))

#define AROS_UFC3(t, n, a1, a2, a3) \
    (((t (*)(__AROS_UFPA(a1), __AROS_UFPA(a2), __AROS_UFPA(a3)))(n))( \
        __AROS_UFCA(a1), __AROS_UFCA(a2), __AROS_UFCA(a3)))

#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT

/* exall.c's eac_MatchFunc hook call. Dir.c never sets eac_MatchFunc (NULL,
   from AllocDosObject's zeroed allocation), so this compiles the call site
   without ever actually being invoked in the profile ACE runs. */
#ifndef CALLHOOKPKT
#define CALLHOOKPKT(hook, object, message) \
    (((IPTR (*)(void *, void *, void *))((hook)->h_Entry))( \
        (void *)(hook), (void *)(object), (void *)(message)))
#endif

#endif
