#pragma once
#include "framebuffer.h"
#include "color.h"

#ifdef __cplusplus
extern "C" {
#endif

// 8x8 bitmap font (scalable integer, used by host tests)
int  text_width(const char *s, int size);
int  text_centered_x(const fb_t *fb, const char *s, int size);
void text_draw(fb_t *fb, const char *s, int x, int y, int size,
               uint16_t color, uint8_t alpha);

// Montserrat Bold 32pt anti-aliased font
int  text_mb_width(const char *s);
void text_mb_draw(fb_t *fb, const char *s, int x, int y,
                  uint16_t color, uint8_t alpha);
// Cell height (px) of one Montserrat Bold line; the caller adds its own gap.
int  text_mb_line_height(void);

#ifdef __cplusplus
}
#endif
