#ifndef ACE_AROS_REAL_PROTO_GRAPHICS_H
#define ACE_AROS_REAL_PROTO_GRAPHICS_H

#ifdef ACE_GRAPHICS_INTERN_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

/*
 * graphics.library, implemented by src/aros_graphics_runtime.c. Signatures
 * match the real ones in rom/graphics/{move,text,setapen,setbpen,setdrmd,
 * setabpendrmd,rectfill,scrollraster,setsoftstyle,allocraster,freeraster,
 * inittmpras}.c -- this is the one seam in ACE that is authored rather than
 * compiled from AROS source, since graphics.library is the actual hardware
 * boundary; see the file header of aros_graphics_runtime.c for why.
 */
void  Move(struct RastPort *rp, WORD x, WORD y);
void  Text(struct RastPort *rp, CONST_STRPTR string, ULONG count);
void  SetAPen(struct RastPort *rp, ULONG pen);
void  SetBPen(struct RastPort *rp, ULONG pen);
void  SetDrMd(struct RastPort *rp, ULONG drawMode);
void  SetABPenDrMd(struct RastPort *rp, ULONG apen, ULONG bpen, ULONG drawMode);
void  RectFill(struct RastPort *rp, WORD xMin, WORD yMin, WORD xMax, WORD yMax);
void  ScrollRaster(struct RastPort *rp, WORD dx, WORD dy, WORD xMin, WORD yMin,
                   WORD xMax, WORD yMax);
ULONG SetSoftStyle(struct RastPort *rp, ULONG style, ULONG enable);

PLANEPTR AllocRaster(UWORD width, UWORD height);
void     FreeRaster(PLANEPTR p, UWORD width, UWORD height);
struct TmpRas *InitTmpRas(struct TmpRas *tmpRas, PLANEPTR buffer, LONG size);

#endif /* ACE_GRAPHICS_INTERN_H */

#endif
