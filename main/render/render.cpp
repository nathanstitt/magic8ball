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
// Geometry of the locked, face-on front triangle as projected to the screen
// (apex DOWN). Derived from PYRAMID_RADIUS=245 with focal=600 (scaled 1.25x about
// the y=233 center so the tips extend a little past the screen edges): top edge
// near y=122, apex near y=478, top half-width ~194px. Text is laid out inside this.
// Keep these in step with PYRAMID_RADIUS (they scale proportionally about center).
#define TRI_TOP_HW      194.0f   // half-width of the top edge (px)
#define TRI_TOP_Y       122      // y of the flat top edge
#define TRI_APEX_Y      478      // y of the bottom apex
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

// The y of the top of a text block of `nlines` lines. The block is centered at
// TEXT_IDEAL_CENTER_Y (balanced in the die) but shifted up if needed so its bottom
// never passes TEXT_MAX_BOTTOM_Y — so short answers sit centered while tall ones
// rise just enough to clear the narrowing apex.
static int text_block_top(int nlines)
{
    int line_h = text_mb_line_height() + RENDER_LINE_GAP;
    int total_h = nlines * line_h;
    int top = TEXT_IDEAL_CENTER_Y - total_h / 2;
    int bottom = top + total_h;
    if (bottom > TEXT_MAX_BOTTOM_Y) {
        top -= (bottom - TEXT_MAX_BOTTOM_Y);   // shift the whole block up
    }
    return top;
}

// Usable pixel width for each of `nlines` rows when the block is centered on
// the block center. Each line is measured at its own mid-y inside the apex-down
// triangle and inset by TRI_TEXT_MARGIN both sides. Fills widths[0..nlines-1].
static void tri_line_widths(int nlines, int widths[RENDER_MAX_LINES])
{
    int font_line_h = text_mb_line_height();
    int line_h = font_line_h + RENDER_LINE_GAP;
    int y0 = text_block_top(nlines);

    for (int i = 0; i < nlines; i++) {
        float mid_y = (float)(y0 + i * line_h) + (float)font_line_h * 0.5f;
        float half = tri_half_width_at(mid_y);
        int w = (int)(half * 2.0f) - TRI_TEXT_MARGIN * 2;
        if (w < 20) {
            w = 20;
        }
        widths[i] = w;
    }
}

// Max words we balance across lines. 8-ball answers are short (now <=5 words by the
// prompt); a hard cap keeps the brute-force partition search trivially cheap and
// bounds the stack arrays. Extra words beyond this spill onto the last line.
#define WRAP_MAX_WORDS  12

// Width (px) of `count` words [first..first+count) joined by single spaces, using
// the precomputed per-word widths and the space width.
static int joined_width(const int word_px[WRAP_MAX_WORDS], int space_px,
                        int first, int count)
{
    if (count <= 0) {
        return 0;
    }
    int w = 0;
    for (int i = 0; i < count; i++) {
        w += word_px[first + i];
    }
    w += space_px * (count - 1);
    return w;
}

