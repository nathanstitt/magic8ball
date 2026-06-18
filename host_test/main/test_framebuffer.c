#include "unity.h"
#include "framebuffer.h"
#include "color.h"

TEST_CASE("fb_init sets dimensions and backing", "[fb]")
{
    uint16_t mem[8 * 4];
    fb_t fb;
    fb_init(&fb, mem, 8, 4);
    TEST_ASSERT_EQUAL_INT(8, fb.w);
    TEST_ASSERT_EQUAL_INT(4, fb.h);
    TEST_ASSERT_EQUAL_PTR(mem, fb.px);
}

TEST_CASE("fb_clear fills every pixel", "[fb]")
{
    uint16_t mem[8 * 4];
    fb_t fb;
    fb_init(&fb, mem, 8, 4);
    fb_clear(&fb, rgb565(10, 20, 30));
    for (int i = 0; i < 8 * 4; i++) {
        TEST_ASSERT_EQUAL_HEX16(rgb565(10, 20, 30), mem[i]);
    }
}

TEST_CASE("fb_set_px writes in-bounds and ignores out-of-bounds", "[fb]")
{
    uint16_t mem[8 * 4];
    fb_t fb;
    fb_init(&fb, mem, 8, 4);
    fb_clear(&fb, 0);
    fb_set_px(&fb, 3, 2, rgb565(255, 255, 255));
    TEST_ASSERT_EQUAL_HEX16(rgb565(255, 255, 255), mem[2 * 8 + 3]);
    // Out-of-bounds writes must not corrupt memory or crash.
    fb_set_px(&fb, -1, 0, rgb565(255, 0, 0));
    fb_set_px(&fb, 8, 0, rgb565(255, 0, 0));
    fb_set_px(&fb, 0, 4, rgb565(255, 0, 0));
}

TEST_CASE("fb_get_px returns the pixel and 0 out of bounds", "[fb]")
{
    uint16_t mem[8 * 4];
    fb_t fb;
    fb_init(&fb, mem, 8, 4);
    fb_clear(&fb, 0);
    fb_set_px(&fb, 5, 1, rgb565(0, 0, 255));
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 0, 255), fb_get_px(&fb, 5, 1));
    TEST_ASSERT_EQUAL_HEX16(0, fb_get_px(&fb, -1, -1));
    TEST_ASSERT_EQUAL_HEX16(0, fb_get_px(&fb, 100, 100));
}
