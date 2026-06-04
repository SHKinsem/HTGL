#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* Internal accessors for the test. */
int16_t htgl_test_abs_x(htgl_ctx *c, int i);
int16_t htgl_test_abs_y(htgl_ctx *c, int i);

static int build(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0; h.node_count = 3;
    h.screen_w = 200; h.screen_h = 200;
    h.strtab_off = sizeof(htgl_header) + 3 * sizeof(htgl_node);
    h.reserved = 0;
    htgl_node n[3];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT; n[0].w = 200; n[0].h = 200;
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0; n[1].x = 10; n[1].y = 20; n[1].w = 50; n[1].h = 50;
    n[2].type = HTGL_TYPE_BOX; n[2].parent = 1; n[2].x = 5;  n[2].y = 7;  n[2].w = 10; n[2].h = 10;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    return (int)(sizeof(h) + sizeof(n));
}

int main(void) {
    uint8_t blob[256];
    int len = build(blob);
    htgl_ctx ctx;
    uint16_t lb[200];
    htgl_init(&ctx, 0, lb, 200);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    CHECK(htgl_test_abs_x(&ctx, 0) == 0 && htgl_test_abs_y(&ctx, 0) == 0);
    CHECK(htgl_test_abs_x(&ctx, 1) == 10 && htgl_test_abs_y(&ctx, 1) == 20);
    CHECK(htgl_test_abs_x(&ctx, 2) == 15 && htgl_test_abs_y(&ctx, 2) == 27);
    printf("ok\n");
    return 0;
}
