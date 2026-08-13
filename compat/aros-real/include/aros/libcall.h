#ifndef ACE_AROS_REAL_LIBCALL_H
#define ACE_AROS_REAL_LIBCALL_H

/*
 * AROS generates <aros/libcall.h> from its own build configuration, so a host
 * build has to supply it.  This is a genuine seam: an AROS library entry point
 * carries its arguments in m68k-style registers and reaches its library base
 * through the call, while a Linux host uses the ordinary C calling convention
 * and a single resolved base.
 *
 * The AROS_LH<n> forms below therefore drop the register annotations and the
 * trailing base/LVO/library descriptors, leaving a plain C function whose body
 * is the unmodified AROS source.  The library base named by the descriptor is
 * a file-scope object instead of a parameter; see ace_boopsi_intern.h, which
 * declares IntuitionBase for the BOOPSI sources.
 */

#define AROS_LIBCALL_H

#define AROS_LHA(type, name, reg) type name
#define AROS_LDA(type, name, reg) type name
#define AROS_LPA(type, name, reg) type name

#define AROS_LH0(rt, name, bt, bn, lvo, lib) \
    rt name(void)
#define AROS_LH1(rt, name, a1, bt, bn, lvo, lib) \
    rt name(a1)
#define AROS_LH2(rt, name, a1, a2, bt, bn, lvo, lib) \
    rt name(a1, a2)
#define AROS_LH3(rt, name, a1, a2, a3, bt, bn, lvo, lib) \
    rt name(a1, a2, a3)
#define AROS_LH4(rt, name, a1, a2, a3, a4, bt, bn, lvo, lib) \
    rt name(a1, a2, a3, a4)
#define AROS_LH5(rt, name, a1, a2, a3, a4, a5, bt, bn, lvo, lib) \
    rt name(a1, a2, a3, a4, a5)
#define AROS_LH6(rt, name, a1, a2, a3, a4, a5, a6, bt, bn, lvo, lib) \
    rt name(a1, a2, a3, a4, a5, a6)
#define AROS_LH7(rt, name, a1, a2, a3, a4, a5, a6, a7, bt, bn, lvo, lib) \
    rt name(a1, a2, a3, a4, a5, a6, a7)

/* Prototype forms, used when a header declares what a source later defines. */
#define AROS_LP0(rt, name, bt, bn, lvo, lib)                           rt name(void)
#define AROS_LP1(rt, name, a1, bt, bn, lvo, lib)                       rt name(a1)
#define AROS_LP2(rt, name, a1, a2, bt, bn, lvo, lib)                   rt name(a1, a2)
#define AROS_LP3(rt, name, a1, a2, a3, bt, bn, lvo, lib)               rt name(a1, a2, a3)
#define AROS_LP4(rt, name, a1, a2, a3, a4, bt, bn, lvo, lib)           rt name(a1, a2, a3, a4)
#define AROS_LP5(rt, name, a1, a2, a3, a4, a5, bt, bn, lvo, lib)       rt name(a1, a2, a3, a4, a5)

/*
 * The AROS sources bracket every library function body with these.  A host
 * build has no library base to load and no register frame to save, so both
 * are empty and the braces already present in the AROS source delimit the
 * function body directly.
 */
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT

#ifndef AROS_ASMSYMNAME
#define AROS_ASMSYMNAME(name) name
#endif

#endif
