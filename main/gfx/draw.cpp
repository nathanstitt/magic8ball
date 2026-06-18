#include "draw.h"
#include "../config.h"
#include <math.h>
#include <stdint.h>
#include <stddef.h>

// Edge function for barycentric fill. Sign encodes which side of edge (a->b)
// the point (px,py) is on. Matches the prior pt_t-based implementation so the
// winding handling is identical between draw_triangle and draw_triangle_lit.
static int edge_fn(int ax, int ay, int bx, int by, int px, int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void draw_fill(fb_t *fb, uint16_t color)
{
    fb_clear(fb, color);
}

void draw_radial_gradient(fb_t *fb, int cx, int cy, int radius,
                          uint16_t center, uint16_t edge)
{
    int cr = (center >> 11) & 0x1F, cg = (center >> 5) & 0x3F, cb = center & 0x1F;
    int er = (edge >> 11) & 0x1F,   eg = (edge >> 5) & 0x3F,   eb = edge & 0x1F;
    float inv_r = radius > 0 ? 1.0f / (float)radius : 0.0f;

    for (int y = 0; y < fb->h; y++) {
        for (int x = 0; x < fb->w; x++) {
            float dx = x - cx, dy = y - cy;
            float d = sqrtf(dx * dx + dy * dy) * inv_r;
            if (d > 1.0f) {
                d = 1.0f;
            }
            // Ordered 2x2 dither to defeat 565 banding.
            static const int bayer[2][2] = {{0, 2}, {3, 1}};
            float dith = (bayer[y & 1][x & 1] - 1.5f) * 0.06f;
            float t = d + dith;
            if (t < 0) {
                t = 0;
            }
            if (t > 1) {
                t = 1;
            }
            int r = (int)(cr + (er - cr) * t + 0.5f);
            int g = (int)(cg + (eg - cg) * t + 0.5f);
            int b = (int)(cb + (eb - cb) * t + 0.5f);
            fb->px[y * fb->w + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

// Fill a horizontal run [x_l, x_r] (inclusive) on row y with a solid color,
// writing 4 pixels at a time through a uint64_t pack in the aligned middle and
// scalar uint16_t at the ragged ends. Caller guarantees 0 <= x_l <= x_r < w.
static void fill_span_opaque(fb_t *fb, int y, int x_l, int x_r, uint16_t color)
{
    uint16_t *row = fb->px + (size_t)y * fb->w;
    uint16_t *p = row + x_l;
    uint16_t *end = row + x_r + 1;   // one-past-last

    int count = x_r - x_l + 1;
    if (count < 8) {
        for (; p < end; p++) {
            *p = color;
        }
        return;
    }

    uint64_t pack = (uint64_t)color
                  | ((uint64_t)color << 16)
                  | ((uint64_t)color << 32)
                  | ((uint64_t)color << 48);

    // Scalar lead-in until p is 8-byte aligned.
    while (p < end && (((uintptr_t)p) & 7u) != 0) {
        *p = color;
        p++;
    }
    // Aligned middle: 4 pixels per 64-bit store.
    while (p + 4 <= end) {
        *(uint64_t *)p = pack;
        p += 4;
    }
    // Scalar tail.
    while (p < end) {
        *p = color;
        p++;
    }
}

void draw_triangle(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                   uint16_t color, uint8_t alpha)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < 0) {
        minx = 0;
    }
    if (miny < 0) {
        miny = 0;
    }
    if (maxx >= fb->w) {
        maxx = fb->w - 1;
    }
    if (maxy >= fb->h) {
        maxy = fb->h - 1;
    }
    if (minx > maxx || miny > maxy) {
        return;
    }

    int area = edge_fn(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;   // degenerate / zero-area triangle
    }

    for (int y = miny; y <= maxy; y++) {
        int span_l = -1;
        int span_r = -1;
        for (int x = minx; x <= maxx; x++) {
            int w0 = edge_fn(x1, y1, x2, y2, x, y);
            int w1 = edge_fn(x2, y2, x0, y0, x, y);
            int w2 = edge_fn(x0, y0, x1, y1, x, y);
            bool inside = (area > 0)
                ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside) {
                if (span_l < 0) {
                    span_l = x;
                }
                span_r = x;
            } else if (span_l >= 0) {
                // The inside region on a scanline is contiguous, so once we
                // leave it we can stop scanning this row.
                break;
            }
        }
        if (span_l < 0) {
            continue;   // no coverage on this scanline
        }
        if (alpha == 255) {
            fill_span_opaque(fb, y, span_l, span_r, color);
        } else {
            uint16_t *row = fb->px + (size_t)y * fb->w;
            for (int x = span_l; x <= span_r; x++) {
                row[x] = rgb565_blend(row[x], color, alpha);
            }
        }
    }
}

void draw_triangle_lit(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                       uint16_t lit_color, uint8_t alpha,
                       float fresnel, float spec, float glow)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < 0) {
        minx = 0;
    }
    if (miny < 0) {
        miny = 0;
    }
    if (maxx >= fb->w) {
        maxx = fb->w - 1;
    }
    if (maxy >= fb->h) {
        maxy = fb->h - 1;
    }
    if (minx > maxx || miny > maxy) {
        return;
    }

    int area = edge_fn(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;   // degenerate / zero-area triangle
    }
    float inv_area = 1.0f / (float)area;

    for (int y = miny; y <= maxy; y++) {
        bool entered = false;
        uint16_t *row = fb->px + (size_t)y * fb->w;
        for (int x = minx; x <= maxx; x++) {
            int w0 = edge_fn(x1, y1, x2, y2, x, y);
            int w1 = edge_fn(x2, y2, x0, y0, x, y);
            int w2 = edge_fn(x0, y0, x1, y1, x, y);
            bool inside = (area > 0)
                ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) {
                if (entered) {
                    break;   // left the contiguous inside span
                }
                continue;
            }
            entered = true;

            // Barycentric coordinates, each normalized to [0..1] via inv_area
            // (which carries the winding sign so all three are non-negative).
            // bary_min is the smallest -> distance to the nearest edge.
            float b0 = w0 * inv_area;
            float b1 = w1 * inv_area;
            float b2 = w2 * inv_area;
            float bary_min = b0 < b1 ? (b0 < b2 ? b0 : b2) : (b1 < b2 ? b1 : b2);
            float edge_t = 1.0f - (bary_min / GLOW_WIDTH);
            if (edge_t < 0.0f) {
                edge_t = 0.0f;
            }
            if (edge_t > 1.0f) {
                edge_t = 1.0f;
            }

            uint16_t pixel = lit_color;
            float add = (GLOW_INTENSITY * edge_t * glow)
                      + (fresnel * edge_t)
                      + spec;
            if (add > 0.0f) {
                pixel = rgb565_add(lit_color, add);
            }

            uint16_t dst = row[x];
            row[x] = rgb565_blend(dst, pixel, alpha);
        }
    }
}

