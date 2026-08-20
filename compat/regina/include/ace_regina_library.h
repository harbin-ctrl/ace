#ifndef ACE_REGINA_LIBRARY_H
#define ACE_REGINA_LIBRARY_H

/*
 * The Regina *library* build, as opposed to the standalone `rexx` program.
 *
 * Force-included on top of ace_regina_compat.h for the object set that
 * upstream builds as regina.library: it adds rexxsaa.c (RexxStart), client.c,
 * mt_amigalib.c and isreginamsg.c, which RexxMast links against.
 *
 * Those files use AROS's library-lifecycle macros -- ADD2INITLIB,
 * ADD2CLOSELIB, ADD2EXPUNGELIB. They are not in any AROS header: genmodule
 * generates them into the module's own libdefs.h as part of building a
 * .library, and there is no such build here. ACE links Regina statically, so
 * "the library is opened" is the process starting and "closed" is it exiting,
 * and constructors and destructors say exactly that.
 *
 * The priority argument is dropped. It orders initialisers among themselves
 * within one AROS module; the two Regina uses are independent, and inventing
 * an ordering guarantee that is not really enforced would be worse than not
 * claiming one.
 */

/*
 * Deliberately does not include ace_regina_compat.h. Regina's own sources get
 * both, in that order, from REGINA_LIB_CFLAGS; src/regina_library_init.c gets
 * only this one, because it is ACE's own code and has no need of the
 * shadowed-declaration repairs that header exists for.
 */

/*
 * The priority argument is dropped, but the *order between the three kinds*
 * is not, because on AROS it is not a preference: a library is closed by its
 * last user before it is expunged, so ExpungeLib runs after every CloseLib
 * and may free what CloseLib still reads. Flattening all three to plain
 * constructors and destructors threw that away, and the order then fell out
 * of link order -- which put ACE's pool teardown first and made
 * mt_amigalib.c's CloseLib() dereference a NULL __regina_tsdlist at exit.
 *
 * Three tiers, outermost first. GCC runs constructors in ascending priority
 * and destructors in the reverse, so one number per tier gives both halves:
 * set up outermost first, tear down outermost last.
 *
 *   LIBRARY  what regina_init.c owned upstream -- the per-task list and the
 *            pool it lives in. src/regina_library_init.c stands in for that
 *            file and uses this tier directly. Everything else needs it.
 *   MODULE   Regina's own whole-library pair, InitLib and ExpungeLib.
 *   TASK     per-task state, mt_amigalib.c's CloseLib. Innermost: last up,
 *            first down.
 */
#define ACE_REGINA_PRIORITY_LIBRARY 101
#define ACE_REGINA_PRIORITY_MODULE  102
#define ACE_REGINA_PRIORITY_TASK    103

/* Runs before main(). */
#define ADD2INITLIB(function, priority)                                     \
    static void                                                             \
    __attribute__((constructor(ACE_REGINA_PRIORITY_MODULE)))                \
    ace_regina_initlib_##function(void) { (void)function(NULL); }

/* Runs at exit. On AROS this fires when a task closes the library, which
   matters there because the library outlives the task; here the library and
   the process are the same thing, so exit is the moment. */
#define ADD2CLOSELIB(function, priority)                                    \
    static void                                                             \
    __attribute__((destructor(ACE_REGINA_PRIORITY_TASK)))                   \
    ace_regina_closelib_##function(void) { (void)function(NULL); }

#define ADD2EXPUNGELIB(function, priority)                                  \
    static void                                                             \
    __attribute__((destructor(ACE_REGINA_PRIORITY_MODULE)))                 \
    ace_regina_expungelib_##function(void) { (void)function(NULL); }

#endif /* ACE_REGINA_LIBRARY_H */
