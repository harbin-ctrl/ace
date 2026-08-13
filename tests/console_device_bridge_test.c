/* Checks that the live console bridge can repaint its retained AROS stream
 * after palette, font, and window-size changes, that scrolling moves what is
 * on screen and clears what it uncovers, and that the bridge reports the
 * region it drew into. */

#include "console_device_bridge.h"
#include "aros_graphics_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static int band_has_ink(const uint8_t *frame, int width, int y0, int y1,
                        uint32_t background)
{
    int x;
    int y;

    for (y = y0; y <= y1; y++) {
        for (x = 0; x < width; x++) {
            if (frame_pixel(frame, width, x, y) != background)
                return 1;
        }
    }
    return 0;
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

    /* Shrinking keeps the pixels already in place; growing back past the
     * original size exercises the reallocating path as well as the in-place
     * one. */
    width = 320;
    height = 200;
    assert(ace_console_device_resize(device, width, height) == 0);
    frame = read_frame(device, width, height);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    width = 900;
    height = 560;
    assert(ace_console_device_resize(device, width, height) == 0);
    frame = read_frame(device, width, height);
    /* The area the console just gained is background, not stale pixels. */
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    assert(frame_pixel(frame, width, width - 2, 2) == 0x123456u);
    free(frame);

    assert(ace_console_device_set_font(device, "Liberation Mono", 20) == 0 ||
           ace_console_device_set_font(device, "DejaVu Sans Mono", 20) == 0);
    frame = read_frame(device, width, height);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    ace_console_device_close(device);
    return 0;
}
