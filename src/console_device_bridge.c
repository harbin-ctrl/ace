#include "console_device_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include <devices/conunit.h>
#include <graphics/rastport.h>

#include "consoleif.h"
#include "console_gcc.h"

#include "aros_boopsi_runtime.h"
#include "aros_graphics_runtime.h"

/*
 * con_inject() is how stdconclass.c answers a DSR cursor-position-report
 * request (ESC[6n) by injecting the reply back into the console's input
 * queue -- real behavior, but implemented in console.c's command-port I/O
 * layer, which is deliberately not part of this seam (see HANDOFF.md: ACE's
 * rendering path never goes through DoIO()/BeginIO(), and console.c's own
 * task/message-port machinery is not needed for rendering). A program that
 * sends a cursor-position-report request will not receive a reply; every
 * other escape sequence is unaffected. Documented as a known gap rather
 * than crashing, since a real program legitimately can trigger this path.
 */
VOID con_inject(struct ConsoleBase *console_device, struct ConUnit *unit,
                const UBYTE *data, LONG size)
{
    (void)console_device;
    (void)unit;
    (void)data;
    (void)size;
}

struct ace_console_device {
    struct ConsoleBase console_base;
    Class *console_class;
    Class *std_class;
    struct Window amiga_window;
    struct TextFont *font;
    struct RastPort *rp;
    Object *unit;
    uint32_t palette[ACE_GFX_PEN_COUNT];
    unsigned char *history;
    size_t history_length;
    size_t history_capacity;
    int history_valid;
};

static const uint32_t default_palette[ACE_GFX_PEN_COUNT] = {
    0x000000u, 0xffffffu, 0xff5555u, 0x55ff55u,
    0x5555ffu, 0xffff55u, 0xff55ffu, 0x55ffffu,
};

static struct TextFont *load_font(const char *const *candidates, int pixel_size)
{
    struct ace_gfx_font_choice choice;
    struct TextFont *font;
    const char *reason = NULL;
    int i;

    choice.pixel_size = pixel_size;
    for (i = 0; candidates && candidates[i]; i++) {
        choice.family = candidates[i];
        font = ace_gfx_load_font(&choice, &reason);
        if (font)
            return font;
    }
    fprintf(stderr, "ace-console: no complete monospace font family found\n");
    return NULL;
}

struct ace_console_device *ace_console_device_open(
    int width, int height, const char *const *font_candidates,
    int pixel_size)
{
    struct ace_console_device *device;
    struct TagItem tags[] = {
        { A_Console_Window, 0 },
        { TAG_DONE, 0 },
    };

    device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;
    device->history_valid = 1;

    if (ace_boopsi_init() != 0) {
        fprintf(stderr, "ace_console_device_open: ace_boopsi_init failed\n");
        goto fail;
    }

    /*
     * makeStdConClass()'s real body subclasses CONSOLECLASSPTR, a macro for
     * ConsoleDevice->consoleClass (console_gcc.h) -- it has to be set before
     * makeStdConClass() runs, not just before it returns, or MakeClass()
     * gets called with a NULL superclass and fails.
     */
    device->console_class = makeConsoleClass(&device->console_base);
    if (!device->console_class) {
        fprintf(stderr, "ace_console_device_open: consoleClass creation failed\n");
        goto fail;
    }
    device->console_base.consoleClass = device->console_class;

    device->std_class = makeStdConClass(&device->console_base);
    if (!device->std_class) {
        fprintf(stderr, "ace_console_device_open: stdConClass creation failed\n");
        goto fail;
    }
    device->console_base.stdConClass = device->std_class;

    device->font = load_font(font_candidates, pixel_size);
    if (!device->font) {
        fprintf(stderr, "ace_console_device_open: font load failed\n");
        goto fail;
    }

    device->rp = ace_gfx_create_rastport(width, height, device->font,
                                         default_palette);
    if (!device->rp) {
        fprintf(stderr, "ace_console_device_open: rastport creation failed\n");
        goto fail;
    }
    memcpy(device->palette, default_palette, sizeof(device->palette));

    device->amiga_window.RPort = device->rp;
    device->amiga_window.Width = (UWORD)width;
    device->amiga_window.Height = (UWORD)height;
    device->amiga_window.Flags = WFLG_WINDOWACTIVE;

    tags[0].ti_Data = (IPTR)&device->amiga_window;
    device->unit = NewObjectA(device->std_class, NULL, tags);
    if (!device->unit) {
        fprintf(stderr, "ace_console_device_open: NewObjectA failed\n");
        goto fail;
    }

    return device;

fail:
    ace_console_device_close(device);
    return NULL;
}

void ace_console_device_close(struct ace_console_device *device)
{
    if (!device)
        return;
    if (device->unit)
        DisposeObject(device->unit);
    if (device->rp)
        ace_gfx_destroy_rastport(device->rp);
    if (device->font)
        ace_gfx_unload_font(device->font);
    free(device->history);
    free(device);
    ace_boopsi_cleanup();
}

static int save_history(struct ace_console_device *device,
                        const void *data, size_t length)
{
    size_t needed;
    size_t capacity;
    unsigned char *history;

    if (length == 0)
        return 0;
    if (!device->history_valid ||
        length > (size_t)-1 - device->history_length) {
        device->history_valid = 0;
        return -1;
    }
    needed = device->history_length + length;
    if (needed <= device->history_capacity) {
        memcpy(device->history + device->history_length, data, length);
        device->history_length = needed;
        return 0;
    }

    capacity = device->history_capacity ? device->history_capacity : 4096;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    history = realloc(device->history, capacity);
    if (!history) {
        device->history_valid = 0;
        return -1;
    }
    device->history = history;
    device->history_capacity = capacity;
    memcpy(device->history + device->history_length, data, length);
    device->history_length = needed;
    return 0;
}

