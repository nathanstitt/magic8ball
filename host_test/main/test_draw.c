#include "unity.h"
#include "color.h"
#include "draw.h"
#include "framebuffer.h"
#include "text.h"

// ---------------------------------------------------------------------------
// color helpers
// ---------------------------------------------------------------------------

TEST_CASE("rgb565 packs channels", "[color]")
{
    TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(255, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0, 255, 0));
    TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0, 0, 255));
    TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565(0, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(255, 255, 255));
}

TEST_CASE("blend endpoints and per-channel midpoint", "[color]")
{
    uint16_t dst = rgb565(10, 20, 30);
    TEST_ASSERT_EQUAL_HEX16(dst, rgb565_blend(dst, rgb565(200, 200, 200), 0));
    uint16_t src = rgb565(200, 100, 50);
    TEST_ASSERT_EQUAL_HEX16(src, rgb565_blend(rgb565(0, 0, 0), src, 255));

    uint16_t out = rgb565_blend(rgb565(0, 0, 0), rgb565(240, 240, 80), 128);
    uint8_t r5 = (out >> 11) & 0x1F, g6 = (out >> 5) & 0x3F, b5 = out & 0x1F;
    TEST_ASSERT_INT_WITHIN(1, 15, r5);
    TEST_ASSERT_INT_WITHIN(1, 30, g6);
    TEST_ASSERT_INT_WITHIN(1, 5, b5);
}

TEST_CASE("rgb565_scale darkens, never brightens, clamps to [0,1]", "[color]")
{
    uint16_t white = rgb565(255, 255, 255);   // 0x1F / 0x3F / 0x1F
    // intensity 1.0 keeps it (within rounding).
    TEST_ASSERT_EQUAL_HEX16(white, rgb565_scale(white, 1.0f));
    // intensity 0 -> black.
    TEST_ASSERT_EQUAL_HEX16(0, rgb565_scale(white, 0.0f));
    // intensity 0.5 halves each channel: 31->16 (15.5+0.5), 63->32 (31.5+0.5).
    uint16_t half = rgb565_scale(white, 0.5f);
    TEST_ASSERT_EQUAL_INT(16, (half >> 11) & 0x1F);
    TEST_ASSERT_EQUAL_INT(32, (half >> 5) & 0x3F);
    TEST_ASSERT_EQUAL_INT(16, half & 0x1F);
    // over-range intensity is clamped (cannot exceed the base color).
    TEST_ASSERT_EQUAL_HEX16(white, rgb565_scale(white, 5.0f));
}

TEST_CASE("rgb565_add brightens toward white and clamps per channel", "[color]")
{
    uint16_t black = rgb565(0, 0, 0);
    // boost 0 changes nothing.
    TEST_ASSERT_EQUAL_HEX16(black, rgb565_add(black, 0.0f));
    // boost 1.0 from black -> full white (adds 31/63/31).
    TEST_ASSERT_EQUAL_HEX16(rgb565(255, 255, 255), rgb565_add(black, 1.0f));
    // huge boost still clamps to white, never overflows into adjacent channels.
    uint16_t big = rgb565_add(rgb565(100, 100, 100), 10.0f);
    TEST_ASSERT_EQUAL_INT(0x1F, (big >> 11) & 0x1F);
    TEST_ASSERT_EQUAL_INT(0x3F, (big >> 5) & 0x3F);
    TEST_ASSERT_EQUAL_INT(0x1F, big & 0x1F);
    // negative boost is treated as 0 (no darkening).
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 0, 255), rgb565_add(rgb565(0, 0, 255), -1.0f));
}

// ---------------------------------------------------------------------------
// triangle fill (flat int API)
// ---------------------------------------------------------------------------

TEST_CASE("filled triangle paints its centroid and skips far corners", "[draw]")
{
    uint16_t mem[32 * 32];
    fb_t fb;
    fb_init(&fb, mem, 32, 32);
    fb_clear(&fb, rgb565(0, 0, 0));
    // apex (16,4), base (6,26),(26,26)
    draw_triangle(&fb, 16, 4, 6, 26, 26, 26, rgb565(255, 255, 255), 255);
    int cx = (16 + 6 + 26) / 3;
    int cy = (4 + 26 + 26) / 3;
    TEST_ASSERT_EQUAL_HEX16(rgb565(255, 255, 255), fb_get_px(&fb, cx, cy));
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 0, 0), fb_get_px(&fb, 0, 0));
}

TEST_CASE("triangle winding is order-independent", "[draw]")
{
    // Same triangle, CW and CCW vertex order, must fill identically.
    uint16_t ccw[32 * 32];
    fb_t fa;
    fb_init(&fa, ccw, 32, 32);
    fb_clear(&fa, 0);
    draw_triangle(&fa, 16, 4, 6, 26, 26, 26, rgb565(255, 255, 255), 255);

    uint16_t cw[32 * 32];
    fb_t fb;
    fb_init(&fb, cw, 32, 32);
    fb_clear(&fb, 0);
    draw_triangle(&fb, 16, 4, 26, 26, 6, 26, rgb565(255, 255, 255), 255);

    for (int i = 0; i < 32 * 32; i++) {
        TEST_ASSERT_EQUAL_HEX16(ccw[i], cw[i]);
    }
}

