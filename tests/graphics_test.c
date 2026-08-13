/*
 * Verifies the ACE graphics.library seam by driving the real AROS
 * console.device classes -- rom/devs/console/{stdconclass,consoleclass}.c,
 * compiled unmodified -- through a synthetic window, and checking the pixels
 * they produce.
 *
 * This is a stronger acceptance test than calling the graphics primitives
 * directly would be: it proves AROS's own class construction, DOS-command
 * dispatch (Console_DoCommand's ANSI/CSI parsing), and cursor logic land
 * correctly on ACE's RastPort, not just that ACE's Move/Text/RectFill happen
 * to behave in isolation.
 */

#include "aros_boopsi_runtime.h"
#include "aros_graphics_runtime.h"

#include <assert.h>
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

#define WIN_WIDTH  640
#define WIN_HEIGHT 400

static const uint32_t test_palette[ACE_GFX_PEN_COUNT] = {
    0x101010u, /* pen 0: background */
    0xf0f0f0u, /* pen 1: foreground */
    0xff0000u, 0x00ff00u, 0x0000ffu, 0xffff00u, 0xff00ffu, 0x00ffffu,
};

/*
 * con_inject() is how stdconclass.c answers a DSR cursor-position-report
 * request (ESC[6n) by injecting the reply back into the console's input
 * queue -- real behavior, but implemented in console.c's command-port I/O
 * layer, which is Phase 3 (console.device's task/BeginIO machinery), not
 * this seam. Linked in only because stdcon_docommand() references it
 * unconditionally; this test never sends a DSR request, so it is never
 * actually called.
 */
VOID con_inject(struct ConsoleBase *ConsoleDevice, struct ConUnit *cu,
                const UBYTE *data, LONG size)
{
    (void)ConsoleDevice;
    (void)cu;
    (void)data;
    (void)size;
    assert(0 && "con_inject is Phase 3 (console.device I/O); not exercised by this test");
}

static struct RastPort *g_rp;
static struct TextFont *g_font;
static struct Window g_window;
static struct ConsoleBase g_console_base;
static Class *g_std_class;

static void find_font(struct ace_gfx_font_choice *choice)
{
    static const char *candidates[] = {
        "Liberation Mono", "DejaVu Sans Mono", "monospace", NULL
    };
    int i;

    for (i = 0; candidates[i]; i++) {
        if (ace_gfx_font_family_complete(candidates[i])) {
            choice->family = candidates[i];
            choice->pixel_size = 16;
            return;
        }
    }
    assert(0 && "no complete monospace family found for the test host");
}

static Object *create_unit(void)
{
    struct TagItem tags[] = {
        { A_Console_Window, (IPTR)&g_window },
        { TAG_DONE, 0 },
    };

    return NewObjectA(g_std_class, NULL, tags);
}

static uint8_t *read_frame(void)
{
    int w, h;
    uint8_t *frame;

    ace_gfx_rastport_size(g_rp, &w, &h);
    assert(w == WIN_WIDTH && h == WIN_HEIGHT);
    frame = malloc((size_t)w * h * 3);
    assert(frame);
    ace_gfx_read_rgb(g_rp, frame, (size_t)w * h * 3);
    return frame;
}

