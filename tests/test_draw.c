#include "../engine/htgl_internal.h"
#include "ctest_util.h"

int main(void) {
    /* band: 4px wide, rows [0,2) */
    uint16_t band[4 * 2];
    for (int i = 0; i < 8; i++) band[i] = 0;

    /* fill a 2x2 rect at (1,0) with 0xABCD */
    htgl_fill_rect(band, 4, 0, 2, 1, 0, 2, 2, 0xABCD);
    CHECK(band[0 * 4 + 1] == 0xABCD);
    CHECK(band[0 * 4 + 2] == 0xABCD);
    CHECK(band[1 * 4 + 1] == 0xABCD);
    CHECK(band[0 * 4 + 0] == 0x0000);  /* outside x */
    CHECK(band[0 * 4 + 3] == 0x0000);

    /* rect partly above the band is clipped, not crashing */
    for (int i = 0; i < 8; i++) band[i] = 0;
    htgl_fill_rect(band, 4, 2, 2, 0, 0, 4, 4, 0x1111); /* rows 0..3, band rows 2..3 */
    CHECK(band[0] == 0x1111);

    /* text: a space glyph sets no pixels */
    for (int i = 0; i < 8; i++) band[i] = 0;
    htgl_draw_text(band, 4, 0, 2, 0, 0, " ", 1, 1, 0xFFFF);
    CHECK(band[0] == 0x0000);

    printf("ok\n");
    return 0;
}