// Best-fit (balanced) word wrap. Greedy filling leaves ragged lines (e.g. a lone
// word on a wide row while a later narrow row overflows). Instead we try every way
// to split the words into 1..RENDER_MAX_LINES contiguous lines, keep only layouts
// where every line fits its row's width (line_widths[i], which narrows toward the
// apex), and pick the one with the FEWEST lines, then — among those — the most
// balanced (smallest widest-line, i.e. least raggedness). Falls back to a greedy
// fit if nothing fits cleanly (very long words), so it always produces something.
int render_wrap(const char *s, const int line_widths[RENDER_MAX_LINES],
                char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN])
{
    // Tokenize into word slices + per-word pixel widths.
    const char *wstart[WRAP_MAX_WORDS];
    int         wlen_arr[WRAP_MAX_WORDS];
    int         word_px[WRAP_MAX_WORDS];
    int nwords = 0;

    const char *p = s ? s : "";
    while (*p && nwords < WRAP_MAX_WORDS) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *end = p;
        while (*end && *end != ' ') {
            end++;
        }
        int wl = (int)(end - p);
        char tmp[RENDER_MAX_LINE_LEN];
        int tl = wl < RENDER_MAX_LINE_LEN - 1 ? wl : RENDER_MAX_LINE_LEN - 1;
        for (int i = 0; i < tl; i++) {
            tmp[i] = p[i];
        }
        tmp[tl] = '\0';
        wstart[nwords] = p;
        wlen_arr[nwords] = tl;
        word_px[nwords] = text_mb_width(tmp);
        nwords++;
        p = end;
    }

    int space_px = text_mb_width(" ");

    if (nwords == 0) {
        lines[0][0] = '\0';
        return 1;
    }

    // Search for the best partition into contiguous lines. break_at[k] = index of
    // the first word on line k+1. We enumerate via a recursive-free loop over the
    // number of lines and break positions. With <=12 words and <=4 lines the space
    // is tiny, so an explicit nested search over up to 3 interior break points is
    // both exhaustive and cheap.
    int best_counts[RENDER_MAX_LINES];   // words per line in the best layout
    int best_nlines = 0;
    int best_score = 0x7fffffff;         // lower = better (fewer lines, then less ragged)

    // Enumerate target line counts from 1..RENDER_MAX_LINES; prefer fewer lines.
    for (int k = 1; k <= RENDER_MAX_LINES && k <= nwords; k++) {
        // Distribute nwords into k contiguous non-empty groups: choose k-1 interior
        // break points among positions 1..nwords-1. Iterate all combinations.
        int brk[RENDER_MAX_LINES];   // brk[0..k-2] interior breaks; sentinels added
        // Initialize the lexicographically-first combination: 1,2,...,k-1.
        for (int i = 0; i < k - 1; i++) {
            brk[i] = i + 1;
        }
        bool more = true;
        while (more) {
            // Build line word-counts from the break points.
            int counts[RENDER_MAX_LINES];
            int prev = 0;
            for (int i = 0; i < k - 1; i++) {
                counts[i] = brk[i] - prev;
                prev = brk[i];
            }
            counts[k - 1] = nwords - prev;

            // Check fit against each row's width and compute the widest line.
            bool fits = true;
            int widest = 0;
            int first = 0;
            for (int i = 0; i < k; i++) {
                int lw = joined_width(word_px, space_px, first, counts[i]);
                if (lw > line_widths[i]) {
                    fits = false;
                    break;
                }
                if (lw > widest) {
                    widest = lw;
                }
                first += counts[i];
            }
            if (fits) {
                // Score (lower = better). Fewer lines dominates. Then prefer a
                // TOP-HEAVY fill: the apex-down triangle narrows downward (each lower
                // row holds less), so more text belongs on the wider upper rows. We
                // reward early fullness by summing each line's width weighted MORE for
                // earlier lines; negate it so "more up top" lowers the score. This
                // makes "Body knows / best" beat "Body / knows best".
                int topheavy = 0;
                int first2 = 0;
                for (int i = 0; i < k; i++) {
                    int lw = joined_width(word_px, space_px, first2, counts[i]);
                    topheavy += lw * (k - i);   // earlier line -> bigger weight
                    first2 += counts[i];
                }
                (void)widest;
                int score = (k << 20) - topheavy;
                if (score < best_score) {
                    best_score = score;
                    best_nlines = k;
                    for (int i = 0; i < k; i++) {
                        best_counts[i] = counts[i];
                    }
                }
            }

            // Advance to the next combination of interior break points.
            if (k == 1) {
                more = false;
            } else {
                int i = k - 2;
                while (i >= 0 && brk[i] >= nwords - (k - 1 - i)) {
                    i--;
                }
                if (i < 0) {
                    more = false;
                } else {
                    brk[i]++;
                    for (int j = i + 1; j < k - 1; j++) {
                        brk[j] = brk[j - 1] + 1;
                    }
                }
            }
        }
        // Prefer the fewest lines that fit: once a line count fits, stop searching
        // larger counts (they'd only add lines).
        if (best_nlines == k) {
            break;
        }
    }

    // Fallback: nothing fit cleanly (e.g. a single word wider than its row). Use a
    // simple greedy fill so we still render something rather than nothing.
    if (best_nlines == 0) {
        int n = 0;
        int first = 0;
        int counts[RENDER_MAX_LINES] = {0};
        int cur = 0;
        for (int w = 0; w < nwords; w++) {
            int need = (counts[n] == 0) ? word_px[w]
                                        : cur + space_px + word_px[w];
            if (counts[n] > 0 && need > line_widths[n] && n < RENDER_MAX_LINES - 1) {
                n++;
                cur = word_px[w];
                counts[n] = 1;
            } else {
                cur = need;
                counts[n]++;
            }
        }
        (void)first;
        best_nlines = n + 1;
        for (int i = 0; i < best_nlines; i++) {
            best_counts[i] = counts[i];
        }
    }

    // Emit the chosen partition into the line buffers.
    int first = 0;
    for (int i = 0; i < best_nlines; i++) {
        int pos = 0;
        for (int j = 0; j < best_counts[i]; j++) {
            if (j > 0 && pos < RENDER_MAX_LINE_LEN - 1) {
                lines[i][pos++] = ' ';
            }
            const char *ws = wstart[first + j];
            int wl = wlen_arr[first + j];
            for (int c = 0; c < wl && pos < RENDER_MAX_LINE_LEN - 1; c++) {
                lines[i][pos++] = ws[c];
            }
        }
        lines[i][pos] = '\0';
        first += best_counts[i];
    }
    return best_nlines;
}

