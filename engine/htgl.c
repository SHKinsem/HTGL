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

/* CRC-32 (IEEE / zlib: poly 0xEDB88320, reflected, init & final XOR with ~0).
   Matches Python's zlib.crc32, so a trailer written by the tool verifies here.
   Bitwise, no lookup table — load runs once, so we trade speed for 1 KB of flash. */
static uint32_t htgl_crc32(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

/* test hook (CRC is otherwise internal) */
uint32_t htgl_test_crc32(const uint8_t *data, int len) { return htgl_crc32(data, len); }

int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len) {
    /* Clear any prior loaded view first, so that EVERY early-return below leaves
       the ctx in an honest "not loaded" state (hdr==NULL). Otherwise a failed
       reload would keep rendering the previous UI -- silent, and a use-after-free
       on the SD/OTA reload path where the old blob's memory may be gone. */
    ctx->hdr = 0; ctx->nodes = 0; ctx->anims = 0; ctx->opacities = 0; ctx->styles = 0;
    ctx->count = 0; ctx->anim_count = 0;
    if (len < (int)sizeof(htgl_header)) return -1;
    const htgl_header *h = (const htgl_header *)blob;
    if (memcmp(h->magic, "HTGL", 4) != 0) return -2;
    if (h->version != 1 && h->version != 2 && h->version != 3) return -3;
    /* Optional CRC32 trailer (flags bit 0): verify integrity BEFORE trusting any
       field, then drop the 4 trailer bytes from every subsequent bound. Catches
       silent corruption / a half-written OTA image that still passes the structural
       checks below. Legacy blobs carry flags=0 and skip this entirely. */
    if (h->flags & HTGL_FLAG_CRC32) {
        if (len < (int)sizeof(htgl_header) + 4) return -16;
        int dlen = len - 4;
        uint32_t want = (uint32_t)blob[dlen]
                      | ((uint32_t)blob[dlen + 1] << 8)
                      | ((uint32_t)blob[dlen + 2] << 16)
                      | ((uint32_t)blob[dlen + 3] << 24);
        if (htgl_crc32(blob, dlen) != want) return -16;
        len = dlen;
    }
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
    int opacity_end = anims_end;
    const uint8_t *opacities = 0;
    if (h->version >= 2 && (h->flags & HTGL_FLAG_OPACITY)) {
        opacity_end += h->node_count;
        if (opacity_end > len) return -17;
        opacities = blob + anims_end;
    }
    int style_end = opacity_end;
    const uint8_t *styles = 0;
    if (h->version >= 3 && (h->flags & HTGL_FLAG_VISUAL_STYLE)) {
        style_end += 2 * (int)h->node_count;
        if (style_end > len) return -18;
        styles = blob + opacity_end;
    }
    /* string table must come after nodes, animations, and V2/V3 data tables. */
    if (h->strtab_off < style_end) return -8;
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
        if (anims[i].prop > 5) return -15;                /* unknown animatable prop:
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
    ctx->opacities = opacities;
    ctx->styles = styles;

    /* Initialize runtime cur_* from static node fields so load+layout+render
       without any tick reproduces the original static behavior exactly. */
    for (int i = 0; i < ctx->count; i++) {
        ctx->cur_x[i]  = nodes[i].x;
        ctx->cur_y[i]  = nodes[i].y;
        ctx->cur_w[i]  = nodes[i].w;
        ctx->cur_h[i]  = nodes[i].h;
        ctx->cur_x_fp[i] = (int32_t)nodes[i].x << 8;
        ctx->cur_y_fp[i] = (int32_t)nodes[i].y << 8;
        ctx->cur_w_fp[i] = (int32_t)nodes[i].w << 8;
        ctx->cur_h_fp[i] = (int32_t)nodes[i].h << 8;
        ctx->cur_bg[i] = nodes[i].bg;
        ctx->cur_opacity[i] = opacities ? opacities[i] : 255;
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

static int16_t fp_to_i16(int32_t value) {
    /* Round Q24.8 to the nearest physical pixel. Values are clamped before
       this helper, so the negation is safe. */
    return (int16_t)(value >= 0 ? (value + 128) >> 8
                                : -(((-value) + 128) >> 8));
}

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
        } else if (a->prop == 5) {
            int opacity = (int)a->from + ((int)a->to - (int)a->from) * p / 256;
            if (opacity < 0) opacity = 0;
            if (opacity > 255) opacity = 255;
            if (ctx->cur_opacity[idx] != (uint8_t)opacity) {
                ctx->cur_opacity[idx] = (uint8_t)opacity;
                changed = 1;
            }
        } else {
            /* Geometry uses Q24.8 throughout interpolation. This retains
               sub-pixel state for consistent animation math, then rounds only
               at the raster boundary where RGB565 pixels are integer-sized. */
            int64_t from_fp = (int64_t)a->from << 8;
            int64_t range_fp = ((int64_t)a->to - (int64_t)a->from) << 8;
            int64_t value_fp64 = from_fp + range_fp * p / 256;
            if (value_fp64 > ((int64_t)32767 << 8)) value_fp64 = (int64_t)32767 << 8;
            if (value_fp64 < ((int64_t)-32768 << 8)) value_fp64 = (int64_t)-32768 << 8;
            int32_t value_fp = (int32_t)value_fp64;
            int16_t value = fp_to_i16(value_fp);

            int16_t *target = 0;
            int32_t *target_fp = 0;
            switch (a->prop) {
                case 0: target = &ctx->cur_x[idx]; target_fp = &ctx->cur_x_fp[idx]; break;
                case 1: target = &ctx->cur_y[idx]; target_fp = &ctx->cur_y_fp[idx]; break;
                case 2: target = &ctx->cur_w[idx]; target_fp = &ctx->cur_w_fp[idx]; break;
                case 3: target = &ctx->cur_h[idx]; target_fp = &ctx->cur_h_fp[idx]; break;
                default: break;
            }
            if (target_fp) *target_fp = value_fp;
            /* Report a change only when it reaches a physical pixel. Callers
               can then skip a full render/flush for sub-pixel-only ticks. */
            if (target && *target != value) {
                *target = value;
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

void htgl_render_rect(htgl_ctx *ctx, int x, int y, int w, int h) {
    if (!ctx->hdr || !ctx->hal || !ctx->hal->flush || w <= 0 || h <= 0) return;
    int sw = ctx->hdr->screen_w;
    int sh = ctx->hdr->screen_h;

    /* Calculate the clipped end points in 64-bit arithmetic so an accidental
       INT_MAX rectangle cannot wrap into a valid-looking screen region. */
    int64_t x_end64 = (int64_t)x + w;
    int64_t y_end64 = (int64_t)y + h;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x_end64 > sw ? sw : (x_end64 < 0 ? 0 : (int)x_end64);
    int y1 = y_end64 > sh ? sh : (y_end64 < 0 ? 0 : (int)y_end64);
    if (x0 >= x1 || y0 >= y1) return;

    int rw = x1 - x0;
    int band_h = ctx->line_buf_px / rw;
    if (band_h < 1) return;
    uint16_t screen_bg = ctx->nodes[0].bg;

    for (int by = y0; by < y1; by += band_h) {
        int bh = band_h;
        if (by + bh > y1) bh = y1 - by;

        /* Replaying every node into only this rectangle keeps z-order and
           alpha blending identical to a full render without a framebuffer. */
        for (int i = 0; i < rw * bh; i++) ctx->line_buf[i] = screen_bg;

        for (int i = 1; i < ctx->count; i++) {
            const htgl_node *n = &ctx->nodes[i];
            int ax = ctx->abs_x[i] - x0;
            int ay = ctx->abs_y[i];
            uint8_t alpha = ctx->cur_opacity[i];
            if (n->type == HTGL_TYPE_BOX) {
                uint8_t radius = ctx->styles ? ctx->styles[2 * i] : 0;
                uint8_t blur = ctx->styles ? ctx->styles[2 * i + 1] : 0;
                htgl_fill_rounded_glass_rect(ctx->line_buf, rw, by, bh,
                                             ax, ay, ctx->cur_w[i], ctx->cur_h[i], ctx->cur_bg[i],
                                             alpha, radius, blur);
            } else if (n->type == HTGL_TYPE_TEXT) {
                int tl;
                const char *t = node_text(ctx, n, &tl);
                if (t && tl > 0) {
                    int s = n->font ? n->font : 1;   /* font byte = integer scale */
                    htgl_draw_text_alpha(ctx->line_buf, rw, by, bh,
                                         ax, ay, t, tl, s, n->fg, alpha);
                }
            }
        }
        ctx->hal->flush(x0, by, rw, bh, ctx->line_buf);
    }
}

void htgl_render(htgl_ctx *ctx) {
    if (!ctx->hdr) return;
    htgl_render_rect(ctx, 0, 0, ctx->hdr->screen_w, ctx->hdr->screen_h);
}
