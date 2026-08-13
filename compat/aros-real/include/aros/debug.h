#ifndef ACE_AROS_REAL_DEBUG_H
#define ACE_AROS_REAL_DEBUG_H

/*
 * The real handler's debug calls are disabled for the first host build.
 * D() expands to nothing rather than to an empty statement, as it does in a
 * non-debug AROS build: several AROS sources write a D(...) block with no
 * trailing semicolon, which only parses if the macro leaves nothing behind.
 */
#define D(statement)

/*
 * Real AROS: #define bug kprintf (compiler/arossupport/include/debug.h),
 * backed by AROSSupportBase.kprintf. Diagnostic-only kernel-log output, not
 * gated by D()/#if DEBUG at every call site (support.c's printstring() calls
 * it unconditionally, though nothing in ACE calls printstring() itself), so
 * it needs to exist even with debugging off, the same way it does on a real
 * non-debug AROS build.
 */
#define bug(...) ((void)0)

#ifndef ASSERT_VALID_PTR
#define ASSERT_VALID_PTR(pointer) do { (void)(pointer); } while (0)
#endif

#ifndef ASSERT_VALID_PTR_OR_NULL
#define ASSERT_VALID_PTR_OR_NULL(pointer) do { (void)(pointer); } while (0)
#endif

/*
 * The BOOPSI sources trace entry and exit through these.  With debugging off
 * the tracing disappears and the Return* forms are simply the return itself,
 * which is what the AROS non-debug build does too.
 */
#ifndef EnterFunc
#define EnterFunc(statement) do { } while (0)
#endif
#ifndef ReturnVoid
#define ReturnVoid(name) return
#endif
#ifndef ReturnPtr
#define ReturnPtr(name, type, value) return (value)
#endif
#ifndef ReturnInt
#define ReturnInt(name, type, value) return (value)
#endif
#ifndef ReturnBool
#define ReturnBool(name, value) return (value)
#endif

#endif
