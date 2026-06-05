#ifndef HTGL_INTERNAL_H
#define HTGL_INTERNAL_H

#include <stdint.h>
#include "htgl.h"

#define HTGL_MAX_NODES 256

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  flags;
    uint16_t node_count;
    uint16_t screen_w;
    uint16_t screen_h;
    uint16_t strtab_off;
    uint16_t anim_count;   /* was 'reserved'; 0 = no animations (backward compat) */
} htgl_header;

typedef struct {
    uint8_t  type;
    uint8_t  font;
    uint16_t parent;
    int16_t  x, y, w, h;
    uint16_t bg, fg;
    uint16_t text_ref;
} htgl_node;

/* prop: 0=x 1=y 2=w 3=h 4=bg.  loop: 0=once 1=loop 2=pingpong. */
typedef struct {
    uint16_t node_idx;
    uint8_t  prop;
    uint8_t  loop;
    int16_t  from;
    int16_t  to;
    uint16_t dur_ms;
} htgl_anim;
#pragma pack(pop)

struct htgl_ctx {
    const htgl_hal  *hal;
    uint16_t        *line_buf;
    int              line_buf_px;
    const uint8_t   *blob;
    const htgl_header *hdr;
    const htgl_node   *nodes;
    const uint8_t   *strtab;
    int              count;
    int16_t          abs_x[HTGL_MAX_NODES];
    int16_t          abs_y[HTGL_MAX_NODES];
    /* animation runtime */
    const htgl_anim *anims;
    int              anim_count;
    int16_t          cur_x[HTGL_MAX_NODES];
    int16_t          cur_y[HTGL_MAX_NODES];
    int16_t          cur_w[HTGL_MAX_NODES];
    int16_t          cur_h[HTGL_MAX_NODES];
    uint16_t         cur_bg[HTGL_MAX_NODES];
};

/* draw.c: fill a rectangle (absolute coords) into a band buffer.
   The band covers absolute rows [band_y0, band_y0 + band_h). */
void htgl_fill_rect(uint16_t *band, int band_w, int band_y0, int band_h,
                    int rx, int ry, int rw, int rh, uint16_t color);

/* draw.c: draw ASCII text at absolute (tx,ty) into the band, scaled by `scale`
   (integer >= 1). Background is transparent; only foreground pixels are set. */
void htgl_draw_text(uint16_t *band, int band_w, int band_y0, int band_h,
                    int tx, int ty, const char *text, int len,
                    int scale, uint16_t color);

#endif
