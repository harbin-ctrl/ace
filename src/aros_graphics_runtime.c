/*
 * ACE's graphics.library: the RastPort/BitMap/Text() surface the real AROS
 * console classes (rom/devs/console/{stdconclass,consoleclass}.c) draw
 * through.
 *
 * This file is deliberately authored, not compiled from AROS source. Every
 * other seam in ACE exists to run more real AROS code; this one is the
 * hardware boundary itself -- the point past which "real AROS code" would
 * mean a HIDD driver stack talking to a framebuffer, which is not what ACE
 * wants to become. What AROS's console classes require of graphics.library
 * is ten drawing calls plus a scratch-buffer trio for the character-cell
 * cursor; the shapes and semantics below come from reading those calls'
 * real implementations in rom/graphics, not from guessing.
 *
 * Fonts are the one place ACE improves on real Amiga hardware rather than
 * emulating it faithfully. Real console.device only ever reads three scalars
 * off a TextFont -- tf_XSize, tf_YSize, tf_Baseline -- to lay out a fixed
 * character-cell grid ("For now one should use only non-proportional
 * fonts", consoleclass.c's own words); it never reads tf_CharData or
 * tf_CharLoc in this codepath. So the glyphs themselves are free to come
 * from a host TrueType font chosen by the user rather than a bitmap font
 * ACE would have to draw and ship. SetSoftStyle()'s bold/italic requests
 * are rendered with the font's own real bold/italic faces rather than
 * synthesized by smearing or shearing a single face the way real Amiga
 * hardware did -- ace_gfx_font_family_complete() is what enforces that a
 * chosen family actually has those faces before it can be selected.
 * Underline is drawn as a rule below the baseline either way: it is a
 * soft-style flag on real hardware too, never a font glyph.
 */

#include "aros_graphics_runtime.h"

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <fontconfig/fontconfig.h>

#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/arossupport.h>

#include "ace_graphics_intern.h"

/* -------------------------------------------------------------------------
 * Fonts
 *
 * struct TextFont is a real, publicly laid-out AROS struct -- callers
 * dereference rp->Font->tf_XSize directly -- so it cannot carry ACE-private
 * fields itself. ace_gfx_font_private embeds it as the first member instead,
 * the same "public struct as the head of a private one" shape
 * aros_boopsi_runtime.c already uses for pool blocks: a struct TextFont*
 * handed to a caller is safely cast back to its owning private struct
 * because the two addresses coincide.
 * ---------------------------------------------------------------------- */

#define ACE_GFX_STYLE_REGULAR     0
#define ACE_GFX_STYLE_BOLD        1
#define ACE_GFX_STYLE_ITALIC      2
#define ACE_GFX_STYLE_BOLD_ITALIC 3

struct ace_gfx_font_private {
    struct TextFont tf;
    char family[128];
    cairo_font_face_t *face[4];
};

