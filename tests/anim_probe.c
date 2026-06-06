/* Conformance probe: load a .uib emitted by the REAL Python build_uib and assert
 * the C engine decodes the anim table AND interpolates it identically. This is the
 * one cross-language check for the animation encoding (the e2e golden, hello.html,
 * has no animations, so a one-sided change to ANIM_FMT / the mode byte / the bg
 * signed-wrap would otherwise pass every test). Usage: htgl_anim_probe <file.uib>
 */
#include <stdio.h>
#include "../engine/htgl_internal.h"

#define MUST(cond)                                                      \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            return 1;                                                   \
        }                                                               \
    } while (0)

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file.uib>\n", argv[0]); return 2; }

    static uint8_t blob[4096];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    long len = (long)fread(blob, 1, sizeof(blob), f);
    fclose(f);
    MUST(len > 0);

    htgl_ctx ctx;
    static uint16_t lb[240];
    MUST(htgl_init(&ctx, 0, lb, 240) == &ctx);
    MUST(htgl_load(&ctx, blob, (int)len) == 0);

    /* Two anims authored Python-side: BOX#1 x(loop, ease-out), BOX#2 bg(once, linear). */
    MUST(ctx.anim_count == 2);

    const htgl_anim *a0 = &ctx.anims[0];
    MUST(a0->node_idx == 1);
    MUST(a0->prop == 0);             /* x */
    MUST(a0->mode == 0x21);          /* loop(1) | ease-out(2)<<4 */
    MUST(a0->from == 10);
    MUST(a0->to == 200);
    MUST(a0->dur_ms == 1000);

    const htgl_anim *a1 = &ctx.anims[1];
    MUST(a1->node_idx == 2);
    MUST(a1->prop == 4);             /* bg */
    MUST(a1->mode == 0x00);          /* once | linear */
    MUST((uint16_t)a1->from == 0xF800);   /* red, RGB565 signed-wrapped through int16 */
    MUST((uint16_t)a1->to   == 0x001F);   /* blue */
    MUST(a1->dur_ms == 1000);

    /* Interpolation must match Python's intent at an interior time t=250ms.
       x:  p = ease_out(250*256/1000=64) = 256-(192*192/256) = 112;
           cur_x = 10 + (200-10)*112/256 = 93.
       bg: p = 64 (linear); per channel red(31,0,0)->blue(0,0,31):
           r=31+(0-31)*64/256=24, g=0, b=0+31*64/256=7  ->  (24<<11)|7 = 0xC007. */
    htgl_tick(&ctx, 250);
    MUST(ctx.cur_x[1]  == 93);
    MUST(ctx.cur_bg[2] == 0xC007);

    printf("anim conformance ok (2 anims decoded + interpolation matches)\n");
    return 0;
}
