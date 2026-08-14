#include "console_device_bridge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* One ace-console process owns one console unit.  This sink keeps the
   imported console classes independent of GTK and the socket transport. */
static int console_input_fd = -1;

/* Set while the retained stream is being re-rendered.  A repaint is ACE's
   substitute for the per-cell character map charmapconclass would have kept,
   so it puts output the program has already produced back through the
   console a second time.  Rendering that again is the point; answering it
   again is not, and the difference only shows up on the input side. */
static int console_replaying;

static void inject_input(const void *data, size_t length)
{
    const unsigned char *bytes = data;

    while (length != 0 && console_input_fd >= 0) {
        ssize_t written = write(console_input_fd, bytes, length);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (written == 0)
            break;
        bytes += written;
        length -= (size_t)written;
    }
}

/*
 * con_inject() is how stdconclass.c answers a DSR cursor-position-report
 * request (ESC[6n) by injecting the reply back into the console's input
 * queue -- real behavior, but implemented at ACE's shell socket boundary
 * because the rendering path deliberately does not run console.c's task and
 * message-port machinery.
 *
 * A replay is silent. The program asked its question once and was answered
 * once; the query bytes are still in the retained stream only because that
 * stream is how ACE redraws, and re-answering would put a reply the program
 * never asked for into its input -- in the middle, as it happens, of the
 * reply it is waiting for, since a repaint is exactly what a resize does.
 */
