#ifndef HTGL_H
#define HTGL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Touch/tap input.
 *
 * htgl_tap_cb is called with the tap_id (the data-tap value, 1..255) stored
 * in the BOX node's font byte, and the user pointer passed to htgl_set_tap_handler.
 *
 * htgl_set_tap_handler: register the tap callback.  Pass cb=NULL to disable.
 *
 * htgl_pointer_down: record a pointer-press at (x,y) in screen coordinates.
 *   Call htgl_layout first so abs_x/abs_y/cur_w/cur_h are current.
 *
 * htgl_pointer_up: record a pointer-release at (x,y).  If the release lands on
 *   the same interactive element that was pressed, the tap callback fires.
 *   pressed_node is always reset to -1 after this call.
 *
 * A "tap" requires press AND release on the same element (standard touch UX).
 * Non-interactive BOX nodes (font==0) and TEXT nodes are never hit-tested.
 */
typedef void (*htgl_tap_cb)(int tap_id, void *user);
void htgl_set_tap_handler(htgl_ctx *ctx, htgl_tap_cb cb, void *user);
void htgl_pointer_down(htgl_ctx *ctx, int x, int y);
void htgl_pointer_up(htgl_ctx *ctx, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
