#pragma once
#include <stdint.h>
#include "../gfx/framebuffer.h"
#include "../scene/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// Precompute the static background gradient + halo falloff LUT into PSRAM.
// Call once after PSRAM is available (e.g. right after display_init). Without
// it fx_draw_background falls back to a plain dark fill.
void fx_init(void);

// Background: near-black gradient + a blue glow halo around the die whose
// strength scales with `murk` (0 = faint halo, 255 = strong/clouded).
void fx_draw_background(fb_t *fb, uint8_t murk);

// Animated "listening" background: a slow blue cloud swirl. `phase` advances over
// time (radians) to animate. Replaces fx_draw_background while a voice listen is
// active. Computes per pixel - only call it on listening frames.
void fx_draw_swirl(fb_t *fb, float phase);

// Draw every active particle in the scene as a faint blended dot.
void fx_draw_particles(fb_t *fb, const scene_t *sc);

// Thin bright glass highlight arc near the top inner edge of the display.
void fx_draw_glass_arc(fb_t *fb);

// Black out everything outside the round display.
void fx_draw_circle_clip(fb_t *fb);

#ifdef __cplusplus
}
#endif
