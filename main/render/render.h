#pragma once
#include "framebuffer.h"
#include "scene.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Composite the entire scene into the framebuffer for one frame.
// Order: background+murk, particles, pyramid (3D), answer text, glass arc,
// circle clip (last). No LVGL, no glm here.
void render_frame(fb_t *fb, const scene_t *sc);

// Word-wrap an answer string into up to RENDER_MAX_LINES lines, each constrained
// by the pixel width available on the locked triangle face at that line's row.
// Returns the number of lines used (>=1). Exposed for host testing.
int render_wrap(const char *s, const int line_widths[RENDER_MAX_LINES],
                char lines[RENDER_MAX_LINES][RENDER_MAX_LINE_LEN]);

#ifdef __cplusplus
}
#endif
