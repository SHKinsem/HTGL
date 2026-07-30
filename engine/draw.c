#include "htgl_internal.h"
#include "font8x8_basic.h"

uint16_t htgl_blend_rgb565(uint16_t dst, uint16_t src, uint8_t alpha) {
    if (alpha == 0) return dst;
    if (alpha == 255) return src;
    /* Q0.8 alpha: RGB565 channels are only 5/6 bits, so 24.8 would not add
       useful precision. 255 is reserved as exact opaque above; all other
       values divide by 256 with a rounded right shift, not an expensive /255. */
    unsigned inv = 256u - alpha;
    unsigned r = (((src >> 11) & 0x1Fu) * alpha + ((dst >> 11) & 0x1Fu) * inv + 128u) >> 8;
    unsigned g = (((src >> 5)  & 0x3Fu) * alpha + ((dst >> 5)  & 0x3Fu) * inv + 128u) >> 8;
    unsigned b = (( src        & 0x1Fu) * alpha + ( dst        & 0x1Fu) * inv + 128u) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void htgl_fill_rect_alpha(uint16_t *band, int band_w, int band_y0, int band_h,
                          int rx, int ry, int rw, int rh, uint16_t color,
                          uint8_t alpha) {
    if (alpha == 0) return;
    int x0 = rx, x1 = rx + rw;
    int y0 = ry, y1 = ry + rh;
    if (x0 < 0) x0 = 0;
    if (y0 < band_y0) y0 = band_y0;
    if (x1 > band_w) x1 = band_w;
    if (y1 > band_y0 + band_h) y1 = band_y0 + band_h;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = band + (y - band_y0) * band_w;
        if (alpha == 255) {
            for (int x = x0; x < x1; x++) row[x] = color;
        } else {
            for (int x = x0; x < x1; x++) row[x] = htgl_blend_rgb565(row[x], color, alpha);
        }
    }
}

/* The ST7789 target is 240px wide. Keeping these three source rows on the
   task stack makes glass blur deterministic without a framebuffer or PSRAM
   scratch buffer. Wider generic ports transparently fall back to alpha-only. */
#define HTGL_GLASS_MAX_BAND_WIDTH 320

static int rounded_contains(int x, int y, int rx, int ry, int rw, int rh, int radius) {
    if (radius <= 0) return 1;
    if (rw <= 0 || rh <= 0 || x < rx || y < ry || x >= rx + rw || y >= ry + rh) return 0;
    int limit = rw < rh ? rw / 2 : rh / 2;
    if (radius > limit) radius = limit;
    int lx = x - rx, ly = y - ry;
    if ((lx >= radius && lx < rw - radius) || (ly >= radius && ly < rh - radius)) return 1;
    int cx = lx < radius ? radius - 1 : rw - radius;
    int cy = ly < radius ? radius - 1 : rh - radius;
    int dx = lx - cx, dy = ly - cy;
    return dx * dx + dy * dy <= radius * radius;
}