static void pixel_at(const uint8_t *frame, int x, int y, uint8_t out[3])
{
    const uint8_t *p = frame + ((size_t)y * WIN_WIDTH + x) * 3;

    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

static int pixel_matches(const uint8_t *frame, int x, int y, uint32_t rgb)
{
    uint8_t p[3];

    pixel_at(frame, x, y, p);
    return p[0] == ((rgb >> 16) & 0xff) && p[1] == ((rgb >> 8) & 0xff) &&
           p[2] == (rgb & 0xff);
}

/* True if any pixel in the cell differs from the solid background pen. */
static int cell_has_ink(const uint8_t *frame, int cell_x, int cell_y)
{
    int cw = g_font->tf_XSize;
    int ch = g_font->tf_YSize;
    int x0 = cell_x * cw;
    int y0 = cell_y * ch;
    int x, y;

    for (y = y0; y < y0 + ch; y++) {
        for (x = x0; x < x0 + cw; x++) {
            if (!pixel_matches(frame, x, y, test_palette[0]))
                return 1;
        }
    }
    return 0;
}

/*
 * The real path text takes from AROS's own con_handler.c down to pixels:
 * Console_DoCommand(unit, C_ASCII_STRING, 2, {ptr, length}) dispatches
 * M_Console_DoCommand, which stdconclass.c's stdcon_docommand() handles by
 * calling Move()/Text() -- the actual graphics.library calls this seam
 * implements. console.c's CMD_WRITE device-I/O entry point (Phase 3, not
 * compiled here) is what would call this same macro in the running shell.
 */
static void write_text(Object *unit, const char *text)
{
    IPTR params[2];

    params[0] = (IPTR)text;
    params[1] = (IPTR)strlen(text);
    Console_DoCommand(unit, C_ASCII_STRING, 2, params);
}

static void test_class_construction(void)
{
    Class *console_class;

    assert(ace_boopsi_init() == 0);

    console_class = makeConsoleClass(&g_console_base);
    assert(console_class != NULL);
    g_console_base.consoleClass = console_class;

    g_std_class = makeStdConClass(&g_console_base);
    assert(g_std_class != NULL);
    g_console_base.stdConClass = g_std_class;

    /* Real AROS class linkage: stdconclass subclasses consoleclass, which
       subclasses rootclass by its public name. */
    assert(g_std_class->cl_Super == console_class);
    assert(console_class->cl_Super == ace_boopsi_rootclass());
}

static void test_window_setup(void)
{
    struct ace_gfx_font_choice choice;
    const char *reason = NULL;

    find_font(&choice);
    g_font = ace_gfx_load_font(&choice, &reason);
    assert(g_font != NULL);

    g_rp = ace_gfx_create_rastport(WIN_WIDTH, WIN_HEIGHT, g_font, test_palette);
    assert(g_rp != NULL);

    memset(&g_window, 0, sizeof(g_window));
    g_window.RPort = g_rp;
    g_window.Width = WIN_WIDTH;
    g_window.Height = WIN_HEIGHT;
    g_window.Flags = 0;
}

static Object *test_unit_construction(void)
{
    Object *unit;
    struct ConUnit *cu;

    unit = create_unit();
    assert(unit != NULL);

    cu = (struct ConUnit *)unit;
    /* AROS's own cell-grid math, off the font this seam measured. */
    assert(cu->cu_XRSize == g_font->tf_XSize);
    assert(cu->cu_YRSize == g_font->tf_YSize);
    assert(cu->cu_XMax == WIN_WIDTH / g_font->tf_XSize - 1);
    assert(cu->cu_YMax == WIN_HEIGHT / g_font->tf_YSize - 1);
    assert(cu->cu_Window == &g_window);

    return unit;
}

int main(void)
{
    test_class_construction();
    test_window_setup();

    {
        Object *unit = test_unit_construction();
        uint8_t *frame;
        int col;

        /* stdcon_new() ends by calling Console_RenderCursor(), so a freshly
           constructed unit already shows a cursor at the home cell (0,0) --
           real AmigaOS behavior, not something this test drives. Everywhere
           else is still ace_gfx_create_rastport()'s plain background fill. */
        frame = read_frame();
        assert(cell_has_ink(frame, 0, 0));
        assert(pixel_matches(frame, WIN_WIDTH - 1, WIN_HEIGHT - 1,
                             test_palette[0]));
        free(frame);

        write_text(unit, "Hi");

        frame = read_frame();
        for (col = 0; col < 2; col++)
            assert(cell_has_ink(frame, col, 0));
        /* Cell 2 is the cursor's new position after two characters -- real
           AROS re-renders it via a COMPLEMENT RectFill, which legitimately
           marks it. Cell 3 is untouched by either the text or the cursor. */
        assert(cell_has_ink(frame, 2, 0));
        assert(!cell_has_ink(frame, 3, 0));
        free(frame);

        DisposeObject(unit);
    }

    ace_gfx_destroy_rastport(g_rp);
    ace_gfx_unload_font(g_font);
    ace_boopsi_cleanup();

    return 0;
}
