#include <string.h>
#include "htgl_internal.h"

htgl_ctx *htgl_init(htgl_ctx *ctx, const htgl_hal *hal,
                    uint16_t *line_buf, int line_buf_px) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->hal = hal;
    ctx->line_buf = line_buf;
    ctx->line_buf_px = line_buf_px;
    ctx->pressed_node = -1;
    return ctx;
}

int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len) {
    /* Clear any prior loaded view first, so that EVERY early-return below leaves
       the ctx in an honest "not loaded" state (hdr==NULL). Otherwise a failed
       reload would keep rendering the previous UI -- silent, and a use-after-free
       on the SD/OTA reload path where the old blob's memory may be gone. */
    ctx->hdr = 0; ctx->nodes = 0; ctx->anims = 0;
    ctx->count = 0; ctx->anim_count = 0;
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
    /* The caller's line buffer must hold at least one full screen row, else
       htgl_render's band-clear writes screen_w words past its end. screen_w is
       blob-controlled, so this is an untrusted-input bound, not just a contract. */
    if (ctx->line_buf_px < (int)h->screen_w) return -12;
    int anims_end = nodes_end + (int)sizeof(htgl_anim) * (int)h->anim_count;
    /* anim table must fit inside the blob (checked before -8 so it is reachable:
       -8 forces strtab_off >= anims_end and -6 forces strtab_off <= len, which
       together would otherwise make this implied and untestable). */
    if (anims_end > len) return -10;
    /* string table must come after the node array and the anim table. */
    if (h->strtab_off < anims_end) return -8;
    /* every node's parent must be the root sentinel or a strictly earlier node;
       this bounds abs_x[parent] and enforces the parent-before-child invariant
       that the single-pass layout relies on. */
    const htgl_node *nodes = (const htgl_node *)(blob + sizeof(htgl_header));
    for (int i = 0; i < h->node_count; i++) {
        if (nodes[i].type > HTGL_TYPE_TEXT) return -14;   /* unknown node type: render
                           has no handler for it, so reject rather than draw nothing. */
        uint16_t parent = nodes[i].parent;
        if (parent != HTGL_ROOT_PARENT && parent >= (uint16_t)i) return -9;
    }
    /* validate anim table: node_idx in range and prop known (size checked above). */
    const htgl_anim *anims = (const htgl_anim *)(blob + nodes_end);
    for (int i = 0; i < (int)h->anim_count; i++) {
        if (anims[i].node_idx >= (uint16_t)h->node_count) return -11;
        if (anims[i].prop > 4) return -15;                /* unknown animatable prop:
                           htgl_tick has no case for it, so reject rather than no-op. */
    }
    /* validate every referenced string stays inside the blob: node_text() reads
       a length byte at strtab+text_ref then that many bytes, both of which a
       malicious .uib could push out of bounds (OOB read -> rasterized glyphs).
       Check the length-byte read is itself in-bounds before dereferencing it. */
    for (int i = 0; i < h->node_count; i++) {
        /* only TEXT nodes are ever dereferenced via node_text(); a stray
           text_ref on any other node type is never read, so don't reject it. */
        if (nodes[i].type != HTGL_TYPE_TEXT) continue;
        uint16_t tref = nodes[i].text_ref;
        if (tref == HTGL_NO_TEXT) continue;
        int off = (int)h->strtab_off + (int)tref;
        if (off >= len) return -13;                      /* length byte itself OOB */
        if (off + 1 + (int)blob[off] > len) return -13;  /* string body OOB */
    }

    ctx->blob = blob;
    ctx->hdr = h;
    ctx->nodes = nodes;
    ctx->strtab = blob + h->strtab_off;
    ctx->count = h->node_count;
    ctx->anims = anims;
    ctx->anim_count = (int)h->anim_count;

    /* Initialize runtime cur_* from static node fields so load+layout+render
       without any tick reproduces the original static behavior exactly. */
    for (int i = 0; i < ctx->count; i++) {
        ctx->cur_x[i]  = nodes[i].x;
        ctx->cur_y[i]  = nodes[i].y;
        ctx->cur_w[i]  = nodes[i].w;
        ctx->cur_h[i]  = nodes[i].h;
        ctx->cur_bg[i] = nodes[i].bg;
    }
    return 0;
}

int htgl_screen_w(const htgl_ctx *ctx) { return ctx->hdr ? ctx->hdr->screen_w : 0; }
int htgl_screen_h(const htgl_ctx *ctx) { return ctx->hdr ? ctx->hdr->screen_h : 0; }

void htgl_layout(htgl_ctx *ctx) {
    /* Nodes are emitted parent-before-child (DFS), so a single forward pass
       resolves absolute coordinates. Use cur_x/cur_y so animations are reflected. */
    for (int i = 0; i < ctx->count; i++) {
        const htgl_node *n = &ctx->nodes[i];
        if (n->parent == HTGL_ROOT_PARENT) {
            ctx->abs_x[i] = ctx->cur_x[i];
            ctx->abs_y[i] = ctx->cur_y[i];
        } else {
            ctx->abs_x[i] = ctx->abs_x[n->parent] + ctx->cur_x[i];
            ctx->abs_y[i] = ctx->abs_y[n->parent] + ctx->cur_y[i];
        }
    }
}

int16_t htgl_test_abs_x(htgl_ctx *c, int i) { return c->abs_x[i]; }
int16_t htgl_test_abs_y(htgl_ctx *c, int i) { return c->abs_y[i]; }

