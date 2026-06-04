#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

#define W 32
#define H 16
static uint16_t fb[W * H];
static void cap(int x, int y, int w, int h, const uint16_t *buf) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            fb[(y + r) * W + (x + c)] = buf[r * w + c];
}

static int build(uint8_t *out) {
    htgl_header hd; memcpy(hd.magic, "HTGL", 4);
    hd.version = 1; hd.flags = 0; hd.node_count = 2;
    hd.screen_w = W; hd.screen_h = H;
    hd.strtab_off = sizeof(htgl_header) + 2 * sizeof(htgl_node);
    hd.reserved = 0;
    htgl_node n[2]; memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = W; n[0].h = H; n[0].bg = 0x0000;
    n[1].type = HTGL_TYPE_TEXT; n[1].parent = 0; n[1].x = 0; n[1].y = 0;
    n[1].font = 2;                       /* scale 2x */
    n[1].fg = 0xFFFF; n[1].text_ref = 0; /* offset 0 in strtab */
    /* strtab: len=1, 'I' */
    uint8_t *st = out + hd.strtab_off;
    st[0] = 1; st[1] = 'I';
    memcpy(out, &hd, sizeof(hd));
    memcpy(out + sizeof(hd), n, sizeof(n));
    return (int)(hd.strtab_off + 2);
}

int main(void) {
    uint8_t blob[256];
    int len = build(blob);
    htgl_hal hal = { cap };
    htgl_ctx ctx; uint16_t lb[W * 4];
    memset(fb, 0, sizeof(fb));
    htgl_init(&ctx, &hal, lb, W * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    htgl_render(&ctx);
    /* 'I' in font8x8 has set pixels; with scale 2 the glyph spans ~16px tall.
       At least one white pixel must exist below row 8 (proving scaling). */
    int found_low = 0;
    for (int y = 8; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (fb[y * W + x] == 0xFFFF) found_low = 1;
    CHECK(found_low == 1);
    printf("ok\n");
    return 0;
}
