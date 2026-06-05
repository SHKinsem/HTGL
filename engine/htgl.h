#ifndef HTGL_H
#define HTGL_H

#include <stdint.h>

#define HTGL_TYPE_SCREEN 0
#define HTGL_TYPE_BOX    1
#define HTGL_TYPE_TEXT   2
#define HTGL_ROOT_PARENT 0xFFFF
#define HTGL_NO_TEXT     0xFFFF

typedef struct {
    /* Push a band (x,y,w,h) of RGB565 pixels to the display. */
    void (*flush)(int x, int y, int w, int h, const uint16_t *buf);
} htgl_hal;

typedef struct htgl_ctx htgl_ctx;

/* Initialize with a HAL and a caller-owned RGB565 line buffer.
   line_buf must hold at least line_buf_px pixels. */
htgl_ctx *htgl_init(htgl_ctx *ctx, const htgl_hal *hal,
                    uint16_t *line_buf, int line_buf_px);

/* Validate and attach a .uib blob (zero-copy). Returns 0 on success. */
int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len);

/* Resolve relative coordinates into absolute screen coordinates. */
void htgl_layout(htgl_ctx *ctx);

/* Render the whole screen in bands, flushing each via the HAL. */
void htgl_render(htgl_ctx *ctx);

/* Screen dimensions from the loaded blob. */
int htgl_screen_w(const htgl_ctx *ctx);
int htgl_screen_h(const htgl_ctx *ctx);

/* Advance animation clock to now_ms (milliseconds).
   Returns 1 if any animated value changed since the previous tick, else 0.
   Must be called after htgl_load; call htgl_layout + htgl_render afterwards
   to reflect the new values. Safe to call with no animations loaded (returns 0). */
int htgl_tick(htgl_ctx *ctx, uint32_t now_ms);

#endif
