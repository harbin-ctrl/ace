#ifndef ACE_AROS_REAL_PROTO_ALIB_H
#define ACE_AROS_REAL_PROTO_ALIB_H

#ifdef ACE_BOOPSI_INTERN_H

#include <stdarg.h>

#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

/*
 * The BOOPSI slice of amiga.lib, implemented by the real AROS sources in
 * compiler/alib.
 *
 * AROS's own <clib/alib_protos.h> would declare these, but it declares the
 * whole of amiga.lib with it and pulls in screens, gadtools, commodities and
 * rexx to do so.  BOOPSI needs none of that, and dragging it in would mean
 * standing up most of Intuition to compile a class system that does not
 * depend on it.  Only the method-dispatch surface is restated here.
 */
IPTR DoMethodA(Object *obj, Msg message);
IPTR DoMethod(Object *obj, IPTR MethodID, ...);
IPTR DoSuperMethodA(Class *cl, Object *obj, Msg message);
IPTR DoSuperMethod(Class *cl, Object *obj, IPTR MethodID, ...);
IPTR CoerceMethodA(Class *cl, Object *obj, Msg message);
IPTR CoerceMethod(Class *cl, Object *obj, IPTR MethodID, ...);

/*
 * How a varargs method call reaches its message.  Both forms below are AROS's,
 * selected by AROS_SLOWSTACKMETHODS from arch/<cpu>/include/aros/cpu.h, which
 * <exec/types.h> has already pulled in.  ACE does not make this choice: AROS
 * sets the flag for exactly those architectures whose ABI does not pass
 * varargs contiguously above the first argument, so aarch64 and x86_64 hosts
 * marshal through GetMsgFromStack() while i386 casts the frame in place.
 */
#ifndef AROS_METHODRETURNTYPE
#define AROS_METHODRETURNTYPE IPTR
#endif

#ifdef AROS_SLOWSTACKMETHODS

/* compiler/alib/alib_util.c */
Msg  GetMsgFromStack(IPTR MethodID, va_list args);
void FreeMsgFromStack(Msg msg);

#define AROS_SLOWSTACKMETHODS_PRE_AS(arg, type) \
    type    retval;                             \
    va_list args;                               \
    Msg     msg;                                \
                                                \
    va_start(args, arg);                        \
                                                \
    if ((msg = GetMsgFromStack(arg, args)))     \
    {

#define AROS_SLOWSTACKMETHODS_PRE(arg) \
    AROS_SLOWSTACKMETHODS_PRE_AS(arg, AROS_METHODRETURNTYPE)

#define AROS_SLOWSTACKMETHODS_ARG(arg) msg

#define AROS_SLOWSTACKMETHODS_POST            \
        FreeMsgFromStack(msg);                \
    }                                         \
    else                                      \
        retval = (AROS_METHODRETURNTYPE)0L;   \
                                              \
    va_end(args);                             \
                                              \
    return retval;

#else /* arguments are contiguous above the first one */

#define AROS_SLOWSTACKMETHODS_PRE(arg)          AROS_METHODRETURNTYPE retval;
#define AROS_SLOWSTACKMETHODS_PRE_AS(arg, type) type retval;
#define AROS_SLOWSTACKMETHODS_ARG(arg)          ((Msg)&(arg))
#define AROS_SLOWSTACKMETHODS_POST              return retval;

#endif /* AROS_SLOWSTACKMETHODS */

#endif /* ACE_BOOPSI_INTERN_H */

#endif
