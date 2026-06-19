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
            fb->px[y * fb->w + x] = fb_pack((uint16_t)((r << 11) | (g << 5) | b));
        }
    }
}

// Fill a horizontal run [x_l, x_r] (inclusive) on row y with a solid color,
// writing 4 pixels at a time through a uint64_t pack in the aligned middle and
// scalar uint16_t at the ragged ends. Caller guarantees 0 <= x_l <= x_r < w.
static void fill_span_opaque(fb_t *fb, int y, int x_l, int x_r, uint16_t color)
{
    color = fb_pack(color);   // store in panel byte order; the replication below is byte-pattern-agnostic
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
                row[x] = fb_pack(rgb565_blend(fb_unpack(row[x]), color, alpha));
            }
        }
    }
}

void draw_triangle_glow(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t color, float max_dist, uint8_t peak_alpha)
{
    int md = (int)max_dist + 1;
    int minx = (x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2)) - md;
    int maxx = (x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2)) + md;
    int miny = (y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2)) - md;
    int maxy = (y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2)) + md;
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
        return;
    }
    float sgn = (area > 0) ? 1.0f : -1.0f;

    // Edge lengths to convert edge_fn values into true pixel distances.
    float len0 = sqrtf((float)((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)));
    float len1 = sqrtf((float)((x0 - x2) * (x0 - x2) + (y0 - y2) * (y0 - y2)));
    float len2 = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    if (len0 < 1.0f) {
        len0 = 1.0f;
    }
    if (len1 < 1.0f) {
        len1 = 1.0f;
    }
    if (len2 < 1.0f) {
        len2 = 1.0f;
    }
    float inv_md = 1.0f / max_dist;

    // Per-edge signed outside-distance d_e is linear in (x,y). Step it
    // incrementally instead of calling edge_fn (6 mults) every pixel:
    //   d_e = (-sgn / len_e) * edge_fn_e(x,y)
    //   edge_fn_e increases by (b.y - a.y) per +1 x, and by -(b.x - a.x) per +1 y.
    float k0 = -sgn / len0;
    float k1 = -sgn / len1;
    float k2 = -sgn / len2;
    float dx0 = k0 * (float)(y2 - y1);   // d0 per +1 x
    float dx1 = k1 * (float)(y0 - y2);
    float dx2 = k2 * (float)(y1 - y0);
    float dy0 = k0 * (float)(-(x2 - x1)); // d0 per +1 y
    float dy1 = k1 * (float)(-(x0 - x2));
    float dy2 = k2 * (float)(-(x1 - x0));

    // d_e at the top-left of the scan box (minx, miny).
    float row_d0 = k0 * (float)edge_fn(x1, y1, x2, y2, minx, miny);
    float row_d1 = k1 * (float)edge_fn(x2, y2, x0, y0, minx, miny);
    float row_d2 = k2 * (float)edge_fn(x0, y0, x1, y1, minx, miny);

    for (int y = miny; y <= maxy; y++) {
        uint16_t *row = fb->px + (size_t)y * fb->w;
        float d0 = row_d0;
        float d1 = row_d1;
        float d2 = row_d2;
        for (int x = minx; x <= maxx; x++) {
            // Largest outside-distance; <=0 on all edges means inside -> skip.
            float dist = d0;
            if (d1 > dist) {
                dist = d1;
            }
            if (d2 > dist) {
                dist = d2;
            }
            if (dist > 0.0f && dist < max_dist) {
                float f = 1.0f - dist * inv_md;
                f = f * f;
                uint8_t a = (uint8_t)((float)peak_alpha * f);
                if (a != 0) {
                    row[x] = fb_pack(rgb565_blend(fb_unpack(row[x]), color, a));
                }
            }
            d0 += dx0;
            d1 += dx1;
            d2 += dx2;
        }
        row_d0 += dy0;
        row_d1 += dy1;
        row_d2 += dy2;
    }
}

