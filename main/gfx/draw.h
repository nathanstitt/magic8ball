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

// Filled triangle with per-pixel edge glow / Fresnel rim / specular boost.
// lit_color is the diffuse-lit base (already scaled). add = GLOW_INTENSITY*edge_t*glow
// + fresnel*edge_t + spec brightens toward the edges; result is alpha-blended.
void draw_triangle_lit(fb_t *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                       uint16_t lit_color, uint8_t alpha,
                       float fresnel, float spec, float glow);

// Faint round particle (a single blended dot) at (x,y).
void draw_particle(fb_t *fb, int x, int y, uint8_t alpha);

// Force every pixel outside the circle to black (round-display safety).
void draw_circle_clip(fb_t *fb, int cx, int cy, int radius);

// Subtle specular arc near the top of the circle (glass highlight).
void draw_glass_arc(fb_t *fb, int cx, int cy, int radius);

#ifdef __cplusplus
}
#endif
