#ifndef ACE_BOOPSI_INTERN_H
#define ACE_BOOPSI_INTERN_H

/*
 * ACE compiles the real AROS BOOPSI sources from rom/intuition.  Those sources
 * include "intuition_intern.h", which is the private header of the whole
 * Intuition library: 1500 lines describing screens, windows, gadgets, menus,
 * decorators and preferences.  BOOPSI itself needs almost none of it.
 *
 * This header is force-included ahead of the AROS sources and claims the real
 * header's include guard, so rom/intuition/intuition_intern.h is read and
 * skipped.  What follows is the subset the class, object and dispatch sources
 * actually reference.  Nothing here reimplements BOOPSI; the class list, the
 * rootclass and every method call come from the AROS sources themselves.
 */
#define INTUITION_INTERN_H

/*
 * The real intuition_intern.h pulls these in for its callers.  Shadowing it
 * means providing them here, or the AROS_LH library-entry and atomic macros
 * would be missing from every BOOPSI source.
 */
#include <aros/libcall.h>
#include <aros/atomic.h>
#include <aros/debug.h>

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

/*
 * The BOOPSI sources reach their library base through the name given in the
 * AROS_LH descriptor.  The host build has one Intuition base, so the name is a
 * file-scope object rather than a hidden parameter.  Only the class list and
 * the rootclass are present: GetPrivIBase() casts to this, exactly as the AROS
 * macro does, so the source text is unchanged.
 */
struct IntIntuitionBase
{
    struct SignalSemaphore ClassListLock;
    struct MinList         ClassList;
    struct IClass          RootClass;
};

extern struct IntIntuitionBase *IntuitionBase;

#define GetPrivIBase(base) ((struct IntIntuitionBase *)(base))

/* Truncation of the instance size argument, as in intuition_intern.h. */
#define EXTENDUWORD(x) x = (ULONG)((UWORD)x);

/* The non-debug forms of the AROS sanity checks. */
#define SANITY_CHECK(x)      if (!((IPTR)x)) return;
#define SANITY_CHECKR(x, v)  if (!((IPTR)x)) return v;

/* Debug tracing is compiled out, as in a non-debug AROS build. */
#define DEBUG_ADDCLASS(x)
#define DEBUG_DISPOSEOBJECT(x)
#define DEBUG_FINDCLASS(x)
#define DEBUG_FREECLASS(x)
#define DEBUG_GETATTR(x)
#define DEBUG_MAKECLASS(x)
#define DEBUG_NEWOBJECT(x)
#define DEBUG_NEXTOBJECT(x)
#define DEBUG_REMOVECLASS(x)
#define DEBUG_SETATTRS(x)

#endif