TEST_CASE("wide opaque triangle fills via the 64-bit span path", "[draw]")
{
    // A wide flat triangle exercises spans > 8px (the uint64_t pack path).
    // Every interior scanline pixel must equal the fill color exactly.
    static uint16_t mem[120 * 60];
    fb_t fb;
    fb_init(&fb, mem, 120, 60);
    fb_clear(&fb, rgb565(0, 0, 0));
    uint16_t col = rgb565(24, 40, 150);
    draw_triangle(&fb, 60, 5, 5, 55, 115, 55, col, 255);
    // A row deep inside the triangle should have a long unbroken run of col.
    int y = 50;
    int run = 0;
    int max_run = 0;
    for (int x = 0; x < 120; x++) {
        if (fb_get_px(&fb, x, y) == col) {
            run++;
            if (run > max_run) {
                max_run = run;
            }
        } else {
            run = 0;
        }
    }
    TEST_ASSERT_TRUE(max_run > 40);   // a wide contiguous filled span
}

TEST_CASE("triangle alpha blends rather than overwrites", "[draw]")
{
    uint16_t mem[16 * 16];
    fb_t fb;
    fb_init(&fb, mem, 16, 16);
    fb_clear(&fb, rgb565(0, 0, 0));
    draw_triangle(&fb, 8, 1, 1, 14, 14, 14, rgb565(255, 255, 255), 128);
    uint16_t px = fb_get_px(&fb, 8, 8);
    uint8_t r5 = (px >> 11) & 0x1F;
    TEST_ASSERT_TRUE(r5 > 0 && r5 < 31);
}

TEST_CASE("degenerate (zero-area) triangle draws nothing", "[draw]")
{
    uint16_t mem[16 * 16];
    fb_t fb;
    fb_init(&fb, mem, 16, 16);
    fb_clear(&fb, rgb565(0, 0, 0));
    // Three collinear points.
    draw_triangle(&fb, 2, 2, 6, 6, 10, 10, rgb565(255, 255, 255), 255);
    for (int i = 0; i < 16 * 16; i++) {
        TEST_ASSERT_EQUAL_HEX16(0, mem[i]);
    }
}

// ---------------------------------------------------------------------------
// draw_triangle_gradient (radial die fill)
// ---------------------------------------------------------------------------

static int brightness565(uint16_t p)
{
    return ((p >> 11) & 0x1F) + ((p >> 5) & 0x3F) + (p & 0x1F);
}

TEST_CASE("gradient triangle is brighter at the centroid than near the edge", "[draw]")
{
    // center_color is bright, edge_color is dark -> the deep interior (high
    // bary_min) must be brighter than a pixel just inside an edge.
    static uint16_t mem[80 * 80];
    fb_t fb;
    fb_init(&fb, mem, 80, 80);
    fb_clear(&fb, rgb565(0, 0, 0));
    uint16_t center = rgb565(70, 120, 255);
    uint16_t edge = rgb565(8, 18, 95);
    // Bevel off (strength 0) so this isolates the radial fill.
    draw_triangle_gradient(&fb, 40, 6, 6, 72, 72, 72, center, edge, edge, 0.0f, 255);
    // Centroid ~ (39,50).
    uint16_t centroid = fb_get_px(&fb, 39, 50);
    // Near the bottom edge (low bary_min).
    uint16_t near_edge = fb_get_px(&fb, 39, 70);
    TEST_ASSERT_TRUE(brightness565(centroid) > brightness565(near_edge));
}

TEST_CASE("gradient centroid approaches the center color", "[draw]")
{
    static uint16_t mem[80 * 80];
    fb_t fb;
    fb_init(&fb, mem, 80, 80);
    fb_clear(&fb, rgb565(0, 0, 0));
    uint16_t center = rgb565(70, 120, 255);
    uint16_t edge = rgb565(8, 18, 95);
    draw_triangle_gradient(&fb, 40, 6, 6, 72, 72, 72, center, edge, edge, 0.0f, 255);
    uint16_t centroid = fb_get_px(&fb, 39, 50);
    // The centroid (bary_min ~ 0.33 >= GRAD_CENTER_BARY) should be at/near full
    // center brightness, clearly above the edge color.
    TEST_ASSERT_TRUE(brightness565(centroid) > brightness565(edge) + 10);
}

TEST_CASE("gradient triangle alpha 0 leaves the background untouched", "[draw]")
{
    static uint16_t mem[40 * 40];
    fb_t fb;
    fb_init(&fb, mem, 40, 40);
    fb_clear(&fb, rgb565(3, 5, 9));
    uint16_t center = rgb565(70, 120, 255);
    uint16_t edge = rgb565(8, 18, 95);
    draw_triangle_gradient(&fb, 20, 4, 4, 36, 36, 36, center, edge, edge, 0.5f, 0);
    // Centroid pixel must still be the original background (alpha 0 = no change).
    TEST_ASSERT_EQUAL_HEX16(rgb565(3, 5, 9), fb_get_px(&fb, 19, 24));
}

