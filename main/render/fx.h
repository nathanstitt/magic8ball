#pragma once
#include <stdint.h>
#include "../gfx/framebuffer.h"
#include "../scene/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// Background gradient (center -> edge) plus an optional dark fog overlay scaled
// by `murk` (0 = pure gradient, 255 = nearly black).
void fx_draw_background(fb_t *fb, uint8_t murk);

// Draw every active particle in the scene as a faint blended dot.
void fx_draw_particles(fb_t *fb, const scene_t *sc);

// Thin bright glass highlight arc near the top inner edge of the display.
void fx_draw_glass_arc(fb_t *fb);

// Black out everything outside the round display.
void fx_draw_circle_clip(fb_t *fb);

#ifdef __cplusplus
}
#endif