static uint16_t blur5_rgb565(uint16_t center, uint16_t left, uint16_t right,
                             uint16_t above, uint16_t below) {
    unsigned r = ((center >> 11) + (left >> 11) + (right >> 11) +
                  (above >> 11) + (below >> 11) + 2u) / 5u;
    unsigned g = (((center >> 5) & 0x3Fu) + ((left >> 5) & 0x3Fu) +
                  ((right >> 5) & 0x3Fu) + ((above >> 5) & 0x3Fu) +
                  ((below >> 5) & 0x3Fu) + 2u) / 5u;
    unsigned b = ((center & 0x1Fu) + (left & 0x1Fu) + (right & 0x1Fu) +
                  (above & 0x1Fu) + (below & 0x1Fu) + 2u) / 5u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void copy_glass_source_row(uint16_t *dst, const uint16_t *band, int band_w,
                                  int band_y0, int band_h, int y) {
    if (y < band_y0) y = band_y0;
    if (y >= band_y0 + band_h) y = band_y0 + band_h - 1;
    const uint16_t *src = band + (y - band_y0) * band_w;
    for (int x = 0; x < band_w; x++) dst[x] = src[x];
}

void htgl_fill_rounded_glass_rect(uint16_t *band, int band_w, int band_y0, int band_h,
                                  int rx, int ry, int rw, int rh, uint16_t color,
                                  uint8_t alpha, uint8_t radius, uint8_t backdrop_blur) {
    if (alpha == 0 || rw <= 0 || rh <= 0) return;
    if (radius == 0 && backdrop_blur == 0) {
        htgl_fill_rect_alpha(band, band_w, band_y0, band_h, rx, ry, rw, rh, color, alpha);
        return;
    }
    int x0 = rx < 0 ? 0 : rx;
    int x1 = rx + rw > band_w ? band_w : rx + rw;
    int y0 = ry < band_y0 ? band_y0 : ry;
    int y1 = ry + rh > band_y0 + band_h ? band_y0 + band_h : ry + rh;
    if (x0 >= x1 || y0 >= y1) return;

    /* Opaque fills have no visible backdrop, and hosts wider than the small
       band scratch still get precise rounded alpha coverage. */
    if (alpha == 255 || backdrop_blur == 0 || band_w > HTGL_GLASS_MAX_BAND_WIDTH) {
        for (int y = y0; y < y1; y++) {
            uint16_t *row = band + (y - band_y0) * band_w;
            for (int x = x0; x < x1; x++) {
                if (!rounded_contains(x, y, rx, ry, rw, rh, radius)) continue;
                row[x] = alpha == 255 ? color : htgl_blend_rgb565(row[x], color, alpha);
            }
        }
        return;
    }

    uint16_t source_rows[3][HTGL_GLASS_MAX_BAND_WIDTH];
    uint16_t *previous = source_rows[0];
    uint16_t *current = source_rows[1];
    uint16_t *next = source_rows[2];
    copy_glass_source_row(previous, band, band_w, band_y0, band_h, y0 - 1);
    copy_glass_source_row(current, band, band_w, band_y0, band_h, y0);
    copy_glass_source_row(next, band, band_w, band_y0, band_h, y0 + 1);
    uint8_t blur_alpha = (uint8_t)(backdrop_blur >= 8 ? 255 : backdrop_blur * 32u);

    for (int y = y0; y < y1; y++) {
        uint16_t *row = band + (y - band_y0) * band_w;
        for (int x = x0; x < x1; x++) {
            if (!rounded_contains(x, y, rx, ry, rw, rh, radius)) continue;
            int left = x > 0 ? x - 1 : x;
            int right = x + 1 < band_w ? x + 1 : x;
            uint16_t blurred = blur5_rgb565(current[x], current[left], current[right],
                                             previous[x], next[x]);
            uint16_t backdrop = htgl_blend_rgb565(current[x], blurred, blur_alpha);
            row[x] = htgl_blend_rgb565(backdrop, color, alpha);
        }
        uint16_t *recycled = previous;
        previous = current;
        current = next;
        next = recycled;
        copy_glass_source_row(next, band, band_w, band_y0, band_h, y + 2);
    }
}

void htgl_fill_rect(uint16_t *band, int band_w, int band_y0, int band_h,
                    int rx, int ry, int rw, int rh, uint16_t color) {
    htgl_fill_rect_alpha(band, band_w, band_y0, band_h, rx, ry, rw, rh, color, 255);
}

static void draw_glyph(uint16_t *band, int band_w, int band_y0, int band_h,
                       int gx, int gy, unsigned char ch, int scale,
                       uint16_t color, uint8_t alpha) {
    if (ch >= 128) ch = '?';
    const char *bits = font8x8_basic[ch];
    for (int row = 0; row < 8; row++) {
        unsigned char rb = (unsigned char)bits[row];
        for (int col = 0; col < 8; col++) {
            if (!(rb & (1u << col))) continue;        /* LSB = leftmost */
            int px = gx + col * scale;
            int py = gy + row * scale;
            htgl_fill_rect_alpha(band, band_w, band_y0, band_h,
                                 px, py, scale, scale, color, alpha);
        }
    }
}

void htgl_draw_text(uint16_t *band, int band_w, int band_y0, int band_h,
                    int tx, int ty, const char *text, int len,
                    int scale, uint16_t color) {
    htgl_draw_text_alpha(band, band_w, band_y0, band_h, tx, ty, text, len, scale, color, 255);
}

void htgl_draw_text_alpha(uint16_t *band, int band_w, int band_y0, int band_h,
                          int tx, int ty, const char *text, int len,
                          int scale, uint16_t color, uint8_t alpha) {
    int advance = 8 * scale;
    int cx = tx, cy = ty;
    for (int i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\n') {            /* line break: carriage-return + one line down */
            cx = tx;
            cy += advance;
            continue;
        }
        draw_glyph(band, band_w, band_y0, band_h,
                   cx, cy, (unsigned char)c, scale, color, alpha);
        cx += advance;
    }
}
