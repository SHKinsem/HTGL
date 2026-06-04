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

/* Build: SCREEN + 1 BOX child, 100x100, no strings. */
static int build_blob2(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 2;
    h.screen_w = 100; h.screen_h = 100;
    h.strtab_off = sizeof(htgl_header) + 2 * sizeof(htgl_node);
    h.reserved = 0;
    htgl_node n[2];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = 100; n[0].h = 100;
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].x = 5; n[1].y = 5; n[1].w = 10; n[1].h = 10;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    return (int)(sizeof(h) + 2 * sizeof(htgl_node));
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

    /* (a) zero screen dimension rejected (else render divides by zero) */
    {
        uint8_t z[128];
        memcpy(z, blob, len);
        htgl_header *zh = (htgl_header *)z;
        zh->screen_w = 0;
        CHECK(htgl_load(&ctx, z, len) != 0);
    }

    /* (b) string table overlapping/before the node array rejected */
    {
        uint8_t s[128];
        memcpy(s, blob, len);
        htgl_header *sh = (htgl_header *)s;
        sh->strtab_off = 0;            /* < nodes_end */
        CHECK(htgl_load(&ctx, s, len) != 0);
    }

    /* (c) out-of-range parent index rejected */
    {
        uint8_t p[128];
        int plen = build_blob2(p);
        htgl_node *pn = (htgl_node *)(p + sizeof(htgl_header));
        pn[1].parent = 50;             /* >= node_count, would read OOB */
        CHECK(htgl_load(&ctx, p, plen) != 0);
        /* sanity: the unmutated 2-node blob still loads */
        plen = build_blob2(p);
        CHECK(htgl_load(&ctx, p, plen) == 0);
    }

    printf("ok\n");
    return 0;
}
