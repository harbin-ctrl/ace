/* Checks that the live console bridge can repaint its retained AROS stream
 * after palette, font, and window-size changes, that scrolling moves what is
 * on screen and clears what it uncovers, and that the bridge reports the
 * region it drew into. */

#include "console_device_bridge.h"
#include "aros_graphics_runtime.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const font_candidates[] = {
    "Liberation Mono", "DejaVu Sans Mono", "monospace", NULL
};

/*
 * The bridge's surface is deliberately larger than the console -- it carries
 * growth slack and the scroll headroom that lets a scroll move a viewing
 * origin instead of copying pixels -- so the console's own rows start at
 * ace_console_device_origin_y() and run for the window's height, not the
 * surface's.
 */
static uint8_t *read_frame(struct ace_console_device *device,
                           int width, int height)
{
    cairo_surface_t *surface = ace_console_device_surface(device);
    int origin_y = ace_console_device_origin_y(device);
    uint8_t *frame;

    assert(surface != NULL);
    assert(cairo_image_surface_get_width(surface) >= width);
    assert(cairo_image_surface_get_height(surface) >= origin_y + height);
    frame = malloc((size_t)width * height * 3);
    assert(frame != NULL);
    cairo_surface_flush(surface);
    {
        unsigned char *pixels = cairo_image_surface_get_data(surface);
        int stride = cairo_image_surface_get_stride(surface);
        int x, y;

        for (y = 0; y < height; y++) {
            uint32_t *row = (uint32_t *)(pixels + (origin_y + y) * stride);

            for (x = 0; x < width; x++) {
                uint32_t pixel = row[x];
                uint8_t *out = frame + ((size_t)y * width + x) * 3;

                out[0] = (pixel >> 16) & 0xff;
                out[1] = (pixel >> 8) & 0xff;
                out[2] = pixel & 0xff;
            }
        }
    }
    return frame;
}

static uint32_t frame_pixel(const uint8_t *frame, int width, int x, int y)
{
    const uint8_t *pixel = frame + ((size_t)y * width + x) * 3;

    return ((uint32_t)pixel[0] << 16) | ((uint32_t)pixel[1] << 8) | pixel[2];
}

static int region_has_ink(const uint8_t *frame, int width, int x0, int y0,
                          int x1, int y1, uint32_t background)
{
    int x;
    int y;

    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            if (frame_pixel(frame, width, x, y) != background)
                return 1;
        }
    }
    return 0;
}

static int band_has_ink(const uint8_t *frame, int width, int y0, int y1,
                        uint32_t background)
{
    return region_has_ink(frame, width, 0, y0, width - 1, y1, background);
}

static int frame_has_ink(const uint8_t *frame, int width, int height,
                         uint32_t background)
{
    return band_has_ink(frame, width, 0, height - 1, background);
}

static void write_text(struct ace_console_device *device, const char *text)
{
    ace_console_device_write(device, text, strlen(text));
}