// ---------------------------------------------------------------------------
// gradient / clip / particle / glass (unchanged behaviors)
// ---------------------------------------------------------------------------

TEST_CASE("radial gradient is darker at edge than center", "[draw]")
{
    uint16_t mem[40 * 40];
    fb_t fb;
    fb_init(&fb, mem, 40, 40);
    draw_radial_gradient(&fb, 20, 20, 20, rgb565(10, 18, 48), rgb565(2, 3, 8));
    uint16_t center = fb_get_px(&fb, 20, 20);
    uint16_t edge = fb_get_px(&fb, 20, 1);
    TEST_ASSERT_TRUE((center & 0x1F) > (edge & 0x1F));
}

TEST_CASE("gradient never corrupts a channel at maximum contrast", "[draw]")
{
    static uint16_t mem[40 * 40];
    fb_t fb;
    fb_init(&fb, mem, 40, 40);
    draw_radial_gradient(&fb, 20, 20, 20, rgb565(255, 255, 255), rgb565(0, 0, 0));
    for (int i = 0; i < 40 * 40; i++) {
        uint16_t p = mem[i];
        TEST_ASSERT_TRUE(((p >> 11) & 0x1F) <= 31);
        TEST_ASSERT_TRUE(((p >> 5) & 0x3F) <= 63);
        TEST_ASSERT_TRUE((p & 0x1F) <= 31);
    }
}

TEST_CASE("circle clip keeps the exact rim and clips just past it", "[draw]")
{
    static uint16_t mem[40 * 40];
    fb_t fb;
    fb_init(&fb, mem, 40, 40);
    fb_clear(&fb, rgb565(255, 255, 255));
    draw_circle_clip(&fb, 20, 20, 10);
    TEST_ASSERT_EQUAL_HEX16(rgb565(255, 255, 255), fb_get_px(&fb, 30, 20));   // exact rim kept
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 0, 0), fb_get_px(&fb, 31, 20));         // one past cleared
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 0, 0), fb_get_px(&fb, 0, 0));           // corner cleared
}

TEST_CASE("particle draw lights the target pixel faintly", "[draw]")
{
    uint16_t mem[16 * 16];
    fb_t fb;
    fb_init(&fb, mem, 16, 16);
    fb_clear(&fb, rgb565(0, 0, 0));
    draw_particle(&fb, 8, 8, 120);
    TEST_ASSERT_TRUE(fb_get_px(&fb, 8, 8) != rgb565(0, 0, 0));
}

TEST_CASE("glass arc lights near the top and leaves the bottom dark", "[draw]")
{
    static uint16_t mem[80 * 80];
    fb_t fb;
    fb_init(&fb, mem, 80, 80);
    fb_clear(&fb, rgb565(0, 0, 0));
    draw_glass_arc(&fb, 40, 40, 30);
    int lit_top = 0;
    for (int y = 40 - 22; y <= 40 - 18; y++) {
        for (int x = 40 - 3; x <= 40 + 3; x++) {
            if (fb_get_px(&fb, x, y) != rgb565(0, 0, 0)) {
                lit_top = 1;
            }
        }
    }
    TEST_ASSERT_TRUE(lit_top);
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 0, 0), fb_get_px(&fb, 40, 40 + 20));
}

// ---------------------------------------------------------------------------
// Montserrat Bold text
// ---------------------------------------------------------------------------

TEST_CASE("mb text width is positive and grows with length", "[text]")
{
    int w1 = text_mb_width("Y");
    int w2 = text_mb_width("Yes");
    TEST_ASSERT_TRUE(w1 > 0);
    TEST_ASSERT_TRUE(w2 > w1);
}

TEST_CASE("mb text alpha 0 draws nothing", "[text]")
{
    static uint16_t mem[200 * 60];
    fb_t fb;
    fb_init(&fb, mem, 200, 60);
    fb_clear(&fb, rgb565(0, 0, 0));
    text_mb_draw(&fb, "Yes", 0, 0, rgb565(255, 255, 255), 0);
    for (int i = 0; i < 200 * 60; i++) {
        TEST_ASSERT_EQUAL_HEX16(0, mem[i]);
    }
}

TEST_CASE("mb text draws visible pixels at full alpha", "[text]")
{
    static uint16_t mem[200 * 60];
    fb_t fb;
    fb_init(&fb, mem, 200, 60);
    fb_clear(&fb, rgb565(0, 0, 0));
    text_mb_draw(&fb, "Yes", 10, 8, rgb565(255, 255, 255), 255);
    int lit = 0;
    for (int i = 0; i < 200 * 60; i++) {
        if (mem[i] != 0) {
            lit++;
        }
    }
    TEST_ASSERT_TRUE(lit > 0);
}
