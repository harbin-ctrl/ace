#ifndef AROS_AROSSUPPORTBASE_H
#define AROS_AROSSUPPORTBASE_H

/*
 * This is compiler/arossupport/include/arossupportbase.h verbatim.
 * <clib/arossupport_protos.h> expects to reach it as <aros/
 * arossupportbase.h>; AROS's own build copies/symlinks it there as part of
 * generating its include tree, a step this checkout has not run (the same
 * gap con_libdefs.h and devices/conunit.h fill elsewhere in compat/).  The
 * real file lives one directory level away from where it is included from,
 * with no header of its own restating it, so it is restated here instead.
 */

#ifndef EXEC_LISTS_H
#include <exec/lists.h>
#endif

#include <stdarg.h>

#if defined(__GNUC__)
#define ATTRIB_FMT(a,b)  __attribute__ ((format (printf, a, b)))
#else
#define ATTRIB_FMT(a,b)
#endif

struct AROSSupportBase
{
    IPTR            _pad;
    int             (*kprintf)(const char *, ...) ATTRIB_FMT(1, 2);
    int             (*rkprintf)(const char *, const char *, int, const char *, ...) ATTRIB_FMT(4, 5);
    int             (*vkprintf)(const char *, va_list);
};

#endif /* AROS_AROSSUPPORTBASE_H */
