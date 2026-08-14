#ifndef AMIGA_SHELL_AROS_LIBCALL_H
#define AMIGA_SHELL_AROS_LIBCALL_H

/*
 * Real AROS DOS sources (rom/dos's .c files) declare their library entry points
 * with AROS_LH<n>, generated on a real AROS build from the toolchain
 * configuration. ACE calls these functions directly rather than through a
 * library vector table, so the library-base parameter becomes an ordinary
 * file-scope object (DOSBase, already declared in dos/dos.h) instead of a
 * hidden argument -- the same seam compat/aros-real/include/aros/libcall.h
 * uses for the BOOPSI/graphics.library sources, restated here for the
 * dos .c-file compile group, which builds against compat/include instead.
 */

#define AROS_LHA(type, name, reg) type name
#define AROS_LDA(type, name, reg) type name

#define AROS_LH0(rt, name, bt, bn, lvo, lib) rt name(void)
#define AROS_LH1(rt, name, a1, bt, bn, lvo, lib) rt name(a1)
#define AROS_LH1I(rt, name, a1, bt, bn, lvo, lib) rt name(a1)
#define AROS_LH2(rt, name, a1, a2, bt, bn, lvo, lib) rt name(a1, a2)
#define AROS_LH2I(rt, name, a1, a2, bt, bn, lvo, lib) rt name(a1, a2)
#define AROS_LH3(rt, name, a1, a2, a3, bt, bn, lvo, lib) rt name(a1, a2, a3)
#define AROS_LH4(rt, name, a1, a2, a3, a4, bt, bn, lvo, lib) \
    rt name(a1, a2, a3, a4)
#define AROS_LH5(rt, name, a1, a2, a3, a4, a5, bt, bn, lvo, lib) \
    rt name(a1, a2, a3, a4, a5)

#define AROS_NTLH0(rt, name, bt, bn, lvo, lib) rt name(void)
#define AROS_NTLH1(rt, name, a1, bt, bn, lvo, lib) rt name(a1)

#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT

#define AROS_ASMSYMNAME(name) name

#endif
