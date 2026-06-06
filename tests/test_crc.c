/* Tests for the optional .uib CRC32 integrity trailer (flags bit 0). */
#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* test hook exported by htgl.c */
extern uint32_t htgl_test_crc32(const uint8_t *data, int len);

/* SCREEN-only blob, 100x100, no strings, no anims. */
static int build_blob(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 1; h.screen_w = 100; h.screen_h = 100;
    h.strtab_off = sizeof(htgl_header) + sizeof(htgl_node);
    h.anim_count = 0;
    htgl_node n;
    memset(&n, 0, sizeof(n));
    n.type = HTGL_TYPE_SCREEN; n.parent = HTGL_ROOT_PARENT; n.w = 100; n.h = 100;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), &n, sizeof(n));
    return (int)(sizeof(h) + sizeof(n));
}

int main(void) {
    /* 1) standard CRC-32 test vector pins our impl to zlib's (poly 0xEDB88320). */
    CHECK(htgl_test_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);

    /* Build a CRC-bearing blob: set flags bit, append CRC of header+body. */
    uint8_t blob[128];
    int blen = build_blob(blob);
    ((htgl_header *)blob)->flags = 0x01;
    uint32_t crc = htgl_test_crc32(blob, blen);
    blob[blen + 0] = (uint8_t)(crc);
    blob[blen + 1] = (uint8_t)(crc >> 8);
    blob[blen + 2] = (uint8_t)(crc >> 16);
    blob[blen + 3] = (uint8_t)(crc >> 24);
    int total = blen + 4;

    htgl_ctx ctx;
    uint16_t lb[100];
    CHECK(htgl_init(&ctx, 0, lb, 100) == &ctx);

    /* 2) valid CRC -> loads, trailer excluded from the structural view. */
    CHECK(htgl_load(&ctx, blob, total) == 0);
    CHECK(htgl_screen_w(&ctx) == 100);

    /* 3) a flipped data byte -> CRC mismatch -> -16. */
    {
        uint8_t bad[128];
        memcpy(bad, blob, total);
        bad[20] ^= 0xFF;
        CHECK(htgl_load(&ctx, bad, total) == -16);
    }

    /* 4) a corrupted trailer -> -16. */
    {
        uint8_t bad[128];
        memcpy(bad, blob, total);
        bad[total - 1] ^= 0xFF;
        CHECK(htgl_load(&ctx, bad, total) == -16);
    }

    /* 5) flags say CRC present but the blob is too short to hold a trailer -> -16. */
    CHECK(htgl_load(&ctx, blob, (int)sizeof(htgl_header) + 2) == -16);

    /* 6) the SAME bytes with flags=0 (no CRC) still load, ignoring the trailer region. */
    {
        uint8_t plain[128];
        int plen = build_blob(plain);    /* flags=0, no trailer */
        CHECK(htgl_load(&ctx, plain, plen) == 0);
    }

    printf("ok\n");
    return 0;
}
