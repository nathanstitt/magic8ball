#include "text.h"
#include "font8x8.h"
#include "font_montserrat_bold.h"
#include "color.h"
#include <string.h>

#define GLYPH_W 8
#define GLYPH_H 8

// --- 8x8 bitmap font (used by host tests) ---

int text_width(const char *s, int size)
{
    return (int)strlen(s) * GLYPH_W * size;
}

int text_centered_x(const fb_t *fb, const char *s, int size)
{
    return (fb->w - text_width(s, size)) / 2;
}

void text_draw(fb_t *fb, const char *s, int x, int y, int size,
               uint16_t color, uint8_t alpha)
{
    if (alpha == 0) {
        return;
    }
    for (int i = 0; s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 128) {
            ch = '?';
        }
        const unsigned char *glyph = font8x8_basic[ch];
        for (int row = 0; row < GLYPH_H; row++) {
            for (int col = 0; col < GLYPH_W; col++) {
                if (glyph[row] & (1 << col)) {
                    for (int dy = 0; dy < size; dy++) {
                        for (int dx = 0; dx < size; dx++) {
                            int px = x + (i * GLYPH_W + col) * size + dx;
                            int py = y + row * size + dy;
                            if (px >= 0 && py >= 0 && px < fb->w && py < fb->h) {
                                uint16_t dst = fb->px[py * fb->w + px];
                                fb->px[py * fb->w + px] = rgb565_blend(dst, color, alpha);
                            }
                        }
                    }
                }
            }
        }
    }
}

// --- Montserrat Bold font ---

int text_mb_width(const char *s)
{
    return font_mb_str_width(s);
}

int text_mb_line_height(void)
{
    return FONT_MB_CELL_H;
}

void text_mb_draw(fb_t *fb, const char *s, int x, int y,
                  uint16_t color, uint8_t alpha)
{
    if (alpha == 0) {
        return;
    }
    int cx = x;
    for (; *s; s++) {
        const font_mb_glyph_t *g = font_mb_get(*s);
        if (!g) {
            cx += FONT_MB_SPACE_W;
            continue;
        }
        if (!g->bitmap) {
            cx += g->advance;
            continue;
        }

        for (int row = 0; row < FONT_MB_CELL_H; row++) {
            for (int col = 0; col < g->width; col++) {
                uint8_t ga = g->bitmap[row * g->width + col];
                if (ga == 0) {
                    continue;
                }
                // The bold font has no bearing_x field; glyph horizontal
                // placement is baked into the bitmap relative to the pen.
                int px = cx + col;
                int py = y + row;
                if (px < 0 || py < 0 || px >= fb->w || py >= fb->h) {
                    continue;
                }
                // Combine glyph alpha with caller alpha
                uint8_t a = (uint8_t)((uint32_t)ga * alpha / 255);
                uint16_t dst = fb->px[py * fb->w + px];
                fb->px[py * fb->w + px] = rgb565_blend(dst, color, a);
            }
        }
        cx += g->advance;
    }
}
