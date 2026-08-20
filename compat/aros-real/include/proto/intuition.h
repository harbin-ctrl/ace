#ifndef ACE_AROS_REAL_PROTO_INTUITION_H
#define ACE_AROS_REAL_PROTO_INTUITION_H

#include <intuition/intuitionbase.h>

struct Window;
struct EasyStruct;
LONG EasyRequest(struct Window *window, struct EasyStruct *easyStruct,
                 ULONG *IDCMP_ptr, ...);

#if defined(ACE_BOOPSI_INTERN_H) || defined(ACE_GRAPHICS_INTERN_H)

#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

/*
 * The BOOPSI entry points implemented by the real AROS sources in
 * rom/intuition.  A real AROS build reaches these through the Intuition
 * library vector table; the host build calls them directly, so the
 * declarations match what <aros/libcall.h> generates from AROS_LH.
 *
 * The console classes in rom/devs/console (built under
 * ACE_GRAPHICS_INTERN_H) reach three of these -- MakeClass,
 * DoSuperMethodA in proto/alib.h, and CoerceMethodA also in proto/alib.h --
 * the same way the BOOPSI sources themselves do, so they share this
 * declaration set rather than duplicating it.
 */
struct IClass *MakeClass(ClassID classID, ClassID superClassID,
                         struct IClass *superClassPtr, ULONG instanceSize,
                         ULONG flags);
BOOL           FreeClass(struct IClass *iclass);
void           AddClass(struct IClass *classPtr);
void           RemoveClass(struct IClass *classPtr);
struct IClass *FindClass(ClassID classID);

APTR  NewObjectA(struct IClass *classPtr, UBYTE *classID,
                 struct TagItem *tagList);
void  DisposeObject(APTR object);
ULONG SetAttrsA(APTR object, struct TagItem *tagList);
ULONG GetAttr(ULONG attrID, Object *object, IPTR *storagePtr);
APTR  NextObject(APTR objectPtrPtr);

#endif /* ACE_BOOPSI_INTERN_H || ACE_GRAPHICS_INTERN_H */

#ifdef ACE_GRAPHICS_INTERN_H

#include <intuition/screens.h>

/*
 * stdconclass.c's constructor reads a screen's DrawInfo to remap the
 * console's initial background/text pens; see
 * src/aros_graphics_runtime.c's GetScreenDrawInfo().
 */
struct DrawInfo *GetScreenDrawInfo(struct Screen *screen);
void             FreeScreenDrawInfo(struct Screen *screen,
                                    struct DrawInfo *drawInfo);

#endif /* ACE_GRAPHICS_INTERN_H */

#endif
