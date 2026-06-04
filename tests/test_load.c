#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* Build: 1 SCREEN node, 240x320, no strings. */
static int build_blob(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 1;
    h.screen_w = 240; h.screen_h = 320;
    h.strtab_off = sizeof(htgl_header) + sizeof(htgl_node);
    h.reserved = 0;
    htgl_node n;
    memset(&n, 0, sizeof(n));
    n.type = HTGL_TYPE_SCREEN;
    n.parent = HTGL_ROOT_PARENT;
    n.w = 240; n.h = 320;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), &n, sizeof(n));
    return (int)(sizeof(h) + sizeof(n));
}

int main(void) {
    uint8_t blob[128];
    int len = build_blob(blob);

    htgl_ctx ctx;
    uint16_t lb[240];
    CHECK(htgl_init(&ctx, 0, lb, 240) == &ctx);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(htgl_screen_w(&ctx) == 240);
    CHECK(htgl_screen_h(&ctx) == 320);

    /* bad magic rejected */
    uint8_t bad[128];
    memcpy(bad, blob, len);
    bad[0] = 'X';
    CHECK(htgl_load(&ctx, bad, len) != 0);

    /* truncated rejected */
    CHECK(htgl_load(&ctx, blob, 4) != 0);

    printf("ok\n");
    return 0;
}
