#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

#define W 16
#define H 16
static uint16_t fb[W * H];

static void capture_flush(int x, int y, int w, int h, const uint16_t *buf) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            fb[(y + row) * W + (x + col)] = buf[row * w + col];
}

static int build(uint8_t *out) {
    htgl_header hd;
    memcpy(hd.magic, "HTGL", 4);
    hd.version = 1; hd.flags = 0; hd.node_count = 2;
    hd.screen_w = W; hd.screen_h = H;
    hd.strtab_off = sizeof(htgl_header) + 2 * sizeof(htgl_node);
    hd.anim_count = 0;
    htgl_node n[2];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = W; n[0].h = H; n[0].bg = 0x0001;          /* screen bg */
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].x = 4; n[1].y = 4; n[1].w = 8; n[1].h = 8; n[1].bg = 0xF800; /* red */
    memcpy(out, &hd, sizeof(hd));
    memcpy(out + sizeof(hd), n, sizeof(n));
    return (int)(sizeof(hd) + sizeof(n));
}

int main(void) {
    uint8_t blob[256];
    int len = build(blob);
    htgl_hal hal = { capture_flush };
    htgl_ctx ctx;
    uint16_t line_buf[W * 4];          /* 4-row bands */
    memset(fb, 0xEE, sizeof(fb));
    htgl_init(&ctx, &hal, line_buf, W * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    htgl_render(&ctx);

    CHECK(fb[0 * W + 0] == 0x0001);     /* corner = screen bg */
    CHECK(fb[5 * W + 5] == 0xF800);     /* inside red box */
    CHECK(fb[4 * W + 4] == 0xF800);     /* box top-left */
    CHECK(fb[12 * W + 12] == 0x0001);   /* just past box = screen bg */

    /* single-band: line buffer holds the whole screen, so the band loop runs once. */
    {
        uint16_t big[W * H];
        htgl_ctx c2;
        memset(fb, 0xEE, sizeof(fb));
        htgl_init(&c2, &hal, big, W * H);
        CHECK(htgl_load(&c2, blob, len) == 0);
        htgl_layout(&c2);
        htgl_render(&c2);
        CHECK(fb[0] == 0x0001);
        CHECK(fb[5 * W + 5] == 0xF800);
        CHECK(fb[(H - 1) * W + (W - 1)] == 0x0001);  /* last pixel reached in one band */
    }

    /* partial last band: screen_h (14) not divisible by band_h (4) -> final band = 2 rows. */
    {
        uint8_t b2[256];
        int l2 = build(b2);
        ((htgl_header *)b2)->screen_h = 14;          /* 14 = 4 + 4 + 4 + 2 */
        htgl_ctx c3;
        memset(fb, 0xEE, sizeof(fb));
        htgl_init(&c3, &hal, line_buf, W * 4);
        CHECK(htgl_load(&c3, b2, l2) == 0);
        htgl_layout(&c3);
        htgl_render(&c3);
        CHECK(fb[5 * W + 5] == 0xF800);              /* box still drawn */
        CHECK(fb[13 * W + 0] == 0x0001);             /* row 13: in the 2-row final band */
        CHECK(fb[14 * W + 0] == 0xEEEE);             /* row 14 never flushed (screen_h=14) */
    }

    printf("ok\n");
    return 0;
}