static void render_text(fb_t *fb, const scene_t *sc)
{
    char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN];
    int widths[RENDER_MAX_LINES];

    // Pass 1: wrap against widths laid out for the maximum number of lines
    // (text sits high in the wide upper band) to discover how many lines we use.
    tri_line_widths(RENDER_MAX_LINES, widths);
    int nlines = render_wrap(sc->text, widths, lines);

    // Pass 2: now that we know nlines, lay the block out centered on
    // the block center, recompute each line's true width at its final y, and
    // re-wrap so the wrap matches where the text actually sits.
    tri_line_widths(nlines, widths);
    nlines = render_wrap(sc->text, widths, lines);

    int line_h = text_mb_line_height() + RENDER_LINE_GAP;
    int y0 = text_block_top(nlines);

    uint16_t color = rgb565(COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);

    for (int i = 0; i < nlines; i++) {
        int w = text_mb_width(lines[i]);
        int x = (int)sc->pyr_cx - w / 2;
        int y = y0 + i * line_h;
        text_mb_draw(fb, lines[i], x, y, color, sc->text_alpha);
    }
}

#ifdef DEBUG_RENDER_PROFILE
#include "esp_timer.h"
#include "esp_log.h"
#define RP_T(var) int64_t var = esp_timer_get_time()
#define RP_LOG(label, t0, t1) ESP_LOGI("rprof", "%s=%dus", label, (int)((t1) - (t0)))
#else
#define RP_T(var)
#define RP_LOG(label, t0, t1)
#endif

void render_frame(fb_t *fb, const scene_t *sc)
{
    RP_T(_t0);
    // 1. Background gradient + murk.
    fx_draw_background(fb, sc->murk);
    RP_T(_t1); RP_LOG("bg", _t0, _t1);

    // 2. Particles. While listening these flit as a brighter blue starfield; the
    //    state machine already agitates them during the (ST_SHAKING) ponder.
    fx_draw_particles(fb, sc);
    RP_T(_t2); RP_LOG("particles", _t1, _t2);

    // 3. Pyramid (3D lit, back-face culled, sorted, rasterized with glow).
    if (sc->pyr_alpha > 0) {
        pyramid_render(fb, sc);
    }
    RP_T(_t3); RP_LOG("pyramid", _t2, _t3);

    // 4. Answer text (inside the locked face).
    if (sc->text != NULL && sc->text_alpha > 0) {
        render_text(fb, sc);
    }
    RP_T(_t4); RP_LOG("text", _t3, _t4);

    // 5. Glass arc highlight.
    fx_draw_glass_arc(fb);

    // 6. Circle clip (must be last for the scene).
    fx_draw_circle_clip(fb);
    RP_T(_t5); RP_LOG("arc+clip", _t4, _t5);

    // 7. Status overlay (Wi-Fi IP / setup hint), drawn AFTER the clip but well
    // inside the circle, so the clip doesn't erase it. Small, dim, monochrome
    // 8x8 bitmap font — utility text, deliberately not the themed answer font.
    if (sc->status != NULL && sc->status[0] != '\0') {
        int size = 2;                       // 8x8 * 2 = 16px tall
        int w = text_width(sc->status, size);
        int x = DISP_CX - w / 2;
        int y = DISP_H - 8 * size - STATUS_MARGIN_Y;
        uint16_t col = rgb565(COL_STATUS_R, COL_STATUS_G, COL_STATUS_B);
        text_draw(fb, sc->status, x, y, size, col, STATUS_ALPHA);
    }
}
