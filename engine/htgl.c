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

    ctx->blob = blob;
    ctx->hdr = h;
    ctx->nodes = (const htgl_node *)(blob + sizeof(htgl_header));
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

void htgl_render(htgl_ctx *ctx) { (void)ctx; }
