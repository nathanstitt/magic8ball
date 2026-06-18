#pragma once
#include <stdint.h>

// =============================================================================
// Magic 8 Ball — all tunable constants. Nothing is a magic literal elsewhere.
// =============================================================================

// --- Display geometry ---
#define DISP_W          466
#define DISP_H          466
#define DISP_CX         233
#define DISP_CY         233
#define DISP_RADIUS     233

// --- Frame rate ---
#define TARGET_FPS      30
#define FRAME_MS        (1000 / TARGET_FPS)

// --- Timing (ms) ---
#define THINK_MIN_MS        1000
#define SHAKE_DEBOUNCE_MS   250
#define IDLE_SLEEP_MS       30000
#define TUMBLE_MS           1400    // ST_TUMBLING duration (rise + spin)
#define LOCK_MS             350     // ST_LOCKING duration (snap face-on, glow bloom, text fade)

// --- IMU ---
// Acceleration magnitude in milli-g (1000 = 1g). A vigorous shake exceeds ~1500.
#define SHAKE_THRESHOLD_MG  1500

// --- Particles ---
#define PARTICLE_COUNT      24

// --- Pyramid geometry ---
// Circumradius (px) of the tetrahedron at pyr_scale=1. Sized so the locked
// front face fills most of the 233px-radius circle with a small dark liquid
// margin (the classic Magic 8 Ball die framed in fluid).
#define PYRAMID_RADIUS      196.0f
#define PYRAMID_FOCAL       600.0f   // perspective focal length (pixels)
#define PYRAMID_LIGHT_X     0.5f
#define PYRAMID_LIGHT_Y     0.8f
#define PYRAMID_LIGHT_Z     0.3f     // light direction (normalized in pyramid.cpp)
#define PYRAMID_AMBIENT     0.15f    // minimum face brightness
#define PYRAMID_DIFFUSE     0.85f

// Submerged start depth: pyramid center begins just below the visible circle and
// rises to DISP_CY during ST_TUMBLING.
#define PYRAMID_START_Y     ((float)(DISP_CY + DISP_RADIUS + 40))

// --- Wobble angular rates (radians/sec, per answer, seeded from answer index) ---
// The die stays mostly face-forward and wobbles gently to reveal its sides a
// little as it rises, rather than tumbling end-over-end. Small rates only.
#define TUMBLE_RX_MIN   0.30f
#define TUMBLE_RX_MAX   0.70f
#define TUMBLE_RY_MIN   0.40f
#define TUMBLE_RY_MAX   0.90f
#define TUMBLE_RZ_MIN  -0.20f
#define TUMBLE_RZ_MAX   0.20f
// Peak wobble tilt in radians (~20°): how far the die leans to show its sides
// during the rise before settling face-on.
#define WOBBLE_AMP      0.35f

// --- Triangle face gradient (the lit blue die look) ---
// bary_min runs 0 at an edge to ~0.333 at the centroid.
#define GRAD_CENTER_BARY   0.34f   // bary_min at/after which the fill is full center color
#define GRAD_BEVEL_BARY    0.09f   // width (in bary_min) of the lighter edge bevel band
#define GRAD_BEVEL_STRENGTH 0.0f   // bevel off: the bright edge replaces the sheen

// --- Outer glow bloom (the die's light bleeding into the liquid) ---
// One smooth per-pixel falloff outside the triangle (draw_triangle_glow): no
// shells, no banding. The glow color matches the bright triangle edge and its
// peak alpha is high, so the rim blends seamlessly into the bloom (no dark
// seam). Strong, diffuse halo like the reference.
#define GLOW_DIST        52.0f     // how far (px) the bloom reaches past the edge
#define GLOW_PEAK_ALPHA  220       // bloom alpha right at the edge (0..255)
#define COL_GLOW_R       45
#define COL_GLOW_G      105
#define COL_GLOW_B      255

// --- Text layout ---
#define RENDER_MAX_LINES     4
#define RENDER_MAX_LINE_LEN  32
#define TRI_TEXT_MARGIN      16    // px inset from triangle edge to text bounding column
#define RENDER_LINE_GAP      2     // px between wrapped lines
// Vertical center of the answer text block. Sits in the wide upper-middle of
// the apex-down triangle (top edge ~144, apex ~429) for max room, like the
// reference where text hugs the top and the apex stays empty.
#define TEXT_CENTER_Y        222

// --- Colors (R, G, B — 0..255) ---
// Background liquid: near-black, faint blue lift toward the center.
#define COL_BG_EDGE_R    2
#define COL_BG_EDGE_G    3
#define COL_BG_EDGE_B   10
#define COL_BG_CTR_R     4
#define COL_BG_CTR_G     8
#define COL_BG_CTR_B    28
// Triangle die: the edge stays BRIGHT (near the glow color) so the rim melts
// into the outer bloom with no dark seam; the center is brightest (near-white
// blue). A gentle bright->brighter gradient, never diving dark at the edge.
#define COL_TRI_EDGE_R   45
#define COL_TRI_EDGE_G  105
#define COL_TRI_EDGE_B  255
#define COL_TRI_CTR_R   140
#define COL_TRI_CTR_G   180
#define COL_TRI_CTR_B   255
#define COL_TRI_BEVEL_R 140
#define COL_TRI_BEVEL_G 180
#define COL_TRI_BEVEL_B 255
#define COL_TRI_ALPHA   245
// Glow halo color in the liquid around the die.
#define COL_HALO_R       20
#define COL_HALO_G       45
#define COL_HALO_B      130
// Answer text: light grey-blue.
#define COL_TEXT_R     205
#define COL_TEXT_G     215
#define COL_TEXT_B     235
