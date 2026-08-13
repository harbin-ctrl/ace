#ifndef ACE_AROS_REAL_PROTO_INTUITION_H
#define ACE_AROS_REAL_PROTO_INTUITION_H

#include <intuition/intuitionbase.h>

#ifdef ACE_BOOPSI_INTERN_H

#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

/*
 * The BOOPSI entry points implemented by the real AROS sources in
 * rom/intuition.  A real AROS build reaches these through the Intuition
 * library vector table; the host build calls them directly, so the
 * declarations match what <aros/libcall.h> generates from AROS_LH.
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

#endif /* ACE_BOOPSI_INTERN_H */

#endif
