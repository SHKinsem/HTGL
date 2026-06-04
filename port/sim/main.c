#include <stdio.h>
#include <stdlib.h>
#include "htgl.h"
#include "hal_png.h"

static uint8_t *read_file(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = 0; }
    fclose(f);
    *out_len = (int)n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: htgl_sim <in.uib> <out.png>\n");
        return 2;
    }
    int len = 0;
    uint8_t *blob = read_file(argv[1], &len);
    if (!blob) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    /* line buffer holds a few full-width rows; size to the screen width. */
    htgl_ctx ctx;
    static uint16_t line_buf[1024 * 8];   /* up to 1024px wide, 8-row bands */

    /* Peek screen size: load with a temporary tiny init, then re-init buffer. */
    htgl_init(&ctx, 0, line_buf, sizeof(line_buf) / sizeof(line_buf[0]));
    if (htgl_load(&ctx, blob, len) != 0) {
        fprintf(stderr, "invalid .uib\n"); free(blob); return 1;
    }
    int w = htgl_screen_w(&ctx), h = htgl_screen_h(&ctx);

    hal_png_begin(w, h);
    htgl_hal hal = hal_png_get();
    /* re-init so the engine has the HAL; line buffer band height = floor(N/w) */
    htgl_init(&ctx, &hal, line_buf, (sizeof(line_buf) / sizeof(line_buf[0])));
    htgl_load(&ctx, blob, len);
    htgl_layout(&ctx);
    htgl_render(&ctx);

    int rc = hal_png_write(argv[2]);
    hal_png_end();
    free(blob);
    if (rc != 0) { fprintf(stderr, "png write failed\n"); return 1; }
    printf("wrote %s (%dx%d)\n", argv[2], w, h);
    return 0;
}