int htgl_tick(htgl_ctx *ctx, uint32_t now_ms) {
    int changed = 0;
    for (int i = 0; i < ctx->anim_count; i++) {
        const htgl_anim *a = &ctx->anims[i];
        uint32_t dur = (uint32_t)a->dur_ms;
        if (dur == 0) continue;

        /* mode packs both: loop = mode & 0x0F, ease = (mode >> 4) & 0x0F. */
        int loop = (int)(a->mode & 0x0F);
        int ease = (int)((a->mode >> 4) & 0x0F);

        /* Compute num/den for position fraction (integer arithmetic only). */
        uint32_t num, den;
        den = dur;
        switch (loop) {
            case 0: /* once: clamp to [0, dur] */
                num = (now_ms < dur) ? now_ms : dur;
                break;
            case 1: /* loop: wrap */
                num = now_ms % dur;
                break;
            case 2: /* pingpong */
            default: {
                uint32_t cycle = dur * 2u;
                uint32_t c2 = now_ms % cycle;
                num = (c2 < dur) ? c2 : (cycle - c2);
                break;
            }
        }

        /* Normalize to fixed-point 0..256 and apply easing curve. */
        int p = (int)((int32_t)num * 256 / (int32_t)den);  /* 0..256 */
        switch (ease) {
            case 1: /* ease-in (quadratic) */
                p = p * p / 256;
                break;
            case 2: { /* ease-out */
                int q = 256 - p;
                p = 256 - q * q / 256;
                break;
            }
            case 3: /* ease-in-out */
                if (p < 128)
                    p = p * p / 128;
                else {
                    int q = 256 - p;
                    p = 256 - q * q / 128;
                }
                break;
            default: /* linear: no change */
                break;
        }

        int idx = (int)a->node_idx;

        if (a->prop == 4) {
            /* bg color: interpolate per RGB565 channel */
            uint16_t cf = (uint16_t)a->from, ct = (uint16_t)a->to;
            int fr = (cf >> 11) & 0x1F, fg = (cf >> 5) & 0x3F, fb = cf & 0x1F;
            int tr = (ct >> 11) & 0x1F, tg = (ct >> 5) & 0x3F, tb = ct & 0x1F;
            int r = fr + (tr - fr) * p / 256;
            int g = fg + (tg - fg) * p / 256;
            int b = fb + (tb - fb) * p / 256;
            uint16_t color = (uint16_t)((r << 11) | (g << 5) | b);
            if (ctx->cur_bg[idx] != color) {
                ctx->cur_bg[idx] = color;
                changed = 1;
            }
        } else {
            /* value = from + (to-from)*p/256, using int32 intermediate to avoid overflow. */
            int32_t range = (int32_t)a->to - (int32_t)a->from;
            int32_t val32 = (int32_t)a->from + range * (int32_t)p / 256;
            /* Clamp to int16 range */
            if (val32 > 32767) val32 = 32767;
            if (val32 < -32768) val32 = -32768;
            int16_t val = (int16_t)val32;

            int16_t *target = 0;
            switch (a->prop) {
                case 0: target = &ctx->cur_x[idx]; break;
                case 1: target = &ctx->cur_y[idx]; break;
                case 2: target = &ctx->cur_w[idx]; break;
                case 3: target = &ctx->cur_h[idx]; break;
                default: break;
            }
            if (target && *target != val) {
                *target = val;
                changed = 1;
            }
        }
    }
    return changed;
}

static const char *node_text(htgl_ctx *ctx, const htgl_node *n, int *out_len) {
    if (n->text_ref == HTGL_NO_TEXT) { *out_len = 0; return 0; }
    const uint8_t *p = ctx->strtab + n->text_ref;
    *out_len = p[0];
    return (const char *)(p + 1);
}

/* ------------------------------------------------------------------ touch */

/* Hit-test (x,y) against interactive BOX nodes.
 * Iterates from the highest index down to 1 (topmost-first, since later nodes
 * are drawn on top).  Returns the first matching node index, or -1 if none.
 * Requires htgl_layout to have been called so abs_x/abs_y/cur_w/cur_h are set. */
static int hittest(htgl_ctx *ctx, int x, int y) {
    for (int i = ctx->count - 1; i >= 1; i--) {
        const htgl_node *n = &ctx->nodes[i];
        if (n->type != HTGL_TYPE_BOX) continue;
        if (n->font == 0) continue;  /* not interactive */
        if (x >= ctx->abs_x[i] && x < ctx->abs_x[i] + ctx->cur_w[i] &&
            y >= ctx->abs_y[i] && y < ctx->abs_y[i] + ctx->cur_h[i]) {
            return i;
        }
    }
    return -1;
}

void htgl_set_tap_handler(htgl_ctx *ctx, htgl_tap_cb cb, void *user) {
    ctx->tap_cb   = cb;
    ctx->tap_user = user;
}

void htgl_pointer_down(htgl_ctx *ctx, int x, int y) {
    ctx->pressed_node = hittest(ctx, x, y);
}

void htgl_pointer_up(htgl_ctx *ctx, int x, int y) {
    if (ctx->pressed_node >= 0 &&
        hittest(ctx, x, y) == ctx->pressed_node &&
        ctx->tap_cb) {
        ctx->tap_cb(ctx->nodes[ctx->pressed_node].font, ctx->tap_user);
    }
    ctx->pressed_node = -1;
}

void htgl_render(htgl_ctx *ctx) {
    if (!ctx->hdr) return;   /* not loaded, or a prior load failed: nothing to draw */
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
                               ax, ay, ctx->cur_w[i], ctx->cur_h[i], ctx->cur_bg[i]);
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
