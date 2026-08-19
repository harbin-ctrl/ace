#ifndef ACE_REGINA_COMPAT_H
#define ACE_REGINA_COMPAT_H

/*
 * The Regina-only corner of ACE's compatibility headers.
 *
 * Regina must see compat/aros-real/include before compat/include: its
 * amifuncs.c needs the real AROS exec/execbase.h and exec/lists.h, and the
 * older shadowing copies under compat/include/exec/ make that file fail with
 * 26 diagnostics.  Reversing the two fixes amifuncs.c and breaks os_amiga.c,
 * because the aros-real tree carries its own proto/dos.h and proto/alib.h --
 * thin files holding PathPart() and a BOOPSI-guarded block that expands to
 * nothing outside BOOPSI -- and they win the <proto/dos.h> and <proto/alib.h>
 * lookups.  StrDup() and SystemTags() are then invisible even though ACE
 * implements both.
 *
 * So this header exists to say: nothing is missing, only hidden.  It reaches
 * the shadowed declarations by explicit relative path, which is the one form
 * that names a specific file rather than asking the search order to pick.
 * Both headers carry their own include guards, so a translation unit that
 * also reaches them the ordinary way is unaffected.
 *
 * Deliberately not a third proto/ tree: another directory of proto/dos.h and
 * proto/alib.h would add a third candidate to the same ambiguous lookup and
 * make the shadowing worse.  This is force-included instead (-include), the
 * pattern ACE already uses for ace_amiga_posix.h in the LhA build.
 *
 * Add to this file only what the aros-real tree genuinely hides.  Anything
 * ACE does not implement at all belongs in the shared headers next to its
 * implementation, not here -- CreateNewProcTags() being the current example.
 */

#include "../../include/proto/alib.h"   /* StrDup() */
#include "../../include/proto/dos.h"    /* SystemTags(), SystemTagList() */

#endif /* ACE_REGINA_COMPAT_H */
