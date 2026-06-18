// =============================================================================
// render.cpp — per-frame compositor (Magic 8 Ball).
//
// Calls gfx + fx + pyramid in order. No LVGL, no glm. The answer text is
// word-wrapped to fit inside the locked, face-on front triangle of the pyramid.
// =============================================================================

#include "render.h"
#include "fx.h"
#include "pyramid.h"
#include "text.h"
#include "color.h"
#include "config.h"
#include <string.h>
#include <math.h>

// The locked pyramid presents its front face {0,2,1} face-on. Projected at the
// resting pose it is an apex-DOWN triangle: a near-horizontal top edge and an
// apex at the bottom. We lay answer text out inside that triangle.
//
// Geometry of the resting front face (object verts scaled by PYRAMID_RADIUS,
// projected with focal=PYRAMID_FOCAL at center): the top edge sits a little
// above center and the apex hangs below. We approximate the usable text region
// as an apex-down triangle of half-height TRI_TEXT_H about the face center,
// with a top half-width TRI_TOP_HW shrinking linearly to ~0 at the apex.
//
// These are derived from the projected resting face but kept as tunables so the
// layout never depends on glm (render.cpp stays host-testable).
#define TRI_TOP_HW      116.0f   // half-width of the top edge (px)
#define TRI_TOP_Y       (DISP_CY - 54)   // y of the top edge
#define TRI_APEX_Y      (DISP_CY + 140)  // y of the bottom apex
// FONT_LINE_H is obtained at runtime from text_mb_line_height() (the Montserrat
// Bold cell height) so render.cpp never reaches into the font header directly.

// Half-width of the apex-down triangle at a given screen y (0 above the top
// edge clamps to the top width; at/below the apex it is 0).
static float tri_half_width_at(float y)
{
    float total_h = (float)TRI_APEX_Y - (float)TRI_TOP_Y;
    if (total_h <= 0.0f) {
        return 0.0f;
    }
    float d = y - (float)TRI_TOP_Y;
    if (d < 0.0f) {
        d = 0.0f;
    }
    float frac = d / total_h;
    if (frac > 1.0f) {
        frac = 1.0f;
    }
    float half = TRI_TOP_HW * (1.0f - frac);
    if (half < 0.0f) {
        half = 0.0f;
    }
    return half;
}

// Compute the usable pixel width for each of the RENDER_MAX_LINES text rows,
// centered vertically on the face. Each line is inset by TRI_TEXT_MARGIN on
// both sides.
static void tri_line_widths(int widths[RENDER_MAX_LINES])
{
    int font_line_h = text_mb_line_height();
    int line_h = font_line_h + RENDER_LINE_GAP;
    int total = RENDER_MAX_LINES * line_h;
    float y0 = (float)DISP_CY - (float)total * 0.5f;

    for (int i = 0; i < RENDER_MAX_LINES; i++) {
        float mid_y = y0 + (float)(i * line_h) + (float)font_line_h * 0.5f;
        float half = tri_half_width_at(mid_y);
        int w = (int)(half * 2.0f) - TRI_TEXT_MARGIN * 2;
        if (w < 20) {
            w = 20;
        }
        widths[i] = w;
    }
}

int render_wrap(const char *s, const int line_widths[RENDER_MAX_LINES],
                char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN])
{
    int n = 0;
    int cur = 0;
    lines[0][0] = '\0';

    const char *word = s;
    while (*word) {
        const char *end = word;
        while (*end && *end != ' ') {
            end++;
        }
        int wlen = (int)(end - word);
        if (wlen == 0) {
            word++;
            continue;
        }

        char tmp[RENDER_MAX_LINE_LEN];
        int tl = wlen < RENDER_MAX_LINE_LEN - 1 ? wlen : RENDER_MAX_LINE_LEN - 1;
        for (int i = 0; i < tl; i++) {
            tmp[i] = word[i];
        }
        tmp[tl] = '\0';

        int word_px = text_mb_width(tmp);
        int space_px = text_mb_width(" ");
        int need = (cur == 0) ? word_px : cur + space_px + word_px;

        if (need > line_widths[n] && cur > 0) {
            n++;
            if (n >= RENDER_MAX_LINES) {
                n = RENDER_MAX_LINES - 1;
                break;
            }
            cur = 0;
            lines[n][0] = '\0';
            need = word_px;
        }

        int pos = (int)strlen(lines[n]);
        if (cur > 0 && pos < RENDER_MAX_LINE_LEN - 1) {
            lines[n][pos] = ' ';
            pos++;
        }
        for (int i = 0; i < tl && pos < RENDER_MAX_LINE_LEN - 1; i++) {
            lines[n][pos] = word[i];
            pos++;
        }
        lines[n][pos] = '\0';
        cur = need;

        word = end;
        while (*word == ' ') {
            word++;
        }
    }
    return n + 1;
}

static void render_text(fb_t *fb, const scene_t *sc)
{
    int widths[RENDER_MAX_LINES];
    tri_line_widths(widths);

    char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN];
    int nlines = render_wrap(sc->text, widths, lines);

    int line_h = text_mb_line_height() + RENDER_LINE_GAP;
    int total_h = nlines * line_h;
    int y0 = (int)sc->pyr_cy - total_h / 2;

    uint16_t color = rgb565(COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);

    for (int i = 0; i < nlines; i++) {
        int w = text_mb_width(lines[i]);
        int x = (int)sc->pyr_cx - w / 2;
        int y = y0 + i * line_h;
        text_mb_draw(fb, lines[i], x, y, color, sc->text_alpha);
    }
}

void render_frame(fb_t *fb, const scene_t *sc)
{
    // 1. Background gradient + murk.
    fx_draw_background(fb, sc->murk);

    // 2. Particles (below the pyramid).
    fx_draw_particles(fb, sc);

    // 3. Pyramid (3D lit, back-face culled, sorted, rasterized with glow).
    if (sc->pyr_alpha > 0) {
        pyramid_render(fb, sc);
    }

    // 4. Answer text (inside the locked face).
    if (sc->text != NULL && sc->text_alpha > 0) {
        render_text(fb, sc);
    }

    // 5. Glass arc highlight.
    fx_draw_glass_arc(fb);

    // 6. Circle clip (must be last).
    fx_draw_circle_clip(fb);
}
