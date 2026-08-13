#ifndef ACE_AROS_REAL_PROTO_UTILITY_H
#define ACE_AROS_REAL_PROTO_UTILITY_H

#ifdef ACE_GRAPHICS_INTERN_H

#include <exec/types.h>
#include <utility/tagitem.h>

/*
 * consoleclass.c's one utility.library call. rom/utility/gettagdata.c's own
 * body is a one-line pass-through to compiler/arossupport's
 * LibGetTagData()/LibFindTagItem()/LibNextTagItem(), which ACE compiles as
 * real AROS source (see the AROS_ARSUPPORT_OBJS build rule); this
 * declaration and its equally small definition in
 * src/aros_graphics_runtime.c are the pass-through wrapper only, since the
 * real wrapper needs rom/utility/intern.h -- a private header with a much
 * larger shadow than its one line of logic is worth.
 */
IPTR GetTagData(Tag tagValue, IPTR defaultVal, const struct TagItem *tagList);

#endif /* ACE_GRAPHICS_INTERN_H */

#endif
