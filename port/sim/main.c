#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "htgl_internal.h"
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

static uint8_t *load_blob_and_check(const char *path, int *out_len,
                                    htgl_ctx *ctx,
                                    uint16_t *line_buf, int line_buf_px) {
    int len = 0;
    uint8_t *blob = read_file(path, &len);
    if (!blob) { fprintf(stderr, "cannot read %s\n", path); return 0; }
    htgl_init(ctx, 0, line_buf, line_buf_px);
    if (htgl_load(ctx, blob, len) != 0) {
        fprintf(stderr, "invalid .uib\n"); free(blob); return 0;
    }
    *out_len = len;
    return blob;
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) {
        fprintf(stderr,
            "usage: htgl_sim <in.uib> <out.png>\n"
            "       htgl_sim <in.uib> <out_prefix> <frames> <total_ms>\n");
        return 2;
    }

    static uint16_t line_buf[1024 * 8];   /* up to 1024px wide, 8-row bands */
    htgl_ctx ctx;
    int len = 0;
    uint8_t *blob = load_blob_and_check(argv[1], &len, &ctx,
                                        line_buf,
                                        (int)(sizeof(line_buf) / sizeof(line_buf[0])));
    if (!blob) return 1;
    int w = htgl_screen_w(&ctx), h = htgl_screen_h(&ctx);
    if (w > 1024) {
        fprintf(stderr, "screen width %d exceeds line buffer (1024)\n", w);
        free(blob); return 1;
    }

    if (argc == 3) {
        /* ---- static single-frame mode (original behaviour) ---- */
        hal_png_begin(w, h);
        htgl_hal hal = hal_png_get();
        htgl_init(&ctx, &hal, line_buf, (int)(sizeof(line_buf) / sizeof(line_buf[0])));
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

    /* ---- frame mode: argc==5 ---- */
    int frames    = atoi(argv[3]);
    long total_ms = atol(argv[4]);
    if (frames <= 0 || total_ms <= 0) {
        fprintf(stderr, "frames and total_ms must be positive\n");
        free(blob); return 1;
    }
    const char *prefix = argv[2];
    /* Build path buffer: prefix + up to a full int's digits (<=11) + ".png" + NUL.
       Sized and written with snprintf so an absurd `frames` truncates, never overflows. */
    int prefix_len = (int)strlen(prefix);
    size_t path_cap = (size_t)prefix_len + 16;
    char *path_buf = (char *)malloc(path_cap);
    if (!path_buf) { free(blob); return 1; }

    int written = 0;
    for (int i = 0; i < frames; i++) {
        uint32_t now_ms = (uint32_t)((uint64_t)i * (uint64_t)total_ms / (uint64_t)frames);

        /* Fresh PNG buffer each frame (clears to black before render). */
        hal_png_begin(w, h);
        htgl_hal hal = hal_png_get();

        /* Reload blob so cur_* is reinitialised from static fields each frame. */
        htgl_init(&ctx, &hal, line_buf, (int)(sizeof(line_buf) / sizeof(line_buf[0])));
        htgl_load(&ctx, blob, len);
        htgl_tick(&ctx, now_ms);
        htgl_layout(&ctx);
        htgl_render(&ctx);

        snprintf(path_buf, path_cap, "%s%03d.png", prefix, i);
        int wrc = hal_png_write(path_buf);
        hal_png_end();
        if (wrc != 0) {
            fprintf(stderr, "png write failed: %s\n", path_buf);
            free(path_buf); free(blob); return 1;
        }
        written++;
    }
    free(path_buf);
    free(blob);
    printf("wrote %d frames to %sNNN.png\n", written, prefix);
    return 0;
}