void draw_triangle_gradient(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                            uint16_t center_color, uint16_t edge_color,
                            uint16_t bevel_color, float bevel, uint8_t alpha)
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
        return;
    }
    float inv_area = 1.0f / (float)area;

    // Per-pixel edge functions are linear, so compute them once per row at minx
    // and step by a constant each pixel (no multiplies in the inner loop). The
    // x-step of w_k is its x-coefficient; the y-step is its y-coefficient.
    // w0 = edge_fn(x1,y1,x2,y2,x,y) -> d/dx = (y2-y1), d/dy = -(x2-x1).
    int ax0 = (y2 - y1), ay0 = -(x2 - x1);
    int ax1 = (y0 - y2), ay1 = -(x0 - x2);
    int ax2 = (y1 - y0), ay2 = -(x1 - x0);
    int row_w0 = edge_fn(x1, y1, x2, y2, minx, miny);
    int row_w1 = edge_fn(x2, y2, x0, y0, minx, miny);
    int row_w2 = edge_fn(x0, y0, x1, y1, minx, miny);

    // Radial-fill ramp: edge_color -> center_color over t in [0,1], baked into a
    // 256-entry LUT so the per-pixel inner loop is a table lookup, not a float
    // rgb565_lerp. Cached and rebuilt only if the two colors change.
    static uint16_t s_grad_ramp[256];
    static uint16_t s_grad_edge = 0, s_grad_center = 0;
    static bool s_grad_ready = false;
    if (!s_grad_ready || edge_color != s_grad_edge || center_color != s_grad_center) {
        for (int i = 0; i < 256; i++) {
            s_grad_ramp[i] = rgb565_lerp(edge_color, center_color, (float)i / 255.0f);
        }
        s_grad_edge = edge_color;
        s_grad_center = center_color;
        s_grad_ready = true;
    }
    const float t_scale = 255.0f / GRAD_CENTER_BARY;   // bary_min -> ramp index

    for (int y = miny; y <= maxy; y++) {
        bool entered = false;
        uint16_t *row = fb->px + (size_t)y * fb->w;
        int w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = minx; x <= maxx; x++, w0 += ax0, w1 += ax1, w2 += ax2) {
            bool inside = (area > 0)
                ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) {
                if (entered) {
                    break;
                }
                continue;
            }
            entered = true;

            // bary_min: 0 at an edge, up to ~0.333 at the centroid.
            float b0 = w0 * inv_area;
            float b1 = w1 * inv_area;
            float b2 = w2 * inv_area;
            float bary_min = b0 < b1 ? (b0 < b2 ? b0 : b2) : (b1 < b2 ? b1 : b2);

            // Radial fill via the ramp LUT (edge at the rim -> center deep inside).
            int ti = (int)(bary_min * t_scale);
            if (ti > 255) {
                ti = 255;
            }
            uint16_t pixel = s_grad_ramp[ti];

            // Soft bevel: a thin lighter-blue band just inside the edge. Rare
            // (only near the rim), so the float path here is fine.
            if (bevel > 0.0f && bary_min < GRAD_BEVEL_BARY) {
                float bf = bary_min / GRAD_BEVEL_BARY;          // 0..1 across band
                float tri = 1.0f - fabsf(bf * 2.0f - 1.0f);     // peak at band center
                pixel = rgb565_lerp(pixel, bevel_color, bevel * tri);
            }

            uint16_t dst = fb_unpack(row[x]);
            row[x] = fb_pack(rgb565_blend(dst, pixel, alpha));
        }
        row_w0 += ay0;
        row_w1 += ay1;
        row_w2 += ay2;
    }
}

void draw_particle_color(fb_t *fb, int x, int y, uint8_t alpha, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < fb->w && y < fb->h) {
        uint16_t dst = fb_unpack(fb->px[y * fb->w + x]);
        fb->px[y * fb->w + x] = fb_pack(rgb565_blend(dst, color, alpha));
    }
}

void draw_particle(fb_t *fb, int x, int y, uint8_t alpha)
{
    draw_particle_color(fb, x, y, alpha, rgb565(180, 200, 255));
}

// Black out everything outside a circle. The mask is STATIC across frames, so we
// cache the inside-circle x-span per row (computed once via sqrt per row, not a
// multiply per pixel) and only zero the OUTSIDE pixels — the corners — each frame.
// A full per-pixel distance test was a ~25-30ms full-frame sweep; this touches
// only the ~corner pixels. The cache is keyed on the clip geometry and rebuilt if
// it ever changes (it doesn't in this app — always DISP center/radius).
#define CLIP_MAX_ROWS  512   // >= DISP_H; the per-row span cache size
static int  s_clip_lo[CLIP_MAX_ROWS];   // first x INSIDE the circle on row y
static int  s_clip_hi[CLIP_MAX_ROWS];   // last  x INSIDE the circle on row y
static int  s_clip_cx = -1, s_clip_cy = -1, s_clip_r = -1, s_clip_w = -1, s_clip_h = -1;

void draw_circle_clip(fb_t *fb, int cx, int cy, int radius)
{
    if (fb->h > CLIP_MAX_ROWS) {
        // Span cache too small for this framebuffer — fall back to the exact sweep.
        long r2 = (long)radius * radius;
        for (int y = 0; y < fb->h; y++) {
            for (int x = 0; x < fb->w; x++) {
                long dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > r2) {
                    fb->px[y * fb->w + x] = 0;
                }
            }
        }
        return;
    }

    // (Re)build the per-row inside-circle span cache if the geometry changed.
    if (cx != s_clip_cx || cy != s_clip_cy || radius != s_clip_r ||
        fb->w != s_clip_w || fb->h != s_clip_h) {
        long r2 = (long)radius * radius;
        for (int y = 0; y < fb->h; y++) {
            long dy = y - cy;
            long inside = r2 - dy * dy;
            if (inside < 0) {
                s_clip_lo[y] = 0;        // whole row outside the circle
                s_clip_hi[y] = -1;       // empty inside span -> entire row zeroed
                continue;
            }
            int hw = (int)(sqrtf((float)inside));   // half-width at this row
            int lo = cx - hw;
            int hi = cx + hw;
            if (lo < 0) {
                lo = 0;
            }
            if (hi > fb->w - 1) {
                hi = fb->w - 1;
            }
            s_clip_lo[y] = lo;
            s_clip_hi[y] = hi;
        }
        s_clip_cx = cx;
        s_clip_cy = cy;
        s_clip_r = radius;
        s_clip_w = fb->w;
        s_clip_h = fb->h;
    }

    // Zero only the outside pixels: [0, lo) and (hi, w-1] on each row.
    for (int y = 0; y < fb->h; y++) {
        uint16_t *row = fb->px + (size_t)y * fb->w;
        int lo = s_clip_lo[y];
        int hi = s_clip_hi[y];
        for (int x = 0; x < lo; x++) {
            row[x] = 0;
        }
        for (int x = hi + 1; x < fb->w; x++) {
            row[x] = 0;
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
            uint16_t dst = fb_unpack(fb->px[y * fb->w + x]);
            fb->px[y * fb->w + x] = fb_pack(rgb565_blend(dst, hi, alpha));
        }
    }
}
