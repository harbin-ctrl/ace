#ifndef ACE_AROS_REAL_ATOMIC_H
#define ACE_AROS_REAL_ATOMIC_H

/*
 * AROS generates <aros/atomic.h> per architecture.  The BOOPSI sources use it
 * for the class subclass/object reference counts, which are updated while the
 * class list semaphore is held but read outside it.  The host build maps them
 * onto the compiler's atomic builtins.
 */

#define AROS_ATOMIC_H

#define AROS_ATOMIC_INC(var) \
    (void)__atomic_add_fetch(&(var), 1, __ATOMIC_SEQ_CST)
#define AROS_ATOMIC_DEC(var) \
    (void)__atomic_sub_fetch(&(var), 1, __ATOMIC_SEQ_CST)
#define AROS_ATOMIC_ADD(var, val) \
    (void)__atomic_add_fetch(&(var), (val), __ATOMIC_SEQ_CST)
#define AROS_ATOMIC_SUB(var, val) \
    (void)__atomic_sub_fetch(&(var), (val), __ATOMIC_SEQ_CST)
#define AROS_ATOMIC_AND(var, mask) \
    (void)__atomic_and_fetch(&(var), (mask), __ATOMIC_SEQ_CST)
#define AROS_ATOMIC_OR(var, mask) \
    (void)__atomic_or_fetch(&(var), (mask), __ATOMIC_SEQ_CST)

#endif