void draw_particle(fb_t *fb, int x, int y, uint8_t alpha)
{
    uint16_t white = rgb565(180, 200, 255);
    if (x >= 0 && y >= 0 && x < fb->w && y < fb->h) {
        uint16_t dst = fb->px[y * fb->w + x];
        fb->px[y * fb->w + x] = rgb565_blend(dst, white, alpha);
    }
}

void draw_circle_clip(fb_t *fb, int cx, int cy, int radius)
{
    long r2 = (long)radius * radius;
    for (int y = 0; y < fb->h; y++) {
        for (int x = 0; x < fb->w; x++) {
            long dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r2) {
                fb->px[y * fb->w + x] = 0;
            }
        }
    }
}

void draw_glass_arc(fb_t *fb, int cx, int cy, int radius)
{
    // A thin bright arc near the top inner edge, fading by angle.
    uint16_t hi = rgb565(255, 255, 255);
    for (int deg = 200; deg <= 340; deg += 1) {
        float a = deg * (float)M_PI / 180.0f;
        int rr = radius - 10;
        int x = cx + (int)(rr * cosf(a));
        int y = cy + (int)(rr * sinf(a));
        // Brightest at the top (deg ~270), fading to the sides.
        float t = 1.0f - fabsf((deg - 270) / 70.0f);
        uint8_t alpha = (uint8_t)(70 * (t < 0 ? 0 : t));
        if (x >= 0 && y >= 0 && x < fb->w && y < fb->h) {
            uint16_t dst = fb->px[y * fb->w + x];
            fb->px[y * fb->w + x] = rgb565_blend(dst, hi, alpha);
        }
    }
}
