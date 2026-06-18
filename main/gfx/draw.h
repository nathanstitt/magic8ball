#pragma once
#include "framebuffer.h"
#include "color.h"

#ifdef __cplusplus
extern "C" {
#endif

// Fill the entire framebuffer with a solid color (alias for fb_clear).
void draw_fill(fb_t *fb, uint16_t color);

// Radial gradient from center color (at cx,cy) to edge color (at radius).
void draw_radial_gradient(fb_t *fb, int cx, int cy, int radius,
                          uint16_t center, uint16_t edge);

// Filled triangle with alpha (0..255) blended over existing pixels.
void draw_triangle(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                   uint16_t color, uint8_t alpha);

// Smooth outer glow around a triangle: for pixels OUTSIDE the triangle within
// `max_dist` px of an edge, blend `color` with an alpha that falls off smoothly
// from `peak_alpha` at the edge to 0 at max_dist. Continuous (no banding) —
// gives the die's light bleeding into the liquid. Pixels inside the triangle
// are left untouched (the gradient fill draws those).
void draw_triangle_glow(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t color, float max_dist, uint8_t peak_alpha);

// Filled triangle with a radial gradient (the Magic 8 Ball die look): bright
// `center_color` deep inside fading to `edge_color` at the triangle edges, plus
// a thin `bevel_color` rim just inside the edge for a faceted-glass highlight.
// `bevel` scales the bevel strength (0 = none, 1 = full). Alpha-blended.
void draw_triangle_gradient(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                            uint16_t center_color, uint16_t edge_color,
                            uint16_t bevel_color, float bevel, uint8_t alpha);

// Faint round particle (a single blended dot) at (x,y).
void draw_particle(fb_t *fb, int x, int y, uint8_t alpha);

// Force every pixel outside the circle to black (round-display safety).
void draw_circle_clip(fb_t *fb, int cx, int cy, int radius);

// Subtle specular arc near the top of the circle (glass highlight).
void draw_glass_arc(fb_t *fb, int cx, int cy, int radius);

#ifdef __cplusplus
}
#endif
