#include "unity.h"
#include "render.h"
#include "fx.h"
#include "framebuffer.h"
#include "color.h"
#include "scene.h"
#include "config.h"
#include <string.h>

// pyramid.cpp uses glm and is excluded from host tests, so provide a stub.
// render_frame still composites background, particles, text, glass arc, and the
// circle clip — all testable without the 3D core.
void pyramid_render(fb_t *fb, const scene_t *sc)
{
    (void)fb;
    (void)sc;
}

TEST_CASE("showing scene leaves the corners black (circle clipped)", "[render]")
{
    static uint16_t mem[DISP_W * DISP_H];
    fb_t fb;
    fb_init(&fb, mem, DISP_W, DISP_H);

    scene_t sc = {0};
    sc.state = ST_SHOWING;
    sc.pyr_cx = DISP_CX;
    sc.pyr_cy = DISP_CY;
    sc.pyr_alpha = 255;
    sc.pyr_scale = 1.0f;
    sc.text = "Yes";
    sc.text_alpha = 255;
    sc.particle_count = 0;

    render_frame(&fb, &sc);

    TEST_ASSERT_EQUAL_HEX16(0, fb_get_px(&fb, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0, fb_get_px(&fb, DISP_W - 1, 0));
    TEST_ASSERT_EQUAL_HEX16(0, fb_get_px(&fb, 0, DISP_H - 1));
    TEST_ASSERT_EQUAL_HEX16(0, fb_get_px(&fb, DISP_W - 1, DISP_H - 1));
}

TEST_CASE("background center is lit (gradient drawn) inside the circle", "[render]")
{
    static uint16_t mem[DISP_W * DISP_H];
    fb_t fb;
    fb_init(&fb, mem, DISP_W, DISP_H);

    scene_t sc = {0};
    sc.state = ST_IDLE;
    sc.pyr_cx = DISP_CX;
    sc.pyr_cy = DISP_CY;
    sc.pyr_alpha = 0;          // pyramid hidden
    sc.text = 0;
    sc.text_alpha = 0;
    sc.particle_count = 0;
    sc.murk = 0;

    render_frame(&fb, &sc);

    // Center pixel should be the (non-black) gradient center color.
    TEST_ASSERT_TRUE(fb_get_px(&fb, DISP_CX, DISP_CY) != 0);
}

TEST_CASE("background halo lifts the blue near the die center", "[render]")
{
    // The baked halo (fixed strength) makes a point near the center bluer than a
    // point out near the rim where the halo has faded. Pyramid hidden so only
    // the background contributes.
    static uint16_t mem[DISP_W * DISP_H];
    fb_t fb;
    fb_init(&fb, mem, DISP_W, DISP_H);
    scene_t sc = {0};
    sc.state = ST_TUMBLING;
    sc.pyr_cx = DISP_CX;
    sc.pyr_cy = DISP_CY;
    sc.pyr_alpha = 0;
    sc.pyr_scale = 1.0f;
    sc.text = 0;
    sc.text_alpha = 0;
    sc.particle_count = 0;
    sc.murk = 0;
    render_frame(&fb, &sc);

    uint16_t near_center = fb_get_px(&fb, DISP_CX + 60, DISP_CY);
    uint16_t near_rim = fb_get_px(&fb, DISP_CX + 215, DISP_CY);
    TEST_ASSERT_TRUE((near_center & 0x1F) > (near_rim & 0x1F));
}

TEST_CASE("answer text is actually drawn onto the frame", "[render]")
{
    // pyramid stubbed + no particles: the only non-background pixels come from
    // text. Confirm a light text pixel appears in the central band.
    static uint16_t mem[DISP_W * DISP_H];
    fb_t fb;
    fb_init(&fb, mem, DISP_W, DISP_H);
    scene_t sc = {0};
    sc.state = ST_SHOWING;
    sc.pyr_cx = DISP_CX;
    sc.pyr_cy = DISP_CY;
    sc.pyr_alpha = 0;          // pyramid hidden (stub anyway)
    sc.pyr_scale = 1.0f;
    sc.text = "YES";
    sc.text_alpha = 255;
    sc.particle_count = 0;
    sc.murk = 0;
    render_frame(&fb, &sc);

    int found_light = 0;
    for (int y = DISP_CY - 60; y <= DISP_CY + 60 && !found_light; y++) {
        for (int x = DISP_CX - 80; x <= DISP_CX + 80; x++) {
            uint16_t p = fb_get_px(&fb, x, y);
            uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            // Text color is rgb565(200,210,230): much brighter than the dark
            // navy gradient. Detect a clearly light pixel.
            if (r >= 18 && g >= 40 && b >= 24) {
                found_light = 1;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(found_light);
}

TEST_CASE("word wrap splits a long answer into multiple lines", "[render]")
{
    const int narrow[RENDER_MAX_LINES] = {120, 160, 190, 200};
    char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN];
    int n = render_wrap("Concentrate and ask again", narrow, lines);
    TEST_ASSERT_TRUE(n >= 2);
}

TEST_CASE("word wrap keeps a short answer on one line", "[render]")
{
    const int wide[RENDER_MAX_LINES] = {400, 400, 400, 400};
    char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN];
    int n = render_wrap("Yes", wide, lines);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_STRING("Yes", lines[0]);
}

TEST_CASE("word wrap never exceeds RENDER_MAX_LINES even for very long text", "[render]")
{
    const int narrow[RENDER_MAX_LINES] = {40, 40, 40, 40};
    char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN];
    int n = render_wrap("one two three four five six seven eight nine ten", narrow, lines);
    TEST_ASSERT_TRUE(n >= 1 && n <= RENDER_MAX_LINES);
}

TEST_CASE("listening starfield renders brighter blue stars", "[render]")
{
    static uint16_t lis[DISP_W * DISP_H];
    static uint16_t idle[DISP_W * DISP_H];
    fb_t fl, fi;
    fb_init(&fl, lis, DISP_W, DISP_H);
    fb_init(&fi, idle, DISP_W, DISP_H);

    // One particle near center so we can read its pixel deterministically.
    scene_t s = {0};
    s.state = ST_SHAKING;
    s.pyr_cx = DISP_CX;
    s.particle_count = 1;
    s.particles[0].x = (float)DISP_CX;
    s.particles[0].y = (float)DISP_CY;
    s.particles[0].alpha = 60;   // faint stored alpha

    // Listening: the star is drawn brighter + blue; non-listening: the faint mote.
    // Backgrounds are identical (static gradient both ways), so any difference is
    // the particle. The two renders must differ at the particle pixel.
    s.listening = true;
    render_frame(&fl, &s);
    s.listening = false;
    render_frame(&fi, &s);

    TEST_ASSERT_NOT_EQUAL(fb_get_px(&fl, DISP_CX, DISP_CY),
                          fb_get_px(&fi, DISP_CX, DISP_CY));
}

TEST_CASE("listening star pixel is blue-dominant", "[render]")
{
    static uint16_t buf[DISP_W * DISP_H];
    fb_t fb;
    fb_init(&fb, buf, DISP_W, DISP_H);

    scene_t s = {0};
    s.state = ST_SHAKING;
    s.pyr_cx = DISP_CX;
    s.particle_count = 1;
    s.particles[0].x = (float)DISP_CX;
    s.particles[0].y = (float)DISP_CY;
    s.particles[0].alpha = 255;   // bright so the star color dominates the base
    s.listening = true;
    render_frame(&fb, &s);

    uint16_t px = fb_get_px(&fb, DISP_CX, DISP_CY);
    int r = (px >> 11) & 0x1F;
    int g = (px >> 5) & 0x3F;
    int b = px & 0x1F;
    TEST_ASSERT_TRUE(b > r);
    TEST_ASSERT_TRUE(b * 2 > g);
}
