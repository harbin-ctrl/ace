/* Checks that the live console bridge can repaint its retained AROS stream
 * after palette, font, and window-size changes. */

#include "console_device_bridge.h"
#include "aros_graphics_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *const font_candidates[] = {
    "Liberation Mono", "DejaVu Sans Mono", "monospace", NULL
};

static uint8_t *read_frame(struct ace_console_device *device,
                           int *width_out, int *height_out)
{
    cairo_surface_t *surface = ace_console_device_surface(device);
    uint8_t *frame;
    int width;
    int height;

    assert(surface != NULL);
    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    frame = malloc((size_t)width * height * 3);
    assert(frame != NULL);
    /* The bridge exposes the AROS RastPort as a Cairo image surface. Read its
     * pixels directly so this test also verifies the replacement surface. */
    cairo_surface_flush(surface);
    {
        unsigned char *pixels = cairo_image_surface_get_data(surface);
        int stride = cairo_image_surface_get_stride(surface);
        int x, y;

        for (y = 0; y < height; y++) {
            uint32_t *row = (uint32_t *)(pixels + y * stride);

            for (x = 0; x < width; x++) {
                uint32_t pixel = row[x];
                uint8_t *out = frame + ((size_t)y * width + x) * 3;

                out[0] = (pixel >> 16) & 0xff;
                out[1] = (pixel >> 8) & 0xff;
                out[2] = pixel & 0xff;
            }
        }
    }
    if (width_out)
        *width_out = width;
    if (height_out)
        *height_out = height;
    return frame;
}

static uint32_t frame_pixel(const uint8_t *frame, int width, int x, int y)
{
    const uint8_t *pixel = frame + ((size_t)y * width + x) * 3;

    return ((uint32_t)pixel[0] << 16) | ((uint32_t)pixel[1] << 8) | pixel[2];
}

static int frame_has_ink(const uint8_t *frame, int width, int height,
                         uint32_t background)
{
    int x;
    int y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            if (frame_pixel(frame, width, x, y) != background)
                return 1;
        }
    }
    return 0;
}

int main(void)
{
    struct ace_console_device *device;
    uint32_t palette[ACE_CONSOLE_PEN_COUNT] = {
        0x101010u, 0xf0f0f0u, 0xff0000u, 0x00ff00u,
        0x0000ffu, 0xffff00u, 0xff00ffu, 0x00ffffu,
    };
    uint8_t *frame;
    int width;
    int height;

    device = ace_console_device_open(640, 400, font_candidates, 16);
    assert(device != NULL);
    ace_console_device_write(device, "retained screen\n", 16);

    frame = read_frame(device, &width, &height);
    assert(width == 640 && height == 400);
    assert(frame_has_ink(frame, width, height, 0x000000u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x000000u);
    free(frame);

    palette[0] = 0x123456u;
    assert(ace_console_device_set_palette(device, palette) == 0);
    frame = read_frame(device, &width, &height);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    assert(ace_console_device_resize(device, 320, 200) == 0);
    frame = read_frame(device, &width, &height);
    assert(width == 320 && height == 200);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    assert(ace_console_device_set_font(device, "Liberation Mono", 20) == 0 ||
           ace_console_device_set_font(device, "DejaVu Sans Mono", 20) == 0);
    frame = read_frame(device, &width, &height);
    assert(width == 320 && height == 200);
    assert(frame_has_ink(frame, width, height, 0x123456u));
    assert(frame_pixel(frame, width, width - 2, height - 2) == 0x123456u);
    free(frame);

    ace_console_device_close(device);
    return 0;
}
