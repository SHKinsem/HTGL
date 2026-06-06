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
    h.anim_count = 0;
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
    h.anim_count = 0;
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

/* Build: SCREEN + 1 TEXT child carrying the 2-char string "Hi". */
static int build_blob3(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 2;
    h.screen_w = 100; h.screen_h = 100;
    h.strtab_off = sizeof(htgl_header) + 2 * sizeof(htgl_node);
    h.anim_count = 0;
    htgl_node n[2];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = 100; n[0].h = 100;
    n[1].type = HTGL_TYPE_TEXT; n[1].parent = 0;
    n[1].x = 5; n[1].y = 5; n[1].fg = 0xFFFF; n[1].text_ref = 0;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    int off = (int)h.strtab_off;
    out[off + 0] = 2;            /* length */
    out[off + 1] = 'H';
    out[off + 2] = 'i';
    return off + 3;
}

/* Build: SCREEN + 2 TEXT children carrying "Hi" and "Yo". */
static int build_blob4(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 3;
    h.screen_w = 100; h.screen_h = 100;
    h.strtab_off = sizeof(htgl_header) + 3 * sizeof(htgl_node);
    h.anim_count = 0;
    htgl_node n[3];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = 100; n[0].h = 100;
    n[1].type = HTGL_TYPE_TEXT; n[1].parent = 0; n[1].text_ref = 0;  /* "Hi" */
    n[2].type = HTGL_TYPE_TEXT; n[2].parent = 0; n[2].text_ref = 3;  /* "Yo" */
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    int off = (int)h.strtab_off;
    out[off + 0] = 2; out[off + 1] = 'H'; out[off + 2] = 'i';
    out[off + 3] = 2; out[off + 4] = 'Y'; out[off + 5] = 'o';
    return off + 6;
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

    /* (d) text_ref pointing outside the blob rejected (OOB read guard) */
    {
        uint8_t t[128];
        int tlen = build_blob3(t);
        CHECK(htgl_load(&ctx, t, tlen) == 0);              /* sanity: valid */
        htgl_node *tn = (htgl_node *)(t + sizeof(htgl_header));
        tn[1].text_ref = 50000;                            /* far past blob end */
        CHECK(htgl_load(&ctx, t, tlen) != 0);
    }

    /* (e) string length byte overrunning the blob rejected (OOB read guard) */
    {
        uint8_t t[128];
        int tlen = build_blob3(t);
        htgl_header *th = (htgl_header *)t;
        t[th->strtab_off] = 200;       /* claims 200 chars; only 2 present */
        CHECK(htgl_load(&ctx, t, tlen) != 0);
    }

    /* (f) line buffer smaller than one screen row rejected (OOB write guard) */
    {
        uint16_t small[64];
        htgl_ctx c2;
        CHECK(htgl_init(&c2, 0, small, 64) == &c2);
        /* `blob` is the 240-wide screen; a 64-px line buffer can't hold a row */
        CHECK(htgl_load(&c2, blob, len) != 0);
    }

    /* (g) boundary: line_buf_px == screen_w is exactly enough -> accepted */
    {
        uint16_t exact[240];
        htgl_ctx c3;
        CHECK(htgl_init(&c3, 0, exact, 240) == &c3);
        CHECK(htgl_load(&c3, blob, len) == 0);
    }

    /* (h) the guard checks EVERY text node: a valid first + invalid second is rejected */
    {
        uint8_t t[160];
        int tlen = build_blob4(t);
        CHECK(htgl_load(&ctx, t, tlen) == 0);              /* sanity: both valid */
        htgl_node *tn = (htgl_node *)(t + sizeof(htgl_header));
        tn[2].text_ref = 50000;                            /* second node OOB */
        CHECK(htgl_load(&ctx, t, tlen) != 0);
    }

    /* (i) a non-TEXT node's text_ref is never dereferenced, so it isn't validated */
    {
        uint8_t p[128];
        int plen = build_blob2(p);                         /* SCREEN + BOX */
        htgl_node *pn = (htgl_node *)(p + sizeof(htgl_header));
        pn[1].text_ref = 50000;                            /* BOX: ignored, must still load */
        CHECK(htgl_load(&ctx, p, plen) == 0);
    }

    /* (j) a TEXT node with no string (text_ref == NO_TEXT) loads fine */
    {
        uint8_t t[128];
        int tlen = build_blob3(t);
        htgl_node *tn = (htgl_node *)(t + sizeof(htgl_header));
        tn[1].text_ref = HTGL_NO_TEXT;
        CHECK(htgl_load(&ctx, t, tlen) == 0);
    }

    printf("ok\n");
    return 0;
}
