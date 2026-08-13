#ifndef ACE_CONSOLE_DEVICE_BRIDGE_H
#define ACE_CONSOLE_DEVICE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

/*
 * Opaque bridge between the GTK-facing amiga_console.c and the real AROS
 * console.device rendering state (ConsoleBase, the consoleClass/stdConClass
 * pair, a RastPort-backed struct Window, the ConUnit those classes build).
 *
 * This split exists because AROS's headers and GTK/glib's cannot be
 * included in the same translation unit: both define struct timeval
 * (aros/types/timeval_s.h vs. the host's own), and console_gcc.h's MAX/MIN
 * macros collide with glib's. src/aros_graphics_runtime.h already avoids
 * this by forward-declaring RastPort/BitMap/TextFont rather than including
 * graphics/rastport.h; this header does the same for the console.device
 * types amiga_console.c needs, and console_device_bridge.c is where the
 * real AROS headers and the actual writeToConsole()/NewObjectA() calls
 * live.
 */

struct ace_console_device;

#define ACE_CONSOLE_PEN_COUNT 8

/*
 * Builds the real console.device rendering state over a width x height
 * RastPort: the class pair (see src/aros_boopsi_runtime.c,
 * src/aros_graphics_runtime.c), a font (candidates tried in order, first
 * complete monospace family wins), and a ConUnit constructed the way
 * console.c's real Open() would, via A_Console_Window.
 *
 * font_candidates is a NULL-terminated array of fontconfig family names.
 */
struct ace_console_device *ace_console_device_open(
    int width, int height, const char *const *font_candidates,
    int pixel_size);
void ace_console_device_close(struct ace_console_device *device);

/*
 * The real entry point console.c's beginio()/CMD_WRITE would call for
 * CMD_WRITE, invoked directly since ACE's rendering path never goes through
 * DoIO()/BeginIO() -- see HANDOFF.md.
 */
void ace_console_device_write(struct ace_console_device *device,
                              const void *data, size_t length);

/* Rebuilds the console unit around a new complete monospace font and replays
   the saved console stream into the new cell grid. */
int ace_console_device_set_font(struct ace_console_device *device,
                                const char *family, int pixel_size);

/* Recolors the current console by rebuilding the RastPort and replaying the
   saved stream with the new eight-pen palette. */
int ace_console_device_set_palette(
    struct ace_console_device *device,
    const uint32_t rgb[ACE_CONSOLE_PEN_COUNT]);

/* Changes one pen and immediately repaints the current console. */
void ace_console_device_set_pen_rgb(struct ace_console_device *device,
                                    int pen, uint32_t rgb);

/* Updates the AROS Window dimensions, sends M_Console_NewWindowSize through
   the real class chain by rebuilding the unit, and repaints the saved stream. */
int ace_console_device_resize(struct ace_console_device *device,
                              int width, int height);

/*
 * The RastPort's pixel surface, for the caller to blit directly. Owned by
 * the device; do not destroy it, and do not hold it past close().
 *
 * The surface is deliberately taller than the console window so that
 * scrolling can move a viewing origin rather than copy every pixel, so a
 * caller must take the console's top row from ace_console_device_origin_y()
 * rather than from row zero of the surface.
 */
cairo_surface_t *ace_console_device_surface(struct ace_console_device *device);
int ace_console_device_origin_y(struct ace_console_device *device);

/* The console's own pixel extent, which is smaller than the surface's. */
void ace_console_device_size(struct ace_console_device *device,
                             int *width_out, int *height_out);

/*
 * Bounding box of everything the console has drawn since the last call, so
 * the window only has to repaint what changed. Returns 0 when nothing has
 * been drawn.
 */
int ace_console_device_take_damage(struct ace_console_device *device,
                                   int *x_out, int *y_out,
                                   int *width_out, int *height_out);

#endif