static void write_direct(struct ace_console_device *device, Object *unit,
                         const void *data, size_t length)
{
    const unsigned char *bytes = data;

    while (length != 0) {
        ULONG chunk = length > 65536 ? 65536 : (ULONG)length;

        writeToConsole((struct ConUnit *)unit, (STRPTR)bytes, chunk,
                       &device->console_base);
        bytes += chunk;
        length -= chunk;
    }
}

static void replay_history(struct ace_console_device *device, Object *unit)
{
    if (device->history_length != 0)
        write_direct(device, unit, device->history, device->history_length);
}

/*
 * Build a new RastPort/unit pair, replay the AROS console stream into it,
 * then retire the old pair.  This is ACE's equivalent of charmapconclass's
 * retained per-cell lines plus charmapcon_refresh(): stdconclass itself only
 * paints the live RastPort and cannot repaint after its font, palette, or
 * dimensions change.
 */
static int replace_render_state(struct ace_console_device *device,
                                int width, int height,
                                struct TextFont *font,
                                const uint32_t palette[ACE_GFX_PEN_COUNT])
{
    struct RastPort *rp;
    struct RastPort *old_rp;
    Object *unit;
    Object *old_unit;
    UWORD old_width;
    UWORD old_height;
    struct TagItem tags[] = {
        { A_Console_Window, 0 },
        { TAG_DONE, 0 },
    };

    if (!device || !font || !device->history_valid)
        return -1;
    if (width < (int)font->tf_XSize)
        width = font->tf_XSize;
    if (height < (int)font->tf_YSize)
        height = font->tf_YSize;
    if (width > 65535)
        width = 65535;
    if (height > 65535)
        height = 65535;

    rp = ace_gfx_create_rastport(width, height, font, palette);
    if (!rp)
        return -1;

    old_rp = device->rp;
    old_unit = device->unit;
    old_width = device->amiga_window.Width;
    old_height = device->amiga_window.Height;
    device->amiga_window.RPort = rp;
    device->amiga_window.Width = (UWORD)width;
    device->amiga_window.Height = (UWORD)height;
    tags[0].ti_Data = (IPTR)&device->amiga_window;
    unit = NewObjectA(device->std_class, NULL, tags);
    if (!unit) {
        device->amiga_window.RPort = old_rp;
        device->amiga_window.Width = old_width;
        device->amiga_window.Height = old_height;
        ace_gfx_destroy_rastport(rp);
        return -1;
    }

    device->rp = rp;
    device->unit = unit;
    /* The constructor has the new dimensions already, but issue the same
     * notification that AROS's console task sends after a real window resize.
     * This keeps the class-chain geometry update explicit for both resize and
     * font/palette rebuilds. */
    Console_NewWindowSize(unit);
    replay_history(device, unit);

    if (old_unit)
        DisposeObject(old_unit);
    ace_gfx_destroy_rastport(old_rp);
    return 0;
}

void ace_console_device_write(struct ace_console_device *device,
                              const void *data, size_t length)
{
    if (!device || !data || length == 0)
        return;
    (void)save_history(device, data, length);
    write_direct(device, device->unit, data, length);
}

int ace_console_device_set_font(struct ace_console_device *device,
                                const char *family, int pixel_size)
{
    struct ace_gfx_font_choice choice;
    struct TextFont *font;
    struct TextFont *old_font;

    if (!device || !family || pixel_size <= 0)
        return -1;
    choice.family = family;
    choice.pixel_size = pixel_size;
    font = ace_gfx_load_font(&choice, NULL);
    if (!font)
        return -1;
    if (replace_render_state(device, device->amiga_window.Width,
                             device->amiga_window.Height, font,
                             device->palette) != 0) {
        ace_gfx_unload_font(font);
        return -1;
    }
    ace_gfx_unload_font(device->font);
    device->font = font;
    return 0;
}

int ace_console_device_set_palette(
    struct ace_console_device *device,
    const uint32_t rgb[ACE_CONSOLE_PEN_COUNT])
{
    if (!device || !rgb)
        return -1;
    if (replace_render_state(device, device->amiga_window.Width,
                             device->amiga_window.Height, device->font,
                             rgb) != 0)
        return -1;
    memcpy(device->palette, rgb, sizeof(device->palette));
    return 0;
}

void ace_console_device_set_pen_rgb(struct ace_console_device *device,
                                    int pen, uint32_t rgb)
{
    uint32_t palette[ACE_GFX_PEN_COUNT];

    if (!device || pen < 0 || pen >= ACE_GFX_PEN_COUNT)
        return;
    memcpy(palette, device->palette, sizeof(palette));
    palette[pen] = rgb;
    (void)ace_console_device_set_palette(device, palette);
}

int ace_console_device_resize(struct ace_console_device *device,
                              int width, int height)
{
    if (!device || width <= 0 || height <= 0)
        return -1;
    if (width == device->amiga_window.Width &&
        height == device->amiga_window.Height)
        return 0;
    return replace_render_state(device, width, height, device->font,
                                device->palette);
}

cairo_surface_t *ace_console_device_surface(struct ace_console_device *device)
{
    return device ? ace_gfx_rastport_surface(device->rp) : NULL;
}
