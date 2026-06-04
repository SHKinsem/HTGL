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

/* htgl_layout and htgl_render are added in later tasks. */
void htgl_layout(htgl_ctx *ctx) { (void)ctx; }
void htgl_render(htgl_ctx *ctx) { (void)ctx; }