static cairo_font_face_t *load_face(const char *family, int bold, int italic)
{
    FcPattern *pattern;
    FcPattern *matched;
    FcResult result;
    cairo_font_face_t *face;

    pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddInteger(pattern, FC_SLANT,
                        italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddInteger(pattern, FC_WEIGHT,
                        bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    matched = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);
    if (!matched)
        return NULL;

    /*
     * FcFontMatch() always returns *something* -- fontconfig substitutes a
     * fallback family rather than failing. A real bold/italic face for the
     * requested family is confirmed by checking the matched pattern named
     * the family we asked for and the style we asked for, not by assuming
     * a match means what was asked for was found.
     */
    {
        FcChar8 *matched_family = NULL;
        int matched_slant = FC_SLANT_ROMAN;
        int matched_weight = FC_WEIGHT_REGULAR;

        FcPatternGetString(matched, FC_FAMILY, 0, &matched_family);
        FcPatternGetInteger(matched, FC_SLANT, 0, &matched_slant);
        FcPatternGetInteger(matched, FC_WEIGHT, 0, &matched_weight);

        if (!matched_family || strcasecmp((const char *)matched_family, family) != 0 ||
            (italic && matched_slant == FC_SLANT_ROMAN) ||
            (bold && matched_weight < FC_WEIGHT_BOLD)) {
            FcPatternDestroy(matched);
            return NULL;
        }
    }

    face = cairo_ft_font_face_create_for_pattern(matched);
    FcPatternDestroy(matched);
    if (cairo_font_face_status(face) != CAIRO_STATUS_SUCCESS) {
        cairo_font_face_destroy(face);
        return NULL;
    }
    return face;
}

int ace_gfx_font_family_complete(const char *family)
{
    cairo_font_face_t *faces[4];
    int i;
    int complete = 1;

    faces[ACE_GFX_STYLE_REGULAR] = load_face(family, 0, 0);
    faces[ACE_GFX_STYLE_BOLD] = load_face(family, 1, 0);
    faces[ACE_GFX_STYLE_ITALIC] = load_face(family, 0, 1);
    faces[ACE_GFX_STYLE_BOLD_ITALIC] = load_face(family, 1, 1);

    for (i = 0; i < 4; i++) {
        if (!faces[i])
            complete = 0;
        else
            cairo_font_face_destroy(faces[i]);
    }
    return complete;
}

struct TextFont *ace_gfx_load_font(const struct ace_gfx_font_choice *choice,
                                   const char **reason_out)
{
    struct ace_gfx_font_private *priv;
    cairo_surface_t *probe_surface;
    cairo_t *probe_cr;
    cairo_font_extents_t extents;
    cairo_text_extents_t cell_extents;

    if (!choice || !choice->family || choice->pixel_size <= 0) {
        if (reason_out)
            *reason_out = "invalid font choice";
        return NULL;
    }

    priv = calloc(1, sizeof(*priv));
    if (!priv) {
        if (reason_out)
            *reason_out = "out of memory";
        return NULL;
    }

    priv->face[ACE_GFX_STYLE_REGULAR] = load_face(choice->family, 0, 0);
    priv->face[ACE_GFX_STYLE_BOLD] = load_face(choice->family, 1, 0);
    priv->face[ACE_GFX_STYLE_ITALIC] = load_face(choice->family, 0, 1);
    priv->face[ACE_GFX_STYLE_BOLD_ITALIC] = load_face(choice->family, 1, 1);

    if (!priv->face[ACE_GFX_STYLE_REGULAR] || !priv->face[ACE_GFX_STYLE_BOLD] ||
        !priv->face[ACE_GFX_STYLE_ITALIC] || !priv->face[ACE_GFX_STYLE_BOLD_ITALIC]) {
        if (reason_out)
            *reason_out = "family is missing a regular, bold, italic or "
                         "bold-italic face";
        ace_gfx_unload_font(&priv->tf);
        return NULL;
    }

    strncpy(priv->family, choice->family, sizeof(priv->family) - 1);

    /*
     * Measured, not guessed: tf_XSize/tf_YSize/tf_Baseline are what
     * consoleclass.c reads to set the fixed character-cell pitch
     * (cu_XRSize/cu_YRSize) the whole console grid is built on, so they have
     * to be the real advance width and line metrics of the chosen face at
     * the chosen size, not a nominal point size.
     */
    probe_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    probe_cr = cairo_create(probe_surface);
    cairo_set_font_face(probe_cr, priv->face[ACE_GFX_STYLE_REGULAR]);
    cairo_set_font_size(probe_cr, choice->pixel_size);
    cairo_font_extents(probe_cr, &extents);
    cairo_text_extents(probe_cr, "M", &cell_extents);
    cairo_destroy(probe_cr);
    cairo_surface_destroy(probe_surface);

    priv->tf.tf_Message.mn_Node.ln_Type = 0; /* NT_FONT, unused downstream */
    priv->tf.tf_Message.mn_Node.ln_Name = priv->family;
    priv->tf.tf_YSize = (UWORD)(extents.height + 0.5);
    priv->tf.tf_Style = 0;
    priv->tf.tf_Flags = 0;
    priv->tf.tf_XSize = (UWORD)(cell_extents.x_advance + 0.5);
    priv->tf.tf_Baseline = (UWORD)(extents.ascent + 0.5);
    priv->tf.tf_BoldSmear = 0; /* real faces are used instead of smearing */
    priv->tf.tf_Accessors = 0;
    priv->tf.tf_LoChar = 32;
    priv->tf.tf_HiChar = 255;
    priv->tf.tf_CharData = NULL; /* unread by this codepath; see file header */
    priv->tf.tf_Modulo = 0;
    priv->tf.tf_CharLoc = NULL;
    priv->tf.tf_CharSpace = NULL;
    priv->tf.tf_CharKern = NULL;

    if (priv->tf.tf_XSize == 0 || priv->tf.tf_YSize == 0) {
        if (reason_out)
            *reason_out = "font measured to a zero-sized cell";
        ace_gfx_unload_font(&priv->tf);
        return NULL;
    }

    return &priv->tf;
}

void ace_gfx_unload_font(struct TextFont *font)
{
    struct ace_gfx_font_private *priv;
    int i;

    if (!font)
        return;
    priv = (struct ace_gfx_font_private *)font;
    for (i = 0; i < 4; i++) {
        if (priv->face[i])
            cairo_font_face_destroy(priv->face[i]);
    }
    free(priv);
}

static cairo_font_face_t *face_for_style(struct TextFont *font, UBYTE algo_style)
{
    struct ace_gfx_font_private *priv = (struct ace_gfx_font_private *)font;
    int bold = (algo_style & FSF_BOLD) != 0;
    int italic = (algo_style & FSF_ITALIC) != 0;

    if (bold && italic)
        return priv->face[ACE_GFX_STYLE_BOLD_ITALIC];
    if (bold)
        return priv->face[ACE_GFX_STYLE_BOLD];
    if (italic)
        return priv->face[ACE_GFX_STYLE_ITALIC];
    return priv->face[ACE_GFX_STYLE_REGULAR];
}

/* -------------------------------------------------------------------------
 * RastPort / BitMap
 *
 * Nothing in the console codepath ACE compiles against this seam reads
 * struct BitMap's Planes[] -- confirmed by checking every call site in
 * rom/devs/console before writing this file -- so the BitMap exists only for
 * structural completeness (rp->BitMap is expected to be non-NULL) and the
 * real pixel storage lives on the RastPort's private context instead.
 *
 * struct RastPort reserves exactly one field for this: RP_Extra, present in
 * the real struct for private per-instance extension space. That is what it
 * is used for here, rather than repurposing a field with real meaning.
 * ---------------------------------------------------------------------- */

struct ace_gfx_rp_private {
    cairo_surface_t *surface;
    cairo_t *cr;
    int width;
    int height;
    uint32_t palette[ACE_GFX_PEN_COUNT];
};

static void rgb_components(uint32_t rgb, double *r, double *g, double *b)
{
    *r = ((rgb >> 16) & 0xff) / 255.0;
    *g = ((rgb >> 8) & 0xff) / 255.0;
    *b = (rgb & 0xff) / 255.0;
}

static uint32_t pen_rgb(struct ace_gfx_rp_private *priv, ULONG pen)
{
    if (pen >= ACE_GFX_PEN_COUNT)
        return 0;
    return priv->palette[pen];
}

struct RastPort *ace_gfx_create_rastport(int width, int height,
                                         struct TextFont *font,
                                         const uint32_t rgb[ACE_GFX_PEN_COUNT])
{
    struct RastPort *rp;
    struct BitMap *bm;
    struct ace_gfx_rp_private *priv;
    int i;

    if (width <= 0 || height <= 0 || !font)
        return NULL;

    rp = calloc(1, sizeof(*rp));
    bm = calloc(1, sizeof(*bm));
    priv = calloc(1, sizeof(*priv));
    if (!rp || !bm || !priv) {
        free(rp);
        free(bm);
        free(priv);
        return NULL;
    }

    priv->surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    priv->cr = cairo_create(priv->surface);
    priv->width = width;
    priv->height = height;
    for (i = 0; i < ACE_GFX_PEN_COUNT; i++)
        priv->palette[i] = rgb ? rgb[i] : (i == 0 ? 0x000000u : 0xffffffu);

    bm->BytesPerRow = (UWORD)((width + 15) & ~15) / 8;
    bm->Rows = (UWORD)height;
    bm->Flags = 0;
    bm->Depth = 24;

    rp->BitMap = bm;
    rp->Font = font;
    rp->FgPen = 1;
    rp->BgPen = 0;
    rp->DrawMode = JAM2;
    rp->cp_x = 0;
    rp->cp_y = 0;
    rp->AlgoStyle = FS_NORMAL;
    rp->TxHeight = font->tf_YSize;
    rp->TxWidth = font->tf_XSize;
    rp->TxBaseline = font->tf_Baseline;
    rp->RP_Extra = priv;

    /* Establish the surface in the background pen, as a fresh screen is. */
    {
        double r, g, b;

        rgb_components(pen_rgb(priv, rp->BgPen), &r, &g, &b);
        cairo_set_source_rgb(priv->cr, r, g, b);
        cairo_paint(priv->cr);
    }

    return rp;
}

void ace_gfx_destroy_rastport(struct RastPort *rp)
{
    struct ace_gfx_rp_private *priv;

    if (!rp)
        return;
    priv = rp->RP_Extra;
    if (priv) {
        cairo_destroy(priv->cr);
        cairo_surface_destroy(priv->surface);
        free(priv);
    }
    free(rp->BitMap);
    free(rp);
}

void ace_gfx_set_pen_rgb(struct RastPort *rp, int pen, uint32_t rgb)
{
    struct ace_gfx_rp_private *priv;

    if (!rp || pen < 0 || pen >= ACE_GFX_PEN_COUNT)
        return;
    priv = rp->RP_Extra;
    priv->palette[pen] = rgb;
}

void ace_gfx_rastport_size(struct RastPort *rp, int *width_out, int *height_out)
{
    struct ace_gfx_rp_private *priv = rp ? rp->RP_Extra : NULL;

    if (width_out)
        *width_out = priv ? priv->width : 0;
    if (height_out)
        *height_out = priv ? priv->height : 0;
}

cairo_surface_t *ace_gfx_rastport_surface(struct RastPort *rp)
{
    struct ace_gfx_rp_private *priv = rp ? rp->RP_Extra : NULL;

    return priv ? priv->surface : NULL;
}

void ace_gfx_read_rgb(struct RastPort *rp, uint8_t *out, size_t out_capacity)
{
    struct ace_gfx_rp_private *priv = rp ? rp->RP_Extra : NULL;
    unsigned char *pixels;
    int stride;
    int x, y;
    size_t needed;

    if (!priv || !out)
        return;
    needed = (size_t)priv->width * (size_t)priv->height * 3;
    if (out_capacity < needed)
        return;

    cairo_surface_flush(priv->surface);
    pixels = cairo_image_surface_get_data(priv->surface);
    stride = cairo_image_surface_get_stride(priv->surface);

    for (y = 0; y < priv->height; y++) {
        uint32_t *row = (uint32_t *)(pixels + y * stride);

        for (x = 0; x < priv->width; x++) {
            uint32_t argb = row[x];
            uint8_t *dst = out + ((size_t)y * priv->width + x) * 3;

            dst[0] = (argb >> 16) & 0xff;
            dst[1] = (argb >> 8) & 0xff;
            dst[2] = argb & 0xff;
        }
    }
}

/* -------------------------------------------------------------------------
 * graphics.library
 *
 * Signatures and semantics below come from reading the real implementations
 * in rom/graphics, not from the AmigaOS documentation alone -- in
 * particular, ScrollRaster()'s vacated-strip fill color: stdconclass.c
 * always does SetAPen(rp, backgroundPen) immediately before calling
 * ScrollRaster(), which only makes sense if ScrollRaster() fills the
 * uncovered area with the current FgPen, so that is what this
 * implementation does.
 * ---------------------------------------------------------------------- */

void Move(struct RastPort *rp, WORD x, WORD y)
{
    if (!rp)
        return;
    rp->cp_x = x;
    rp->cp_y = y;
}

void SetAPen(struct RastPort *rp, ULONG pen)
{
    if (!rp)
        return;
    rp->FgPen = (BYTE)pen;
}

void SetBPen(struct RastPort *rp, ULONG pen)
{
    if (!rp)
        return;
    rp->BgPen = (BYTE)pen;
}

void SetDrMd(struct RastPort *rp, ULONG drawMode)
{
    if (!rp)
        return;
    rp->DrawMode = (BYTE)drawMode;
}

void SetABPenDrMd(struct RastPort *rp, ULONG apen, ULONG bpen, ULONG drawMode)
{
    if (!rp)
        return;
    rp->FgPen = (BYTE)apen;
    rp->BgPen = (BYTE)bpen;
    rp->DrawMode = (BYTE)drawMode;
}

ULONG SetSoftStyle(struct RastPort *rp, ULONG style, ULONG enable)
{
    ULONG old;

    if (!rp)
        return 0;
    old = rp->AlgoStyle;
    rp->AlgoStyle = (UBYTE)((rp->AlgoStyle & ~enable) | (style & enable));
    return old;
}

static void apply_complement(struct ace_gfx_rp_private *priv, int x0, int y0,
                             int x1, int y1)
{
    unsigned char *pixels;
    int stride;
    int x, y;

    cairo_surface_flush(priv->surface);
    pixels = cairo_image_surface_get_data(priv->surface);
    stride = cairo_image_surface_get_stride(priv->surface);

    for (y = y0; y <= y1; y++) {
        uint32_t *row = (uint32_t *)(pixels + y * stride);

        for (x = x0; x <= x1; x++)
            row[x] ^= 0x00ffffffu;
    }
    cairo_surface_mark_dirty(priv->surface);
}

void RectFill(struct RastPort *rp, WORD xMin, WORD yMin, WORD xMax, WORD yMax)
{
    struct ace_gfx_rp_private *priv;
    int x0, y0, x1, y1;

    if (!rp)
        return;
    priv = rp->RP_Extra;
    x0 = xMin < 0 ? 0 : xMin;
    y0 = yMin < 0 ? 0 : yMin;
    x1 = xMax >= priv->width ? priv->width - 1 : xMax;
    y1 = yMax >= priv->height ? priv->height - 1 : yMax;
    if (x0 > x1 || y0 > y1)
        return;

    if (rp->DrawMode == COMPLEMENT) {
        apply_complement(priv, x0, y0, x1, y1);
        return;
    }

    {
        double r, g, b;

        rgb_components(pen_rgb(priv, rp->FgPen), &r, &g, &b);
        cairo_set_source_rgb(priv->cr, r, g, b);
        cairo_rectangle(priv->cr, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        cairo_fill(priv->cr);
    }
}

void ScrollRaster(struct RastPort *rp, WORD dx, WORD dy, WORD xMin, WORD yMin,
                  WORD xMax, WORD yMax)
{
    struct ace_gfx_rp_private *priv;
    cairo_surface_t *snapshot;
    cairo_t *snapshot_cr;
    int x0, y0, x1, y1;
    int box_w, box_h;

    if (!rp)
        return;
    priv = rp->RP_Extra;
    x0 = xMin < 0 ? 0 : xMin;
    y0 = yMin < 0 ? 0 : yMin;
    x1 = xMax >= priv->width ? priv->width - 1 : xMax;
    y1 = yMax >= priv->height ? priv->height - 1 : yMax;
    if (x0 > x1 || y0 > y1)
        return;
    box_w = x1 - x0 + 1;
    box_h = y1 - y0 + 1;

    /*
     * The source and destination regions of the blit overlap whenever a
     * scroll moves content within the same box, which every caller in
     * stdconclass.c does. Cairo's own surface cannot be its own source and
     * destination for an overlapping copy, so the box is captured to a
     * snapshot first and blitted back shifted by (dx,dy).
     */
    snapshot = cairo_image_surface_create(CAIRO_FORMAT_RGB24, box_w, box_h);
    snapshot_cr = cairo_create(snapshot);
    cairo_set_source_surface(snapshot_cr, priv->surface, -x0, -y0);
    cairo_paint(snapshot_cr);
    cairo_destroy(snapshot_cr);

    {
        double r, g, b;

        rgb_components(pen_rgb(priv, rp->FgPen), &r, &g, &b);
        cairo_set_source_rgb(priv->cr, r, g, b);
        cairo_rectangle(priv->cr, x0, y0, box_w, box_h);
        cairo_fill(priv->cr);
    }

    cairo_save(priv->cr);
    cairo_rectangle(priv->cr, x0, y0, box_w, box_h);
    cairo_clip(priv->cr);
    cairo_set_source_surface(priv->cr, snapshot, x0 + dx, y0 + dy);
    cairo_paint(priv->cr);
    cairo_restore(priv->cr);

    cairo_surface_destroy(snapshot);
}

void Text(struct RastPort *rp, CONST_STRPTR string, ULONG count)
{
    struct ace_gfx_rp_private *priv;
    cairo_font_face_t *face;
    ULONG i;
    int pen_x = rp ? rp->cp_x : 0;
    int pen_y = rp ? rp->cp_y : 0;
    int cell_w = rp && rp->Font ? rp->Font->tf_XSize : 0;
    int cell_h = rp && rp->Font ? rp->Font->tf_YSize : 0;
    int baseline = rp && rp->Font ? rp->Font->tf_Baseline : 0;

    if (!rp || !string || !rp->Font || cell_w <= 0 || cell_h <= 0)
        return;
    priv = rp->RP_Extra;
    face = face_for_style(rp->Font, rp->AlgoStyle);

    for (i = 0; i < count; i++) {
        int x = pen_x + (int)i * cell_w;
        char glyph[2] = { (char)string[i], 0 };
        double fr, fg, fb;

        if (x + cell_w > priv->width)
            break;

        /*
         * Antialiased glyph rendering can paint a pixel or two past a
         * glyph's own advance width -- real font hinting/kerning, not a
         * bug -- which would otherwise bleed into a neighboring character
         * cell. A monospace terminal grid should never let that happen, so
         * every draw in this cell, glyph included, is clipped to it.
         */
        cairo_save(priv->cr);
        cairo_rectangle(priv->cr, x, pen_y - baseline, cell_w, cell_h);
        cairo_clip(priv->cr);

        /*
         * JAM2 paints the cell's background pen before the glyph, matching
         * an opaque terminal cell; JAM1 leaves whatever is already there,
         * matching real graphics.library's transparent text mode.
         */
        if (rp->DrawMode == JAM2 || rp->DrawMode == JAM1 + JAM2) {
            rgb_components(pen_rgb(priv, rp->BgPen), &fr, &fg, &fb);
            cairo_set_source_rgb(priv->cr, fr, fg, fb);
            cairo_paint(priv->cr);
        }

        rgb_components(pen_rgb(priv, rp->FgPen), &fr, &fg, &fb);
        cairo_set_source_rgb(priv->cr, fr, fg, fb);
        cairo_set_font_face(priv->cr, face);
        cairo_set_font_size(priv->cr, rp->Font->tf_YSize);
        cairo_move_to(priv->cr, x, pen_y);
        cairo_show_text(priv->cr, glyph);

        if (rp->AlgoStyle & FSF_UNDERLINED) {
            cairo_move_to(priv->cr, x, pen_y + 1.0);
            cairo_line_to(priv->cr, x + cell_w, pen_y + 1.0);
            cairo_set_line_width(priv->cr, 1.0);
            cairo_stroke(priv->cr);
        }

        cairo_restore(priv->cr);
    }

    rp->cp_x = (WORD)(pen_x + (int)count * cell_w);
}

/* -------------------------------------------------------------------------
 * Scratch raster (AllocRaster/FreeRaster/InitTmpRas)
 *
 * These back the character-cell cursor's use of rp->TmpRas in
 * stdconclass.c. Real graphics.library uses this scratch space when its own
 * Text() renders a proportional font into an off-screen buffer before
 * blitting it. ACE's Text() above draws straight to the surface and never
 * consults rp->TmpRas, so nothing here needs planar bitmap semantics -- only
 * to hand back a buffer of the size AROS's own RASSIZE() macro computes, and
 * not crash when stdconclass.c sets it up.
 * ---------------------------------------------------------------------- */

PLANEPTR AllocRaster(UWORD width, UWORD height)
{
    ULONG size = RASSIZE(width, height);

    return calloc(1, size ? size : 1);
}

void FreeRaster(PLANEPTR p, UWORD width, UWORD height)
{
    (void)width;
    (void)height;
    free(p);
}

struct TmpRas *InitTmpRas(struct TmpRas *tmpRas, PLANEPTR buffer, LONG size)
{
    if (!tmpRas)
        return NULL;
    tmpRas->RasPtr = buffer;
    tmpRas->Size = size;
    return tmpRas;
}

/* -------------------------------------------------------------------------
 * Intuition DrawInfo (GetScreenDrawInfo/FreeScreenDrawInfo)
 *
 * stdconclass.c's constructor reads dri_Pens[BACKGROUNDPEN] and
 * dri_Pens[TEXTPEN] to remap the console's initial pen 0/1 to whatever a
 * screen's color scheme prefers; a single screen's worth of DrawInfo is
 * this seam's whole Intuition surface, real Intuition Screen/Window
 * structs being used as-is otherwise.
 * ---------------------------------------------------------------------- */

struct DrawInfo *GetScreenDrawInfo(struct Screen *screen)
{
    struct DrawInfo *dri = calloc(1, sizeof(*dri));
    UWORD *pens;

    (void)screen;
    if (!dri)
        return NULL;
    pens = calloc(NUMDRIPENS, sizeof(UWORD));
    if (!pens) {
        free(dri);
        return NULL;
    }
    pens[BACKGROUNDPEN] = 0;
    pens[TEXTPEN] = 1;

    dri->dri_Version = 1;
    dri->dri_NumPens = NUMDRIPENS;
    dri->dri_Pens = pens;
    dri->dri_Font = NULL;
    dri->dri_Depth = 24;
    return dri;
}

void FreeScreenDrawInfo(struct Screen *screen, struct DrawInfo *drawInfo)
{
    (void)screen;
    if (!drawInfo)
        return;
    free(drawInfo->dri_Pens);
    free(drawInfo);
}

/* -------------------------------------------------------------------------
 * TaggedOpenLibrary/CloseLibrary
 *
 * TaggedOpenLibrary() is AROS's own config-generated shortcut for opening
 * one of a small set of system libraries by tag; the console classes only
 * ever ask for TAGGEDOPEN_GRAPHICS, so it hands back a nonzero token
 * standing in for this file's own presence rather than a real Library base.
 * ---------------------------------------------------------------------- */

static int graphics_library_open_count;

void *TaggedOpenLibrary(IPTR library)
{
    if (library != TAGGEDOPEN_GRAPHICS)
        return NULL;
    graphics_library_open_count++;
    return &graphics_library_open_count;
}

void CloseLibrary(void *library)
{
    if (library == &graphics_library_open_count && graphics_library_open_count > 0)
        graphics_library_open_count--;
}

/* -------------------------------------------------------------------------
 * Remaining small Exec/Utility calls
 * ---------------------------------------------------------------------- */

void SetMem(APTR destination, ULONG length, UBYTE value)
{
    memset(destination, value, length);
}

void CopyMem(CONST_APTR source, APTR destination, ULONG length)
{
    memmove(destination, source, length);
}

/*
 * rom/utility/gettagdata.c's real body is exactly this pass-through to
 * LibGetTagData(), which is itself exactly "found ? found->ti_Data :
 * defaultVal" over LibFindTagItem(). ACE compiles LibFindTagItem() and
 * LibNextTagItem() as real AROS source (compiler/arossupport) for the part
 * with actual logic -- the TAG_MORE/TAG_IGNORE/TAG_SKIP/TAG_END list walk --
 * and only this trivial combining step is hand-written, to avoid pulling in
 * rom/utility/intern.h for a one-line pass-through. See proto/utility.h.
 */
IPTR GetTagData(Tag tagValue, IPTR defaultVal, const struct TagItem *tagList)
{
    struct TagItem *found = LibFindTagItem(tagValue, tagList);

    return found ? found->ti_Data : defaultVal;
}

/* -------------------------------------------------------------------------
 * NewRawDoFmt (utility.library-shaped, %d/%u/%s/%c subset)
 *
 * A hand-written exception to "compile the real AROS source": the real
 * implementation (rom/exec/rawdofmt.c) depends on exec.library's private
 * exec_intern.h, a shadow far larger than ace_boopsi_intern.h's for a
 * function that never touches a pixel -- stdconclass.c uses it only to
 * format two VT100-style replies (cursor position report, window size
 * report), both %d substitution into a caller buffer via
 * RAWFMTFUNC_STRING. This covers exactly the conversions AROS's own console
 * sources use.
 * ---------------------------------------------------------------------- */

STRPTR NewRawDoFmt(CONST_STRPTR formatString, VOID_FUNC putChProc,
                   APTR putChData, ...)
{
    va_list args;
    char *out = (char *)putChData;
    const char *f = (const char *)formatString;

    (void)putChProc; /* RAWFMTFUNC_STRING is the only mode this seam needs */

    va_start(args, putChData);
    while (*f) {
        if (*f == '%') {
            f++;
            switch (*f) {
            case 'd': {
                int v = va_arg(args, int);
                unsigned int u = v < 0 ? (unsigned int)(-v) : (unsigned int)v;
                char digits[16];
                int n = 0;

                if (v < 0)
                    *out++ = '-';
                do {
                    digits[n++] = (char)('0' + (u % 10));
                    u /= 10;
                } while (u && n < (int)sizeof(digits));
                while (n > 0)
                    *out++ = digits[--n];
                f++;
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                char digits[16];
                int n = 0;

                do {
                    digits[n++] = (char)('0' + (u % 10));
                    u /= 10;
                } while (u && n < (int)sizeof(digits));
                while (n > 0)
                    *out++ = digits[--n];
                f++;
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);

                while (s && *s)
                    *out++ = *s++;
                f++;
                break;
            }
            case 'c':
                *out++ = (char)va_arg(args, int);
                f++;
                break;
            case '%':
                *out++ = '%';
                f++;
                break;
            default:
                *out++ = '%';
                break;
            }
        } else {
            *out++ = *f++;
        }
    }
    va_end(args);
    *out = '\0';
    return (STRPTR)putChData;
}
