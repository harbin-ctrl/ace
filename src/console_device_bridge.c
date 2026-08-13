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
    static const uint32_t default_palette[ACE_GFX_PEN_COUNT] = {
        0x000000u, 0xffffffu, 0xff5555u, 0x55ff55u,
        0x5555ffu, 0xffff55u, 0xff55ffu, 0x55ffffu,
    };

    device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;

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
    free(device);
    ace_boopsi_cleanup();
}

void ace_console_device_write(struct ace_console_device *device,
                              const void *data, size_t length)
{
    if (!device)
        return;
    writeToConsole((struct ConUnit *)device->unit, (STRPTR)data,
                   (ULONG)length, &device->console_base);
}

cairo_surface_t *ace_console_device_surface(struct ace_console_device *device)
{
    return device ? ace_gfx_rastport_surface(device->rp) : NULL;
}
