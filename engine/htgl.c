#include <string.h>
#include "htgl_internal.h"

htgl_ctx *htgl_init(htgl_ctx *ctx, const htgl_hal *hal,
                    uint16_t *line_buf, int line_buf_px) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->hal = hal;
    ctx->line_buf = line_buf;
    ctx->line_buf_px = line_buf_px;
    return ctx;
}

int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len) {
    if (len < (int)sizeof(htgl_header)) return -1;
    const htgl_header *h = (const htgl_header *)blob;
    if (memcmp(h->magic, "HTGL", 4) != 0) return -2;
    if (h->version != 1) return -3;
    if (h->node_count == 0 || h->node_count > HTGL_MAX_NODES) return -4;
    int nodes_end = (int)sizeof(htgl_header) + (int)sizeof(htgl_node) * h->node_count;
    if (nodes_end > len) return -5;
    if (h->strtab_off > len) return -6;
    /* screen dims must be non-zero (render computes band_h = line_buf_px/screen_w). */
    if (h->screen_w == 0 || h->screen_h == 0) return -7;
    /* string table must come after the node array. */
    if (h->strtab_off < nodes_end) return -8;
    /* every node's parent must be the root sentinel or a strictly earlier node;
       this bounds abs_x[parent] and enforces the parent-before-child invariant
       that the single-pass layout relies on. */
    const htgl_node *nodes = (const htgl_node *)(blob + sizeof(htgl_header));
    for (int i = 0; i < h->node_count; i++) {
        uint16_t parent = nodes[i].parent;
        if (parent != HTGL_ROOT_PARENT && parent >= (uint16_t)i) return -9;
    }

    ctx->blob = blob;
    ctx->hdr = h;
    ctx->nodes = nodes;
    ctx->strtab = blob + h->strtab_off;
    ctx->count = h->node_count;
    return 0;
}

int htgl_screen_w(const htgl_ctx *ctx) { return ctx->hdr ? ctx->hdr->screen_w : 0; }
int htgl_screen_h(const htgl_ctx *ctx) { return ctx->hdr ? ctx->hdr->screen_h : 0; }

void htgl_layout(htgl_ctx *ctx) {
    /* Nodes are emitted parent-before-child (DFS), so a single forward pass
       resolves absolute coordinates. */
    for (int i = 0; i < ctx->count; i++) {
        const htgl_node *n = &ctx->nodes[i];
        if (n->parent == HTGL_ROOT_PARENT) {
            ctx->abs_x[i] = n->x;
            ctx->abs_y[i] = n->y;
        } else {
            ctx->abs_x[i] = ctx->abs_x[n->parent] + n->x;
            ctx->abs_y[i] = ctx->abs_y[n->parent] + n->y;
        }
    }
}

int16_t htgl_test_abs_x(htgl_ctx *c, int i) { return c->abs_x[i]; }
int16_t htgl_test_abs_y(htgl_ctx *c, int i) { return c->abs_y[i]; }

static const char *node_text(htgl_ctx *ctx, const htgl_node *n, int *out_len) {
    if (n->text_ref == HTGL_NO_TEXT) { *out_len = 0; return 0; }
    const uint8_t *p = ctx->strtab + n->text_ref;
    *out_len = p[0];
    return (const char *)(p + 1);
}

void htgl_render(htgl_ctx *ctx) {
    int sw = ctx->hdr->screen_w;
    int sh = ctx->hdr->screen_h;
    int band_h = ctx->line_buf_px / sw;
    if (band_h < 1) band_h = 1;
    uint16_t screen_bg = ctx->nodes[0].bg;

    for (int by = 0; by < sh; by += band_h) {
        int bh = band_h;
        if (by + bh > sh) bh = sh - by;

        /* clear band to screen background */
        for (int i = 0; i < sw * bh; i++) ctx->line_buf[i] = screen_bg;

        for (int i = 1; i < ctx->count; i++) {
            const htgl_node *n = &ctx->nodes[i];
            int ax = ctx->abs_x[i];
            int ay = ctx->abs_y[i];
            if (n->type == HTGL_TYPE_BOX) {
                htgl_fill_rect(ctx->line_buf, sw, by, bh,
                               ax, ay, n->w, n->h, n->bg);
            } else if (n->type == HTGL_TYPE_TEXT) {
                int tl;
                const char *t = node_text(ctx, n, &tl);
                if (t && tl > 0) {
                    int s = n->font ? n->font : 1;   /* font byte = integer scale */
                    htgl_draw_text(ctx->line_buf, sw, by, bh,
                                   ax, ay, t, tl, s, n->fg);
                }
            }
        }
        ctx->hal->flush(0, by, sw, bh, ctx->line_buf);
    }
}
