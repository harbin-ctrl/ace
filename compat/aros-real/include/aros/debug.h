#ifndef ACE_AROS_REAL_DEBUG_H
#define ACE_AROS_REAL_DEBUG_H

/* The real handler's debug calls are disabled for the first host build. */
#define D(statement) do { } while (0)

#ifndef ASSERT_VALID_PTR
#define ASSERT_VALID_PTR(pointer) do { (void)(pointer); } while (0)
#endif

#endif
