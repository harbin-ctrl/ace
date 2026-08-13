#ifndef ACE_GRAPHICS_INTERN_H
#define ACE_GRAPHICS_INTERN_H

/*
 * ACE compiles the real AROS console.device classes -- rom/devs/console/
 * {stdconclass,consoleclass}.c -- against the ACE graphics/Intuition/exec
 * seam. This is their forced include (see AROS_GRAPHICS_CFLAGS in the
 * Makefile), the same role ace_boopsi_intern.h plays for rom/intuition and
 * ace_handler_types.h for the console handler.
 *
 * Unlike those two, this header does not shadow an AROS private header --
 * stdconclass.c and consoleclass.c include their own real dependencies
 * directly, and none of those is a private intern.h this checkout is
 * missing. What they need beyond their own includes is TaggedOpenLibrary(),
 * which ACE implements in src/aros_graphics_runtime.c; the TAGGEDOPEN_
 * constants it takes are real and already in exec/libraries.h, pulled in
 * below to guarantee they are visible before any use regardless of what
 * else a given translation unit happens to include first.
 *
 * The DrawInfo pen indices (DETAILPEN..NUMDRIPENS) GetScreenDrawInfo() fills
 * in are likewise real, in this checkout's intuition/screens.h, included
 * from proto/intuition.h's ACE_GRAPHICS_INTERN_H block.
 */

#include <exec/libraries.h>

/*
 * clib/arossupport_protos.h (used by compiler/arossupport's
 * LibFindTagItem/LibNextTagItem, which ACE compiles as real AROS source for
 * GetTagData -- see proto/utility.h) uses the Tag typedef without including
 * utility/tagitem.h itself, ordinarily already loaded by the time a real
 * AROS translation unit reaches it. Forced in here for the same reason.
 */
#include <utility/tagitem.h>

#endif