int main(void)
{
    struct ace_console_device *device;
    uint32_t palette[ACE_CONSOLE_PEN_COUNT] = {
        0x101010u, 0xf0f0f0u, 0xff0000u, 0x00ff00u,
        0x0000ffu, 0xffff00u, 0xff00ffu, 0x00ffffu,
    };
    uint8_t *frame;
    int width = 640;
    int height = 400;
    int x, y, w, h;
    int i;

    device = ace_console_device_open(width, height, font_candidates, 16);
    assert(device != NULL);

    /* A fresh console has been painted, so it has damage to report; taking it
     * consumes it. */
    assert(ace_console_device_take_damage(device, &x, &y, &w, &h) == 1);
    assert(ace_console_device_take_damage(device, &x, &y, &w, &h) == 0);

    ace_console_device_write(device, "retained screen\n", 16);
    assert(ace_console_device_take_damage(device, &x, &y, &w, &h) == 1);
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x + w <= width && y + h <= height);

    frame = read_frame(device, width, height);
    assert(frame_has_ink(frame, width, height, 0x000000u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x000000u);
    free(frame);

    /*
     * Scroll far past the surface's scroll headroom, so the viewing origin
     * both advances and is folded back to the top of the allocation at least
     * once, then blank the console by scrolling that same content off the
     * top. Anything the scroll failed to move or failed to clear shows up as
     * ink left behind in the upper half of the console.
     */
    for (i = 0; i < 600; i++)
        write_text(device, "################################\n");
    frame = read_frame(device, width, height);
    assert(band_has_ink(frame, width, 0, height / 2, 0x000000u));
    free(frame);

    for (i = 0; i < 600; i++)
        write_text(device, "\n");
    frame = read_frame(device, width, height);
    assert(!band_has_ink(frame, width, 0, height / 2, 0x000000u));
    free(frame);

    write_text(device, "after the scroll\n");
    frame = read_frame(device, width, height);
    assert(frame_has_ink(frame, width, height, 0x000000u));
    free(frame);

    palette[0] = 0x123456u;
    assert(ace_console_device_set_palette(device, palette) == 0);
    frame = read_frame(device, width, height);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    /*
     * Shrinking keeps what is on screen. This is the case that needs
     * checking rather than assuming: real AROS clears the whole console
     * when a resize has to clamp the cursor into the new grid, which only a
     * shrink can do, and the bridge repaints the retained stream in
     * response. Ink is looked for in the upper half deliberately -- the
     * cursor sits on the bottom row and is itself non-background, so a
     * whole-frame ink check passes even on a console that has been wiped.
     */
    for (i = 0; i < 40; i++)
        write_text(device, "shrink me ##########\n");
    width = 320;
    height = 200;
    assert(ace_console_device_resize(device, width, height) == 0);
    frame = read_frame(device, width, height);
    assert(band_has_ink(frame, width, 0, height / 2, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    /* Shrinking again, by less than one character cell, must not lose it
     * either. */
    height = 196;
    assert(ace_console_device_resize(device, width, height) == 0);
    frame = read_frame(device, width, height);
    assert(band_has_ink(frame, width, 0, height / 2, 0x123456u));
    free(frame);

    /*
     * Growing re-wraps the retained text against the wider grid instead of
     * leaving it laid out for the narrow one. Lines long enough to wrap at
     * 320 pixels, but not at 900, are written first, so the columns the
     * console gains must end up with text in them -- "the text stays
     * squashed into the top left corner" is exactly the absence of ink out
     * there.
     */
    for (i = 0; i < 40; i++)
        write_text(device,
                   "a line long enough that it has to wrap in a narrow "
                   "console but not in a wide one\n");
    width = 900;
    height = 560;
    assert(ace_console_device_resize(device, width, height) == 0);
    frame = read_frame(device, width, height);
    assert(region_has_ink(frame, width, 500, 0, width - 1, height / 2,
                          0x123456u));
    free(frame);

    /*
     * A console smaller than one character cell must not be built. AROS
     * computes cu_XMax/cu_YMax as (pixels / cell) - 1, so a sub-cell console
     * has a grid with no columns or no rows, and the class chain spins
     * forever on the next character written into it -- a hang inside AROS's
     * own code, with the window frozen. Writing after each of these is the
     * point of the check: the resize alone is harmless.
     */
    assert(ace_console_device_resize(device, 1, 1) == 0);
    write_text(device, "into a one-pixel console\n");
    assert(ace_console_device_resize(device, 5, 400) == 0);
    write_text(device, "into a sliver of a console\n");
    assert(ace_console_device_resize(device, 400, 3) == 0);
    write_text(device, "into a slot of a console\n");
    {
        int clamped_width;
        int clamped_height;

        ace_console_device_size(device, &clamped_width, &clamped_height);
        assert(clamped_width >= 1 && clamped_height >= 1);
    }
    width = 900;
    height = 560;
    assert(ace_console_device_resize(device, width, height) == 0);

    assert(ace_console_device_set_font(device, "Liberation Mono", 20) == 0 ||
           ace_console_device_set_font(device, "DejaVu Sans Mono", 20) == 0);
    frame = read_frame(device, width, height);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    /* The real AROS stdconclass answers Vim's unchanged __AROS__ size query
     * by injecting the response into the console input stream.  The same
     * stream carries raw SIZEWINDOW reports after Vim enables event 12. */
    {
        int input_pipe[2];
        char reply[128] = {0};
        ssize_t length;

        assert(pipe(input_pipe) == 0);
        /* So "nothing else was injected" can be asserted, not just waited
         * on. Every injection below is synchronous with the call that
         * causes it, so anything pending is readable by the time it is. */
        assert(fcntl(input_pipe[0], F_SETFL, O_NONBLOCK) == 0);
        ace_console_device_set_input_fd(device, input_pipe[1]);
        ace_console_device_write(device, "\2330 q", 4);
        length = read(input_pipe[0], reply, sizeof(reply) - 1);
        assert(length > 0);
        reply[length] = '\0';
        /* The exact cell metrics come from the selected font, so verify the
         * response protocol here; the rendering assertions above already
         * exercise the public pixel-to-cell geometry path. */
        assert(reply[0] == '\233');
        assert(strstr(reply, "\2331;1;") == reply);
        assert(strstr(reply, " r") != NULL);

        ace_console_device_write(device, "\033[12{", 5);
        assert(ace_console_device_resize(device, width - 20, height - 20) == 0);
        ace_console_device_notify_resize(device);
        memset(reply, 0, sizeof(reply));
        length = read(input_pipe[0], reply, sizeof(reply) - 1);
        assert(length > 0);
        reply[length] = '\0';
        assert(reply[0] == '\233');
        assert(strstr(reply, ";0;0;0;") != NULL);
        assert(reply[length - 1] == '|');
        /*
         * The raw event has to be the whole of it. Changing the grid
         * repaints, and a repaint re-renders the retained stream -- which
         * still holds the size query above. Rendering that again is the
         * point of keeping the stream; answering it again is not. The
         * duplicate reply would arrive unasked, and land in the place of the
         * reply to the query this very report provokes, where a program
         * reading its console bounds would read a size report instead and
         * conclude it is not on a console at all.
         */
        assert(strncmp(reply, "\23312;", 4) == 0);
        assert(strstr(reply, " r") == NULL);

        /* One report per character grid. The pixel steps a drag delivers in
         * between change nothing a program laid out in cells can act on, and
         * a second report would be read as the answer to the first. */
        ace_console_device_notify_resize(device);
        assert(read(input_pipe[0], reply, sizeof(reply) - 1) < 0);
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        assert(ace_console_device_resize(device, width, height) == 0);
        ace_console_device_notify_resize(device);
        memset(reply, 0, sizeof(reply));
        length = read(input_pipe[0], reply, sizeof(reply) - 1);
        assert(length > 0);
        reply[length] = '\0';
        assert(strncmp(reply, "\23312;", 4) == 0);
        assert(reply[length - 1] == '|');

        ace_console_device_set_input_fd(device, -1);
        close(input_pipe[0]);
        close(input_pipe[1]);
    }

    ace_console_device_close(device);
    return 0;
}
