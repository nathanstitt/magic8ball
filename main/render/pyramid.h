#pragma once
#include "../gfx/framebuffer.h"
#include "../scene/scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// Render the lit, perspective-projected tetrahedron described by the scene's
// pyramid fields (pyr_cx/cy, pyr_scale, pyr_rot[9] column-major, pyr_alpha,
// pyr_glow) into the framebuffer. The only translation unit that uses glm.
void pyramid_render(fb_t *fb, const scene_t *sc);

#ifdef __cplusplus
}
#endif