VOID con_inject(struct ConsoleBase *console_device, struct ConUnit *unit,
                const UBYTE *data, LONG size)
{
    (void)console_device;
    (void)unit;
    if (!data || console_replaying)
        return;
    if (size < 0)
        size = (LONG)strlen((const char *)data);
    if (size > 0)
        inject_input(data, (size_t)size);
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
    /* The grid the last SIZEWINDOW report described, so a drag that crosses
       many pixel steps inside one character cell reports once. */
    int reported_xmax;
    int reported_ymax;
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

/*
 * A console has to be at least one character cell in each direction.
 * consoleclass.c computes cu_XMax/cu_YMax as (pixels / cell) - 1, so a
 * console narrower or shorter than a single cell gets -1 -- a grid with no
 * columns or no rows -- and the class chain then spins forever trying to
 * place a character in it. Confirmed by attaching to a hung process: the
 * stack sits inside AROS's own dispatch_consoleclass(), not in ACE's code.
 * Every path that sizes a console goes through here.
 */
static void clamp_to_cell(const struct TextFont *font, int *width, int *height)
{
    if (!font)
        return;
    if (*width < (int)font->tf_XSize)
        *width = font->tf_XSize;
    if (*height < (int)font->tf_YSize)
        *height = font->tf_YSize;
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
    clamp_to_cell(device->font, &width, &height);

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

/*
 * The retained stream only exists to repaint the console after a font or
 * palette change, so it needs to hold what is still on screen, not the whole
 * session. Left unbounded it made those two operations cost time
 * proportional to how long the shell had been running -- the console had to
 * re-render, and re-scroll past, every line ever written.
 *
 * What is kept is sized from the console's own grid -- several screenfuls of
 * text, so a repaint reproduces every visible line and a good deal of what
 * preceded it -- with a floor for tiny windows and a ceiling so a very large
 * one cannot make the repaint slow again. Older bytes are dropped from the
 * front at a line boundary: a partial escape sequence at the cut would
 * otherwise be replayed as stray text.
 */
#define ACE_CONSOLE_HISTORY_SCREENS 8
#define ACE_CONSOLE_HISTORY_MIN (32u * 1024u)
#define ACE_CONSOLE_HISTORY_MAX (256u * 1024u)

/*
 * A repaint only has to reproduce what ends up visible, so it replays a few
 * screenfuls of the tail rather than everything retained. More than one
 * screenful, because a stream does not divide into screens evenly -- long
 * lines wrap, escape sequences occupy bytes but no cells -- and the extra
 * scrolls off the top exactly as it did the first time round.
 */
#define ACE_CONSOLE_REPLAY_SCREENS 3

/* Bytes of stream it takes to fill the console once. Zero if unknown. */
static size_t console_screenful(struct ace_console_device *device)
{
    size_t columns;
    size_t rows;

    if (!device->font || device->font->tf_XSize == 0 ||
        device->font->tf_YSize == 0)
        return 0;
    columns = (size_t)device->amiga_window.Width / device->font->tf_XSize;
    rows = (size_t)device->amiga_window.Height / device->font->tf_YSize;
    /* One newline per line on top of the cells themselves. */
    return rows * (columns + 1);
}

static size_t history_limit(struct ace_console_device *device)
{
    size_t screenful = console_screenful(device);
    size_t limit;

    if (screenful == 0)
        return ACE_CONSOLE_HISTORY_MIN;
    limit = ACE_CONSOLE_HISTORY_SCREENS * screenful;
    if (limit < ACE_CONSOLE_HISTORY_MIN)
        limit = ACE_CONSOLE_HISTORY_MIN;
    if (limit > ACE_CONSOLE_HISTORY_MAX)
        limit = ACE_CONSOLE_HISTORY_MAX;
    return limit;
}

static void trim_history(struct ace_console_device *device)
{
    size_t limit = history_limit(device);
    size_t cut;

    if (device->history_length <= limit)
        return;

    cut = device->history_length - limit;
    while (cut < device->history_length && device->history[cut] != '\n')
        cut++;
    if (cut < device->history_length)
        cut++; /* start just after the newline, not on it */
    if (cut >= device->history_length) {
        device->history_length = 0;
        return;
    }
    memmove(device->history, device->history + cut,
            device->history_length - cut);
    device->history_length -= cut;
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
    trim_history(device);
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

/*
 * Where in the retained stream a repaint should start: far enough back to
 * fill the console several times over, cut at a line boundary so a partial
 * escape sequence is never replayed as stray text. Everything older than
 * that would only scroll off the top again, and re-rendering it is the whole
 * cost of a repaint.
 */
static size_t replay_start(struct ace_console_device *device)
{
    size_t screenful = console_screenful(device);
    size_t want;
    size_t cut;

    if (screenful == 0)
        return 0;
    want = ACE_CONSOLE_REPLAY_SCREENS * screenful;
    if (device->history_length <= want)
        return 0;

    cut = device->history_length - want;
    while (cut < device->history_length && device->history[cut] != '\n')
        cut++;
    if (cut < device->history_length)
        cut++; /* start just after the newline, not on it */
    /* No line boundary in the tail: replaying all of it is still correct. */
    return cut < device->history_length ? cut : 0;
}

static void replay_history(struct ace_console_device *device, Object *unit)
{
    size_t start = replay_start(device);

    console_replaying++;
    if (device->history_length > start)
        write_direct(device, unit, device->history + start,
                     device->history_length - start);
    console_replaying--;
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
    clamp_to_cell(font, &width, &height);
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
    struct ConUnit *unit;
    WORD old_xmax;
    WORD old_ymax;

    if (!device || width <= 0 || height <= 0)
        return -1;
    /* Clamped before the early-out, so a window dragged below one cell
     * settles on the clamped size instead of retrying on every step. */
    clamp_to_cell(device->font, &width, &height);
    if (width == device->amiga_window.Width &&
        height == device->amiga_window.Height)
        return 0;
    if (ace_gfx_resize_rastport(device->rp, width, height) != 0)
        return -1;
    device->amiga_window.Width = (UWORD)width;
    device->amiga_window.Height = (UWORD)height;

    /*
     * A resize that leaves the character grid the same size -- anything
     * smaller than one cell, which is most of the steps a drag delivers --
     * changes nothing about the layout. The font is unchanged, so every cell
     * keeps its pixel position, and the pixels already on screen stay valid
     * across the geometry update.
     *
     * A resize that does change the grid has to repaint, because the text on
     * screen was laid out against the old column count and the old bottom
     * row. Repainting the retained stream is what ACE has instead of the
     * character map AROS's own charmapconclass would have used -- the same
     * mechanism a typeface or palette change goes through -- and it is what
     * re-wraps the text to the new width and puts the last line back on the
     * last row.
     *
     * Shrinking would need this in any case: console_newwindowsize() clamps
     * the cursor into the new grid, and stdcon_newwindowsize() responds to a
     * cursor that moved by clearing the whole console and redrawing the
     * cursor -- a character-cell renderer with no retained character map has
     * no way to tidy up the cursor it left at the old position without
     * wiping what it cannot redraw. Clamping only ever goes downwards, so
     * enlarging never triggers that clear; it needs the repaint for the
     * layout alone, which is why the test here is the grid rather than the
     * cursor.
     *
     * The console is cleared and homed first, with a real form feed, because
     * the replay has to start from the top left, wherever AROS has left the
     * cursor.
     */
    unit = (struct ConUnit *)device->unit;
    old_xmax = unit->cu_XMax;
    old_ymax = unit->cu_YMax;
    Console_NewWindowSize(device->unit);
    if ((unit->cu_XMax != old_xmax || unit->cu_YMax != old_ymax) &&
        device->history_length != 0) {
        static const unsigned char form_feed = 0x0c;

        write_direct(device, device->unit, &form_feed, sizeof(form_feed));
        replay_history(device, device->unit);
    }
    return 0;
}

void ace_console_device_set_input_fd(struct ace_console_device *device, int fd)
{
    (void)device;
    console_input_fd = fd;
}

void ace_console_device_notify_resize(struct ace_console_device *device)
{
    struct ConUnit *unit;
    char report[128];
    int length;

    if (!device || console_input_fd < 0 || !device->unit)
        return;
    unit = (struct ConUnit *)device->unit;
    if (!CHECK_RAWEVENT((Object *)unit, IECLASS_SIZEWINDOW))
        return;

    /*
     * A program answers this report by asking the console for its bounds,
     * and it reads that answer with a plain read of whatever arrives next.
     * So a second report sent before the program has asked does not tell it
     * anything new -- it lands in the answer's place and is read as one, and
     * a size report is not a valid answer to a bounds request.
     *
     * Reporting once per character grid is what keeps that from happening
     * during a drag, which delivers a resize every frame: the pixel steps in
     * between change nothing a console program can act on, since a console
     * program is laid out in cells.
     */
    if (unit->cu_XMax == device->reported_xmax &&
        unit->cu_YMax == device->reported_ymax)
        return;
    device->reported_xmax = unit->cu_XMax;
    device->reported_ymax = unit->cu_YMax;

    /* Match consoleTask's report_raw_event() format: event class, subclass,
       code, qualifier, pixel X/Y, seconds, and microseconds. */
    length = snprintf(report, sizeof(report),
                      "\233%u;0;0;0;%u;%u;0;0|",
                      (unsigned)IECLASS_SIZEWINDOW,
                      (unsigned)device->amiga_window.Width,
                      (unsigned)device->amiga_window.Height);
    if (length > 0 && (size_t)length < sizeof(report))
        inject_input(report, (size_t)length);
}

cairo_surface_t *ace_console_device_surface(struct ace_console_device *device)
{
    return device ? ace_gfx_rastport_surface(device->rp) : NULL;
}

int ace_console_device_origin_y(struct ace_console_device *device)
{
    return device ? ace_gfx_rastport_origin_y(device->rp) : 0;
}

void ace_console_device_size(struct ace_console_device *device,
                             int *width_out, int *height_out)
{
    if (width_out)
        *width_out = device ? device->amiga_window.Width : 0;
    if (height_out)
        *height_out = device ? device->amiga_window.Height : 0;
}

int ace_console_device_take_damage(struct ace_console_device *device,
                                   int *x_out, int *y_out,
                                   int *width_out, int *height_out)
{
    if (!device)
        return 0;
    return ace_gfx_take_damage(device->rp, x_out, y_out, width_out,
                               height_out);
}
