#include <stdlib.h>
#include "hal_png.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static int g_w, g_h;
static uint8_t *g_rgb;   /* RGB888, g_w*g_h*3 */

static void rgb565_to_888(uint16_t c, uint8_t *out) {
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5) & 0x3F;
    uint8_t b5 = c & 0x1F;
    out[0] = (uint8_t)((r5 * 255 + 15) / 31);
    out[1] = (uint8_t)((g6 * 255 + 31) / 63);
    out[2] = (uint8_t)((b5 * 255 + 15) / 31);
}

static void png_flush(int x, int y, int w, int h, const uint16_t *buf) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col, py = y + row;
            if (px < 0 || py < 0 || px >= g_w || py >= g_h) continue;
            rgb565_to_888(buf[row * w + col], &g_rgb[(py * g_w + px) * 3]);
        }
    }
}

void hal_png_begin(int w, int h) {
    g_w = w; g_h = h;
    g_rgb = (uint8_t *)calloc((size_t)w * h * 3, 1);
}

htgl_hal hal_png_get(void) {
    htgl_hal hal;
    hal.flush = png_flush;
    return hal;
}

int hal_png_write(const char *path) {
    if (!g_rgb) return -1;
    return stbi_write_png(path, g_w, g_h, 3, g_rgb, g_w * 3) ? 0 : -2;
}

void hal_png_end(void) {
    free(g_rgb);
    g_rgb = 0;
}
