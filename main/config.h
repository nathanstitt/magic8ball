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
#define PYRAMID_RADIUS      140.0f   // circumradius in pixels at pyr_scale = 1
#define PYRAMID_FOCAL       600.0f   // perspective focal length (pixels)
#define PYRAMID_LIGHT_X     0.5f
#define PYRAMID_LIGHT_Y     0.8f
#define PYRAMID_LIGHT_Z     0.3f     // light direction (normalized in pyramid.cpp)
#define PYRAMID_AMBIENT     0.15f    // minimum face brightness
#define PYRAMID_DIFFUSE     0.85f

// Submerged start depth: pyramid center begins just below the visible circle and
// rises to DISP_CY during ST_TUMBLING.
#define PYRAMID_START_Y     ((float)(DISP_CY + DISP_RADIUS + 40))

// --- Tumble angular rates (radians/sec, per answer, seeded from answer index) ---
#define TUMBLE_RX_MIN   1.5f
#define TUMBLE_RX_MAX   3.5f
#define TUMBLE_RY_MIN   0.8f
#define TUMBLE_RY_MAX   2.0f
#define TUMBLE_RZ_MIN  -0.3f
#define TUMBLE_RZ_MAX   0.3f

// --- Edge glow ---
#define GLOW_WIDTH      0.12f    // barycentric distance from edge that glows
#define GLOW_INTENSITY  1.8f     // peak glow multiplier over face color
#define FRESNEL_POWER   2.0f     // sharpness of Fresnel rim

// --- Specular highlight ---
#define PYRAMID_SPECULAR_POWER      12.0f   // shininess exponent
#define PYRAMID_SPECULAR_INTENSITY   0.6f   // additive boost at peak

// --- Text layout ---
#define RENDER_MAX_LINES     4
#define RENDER_MAX_LINE_LEN  32
#define TRI_TEXT_MARGIN      18    // px inset from triangle edge to text bounding column
#define RENDER_LINE_GAP      4     // px between wrapped lines

// --- Colors (R, G, B — 0..255) ---
#define COL_BG_EDGE_R    4
#define COL_BG_EDGE_G    6
#define COL_BG_EDGE_B   16
#define COL_BG_CTR_R    10
#define COL_BG_CTR_G    18
#define COL_BG_CTR_B    48
#define COL_TRI_R       24
#define COL_TRI_G       40
#define COL_TRI_B      150
#define COL_TRI_ALPHA  210
#define COL_TEXT_R     200
#define COL_TEXT_G     210
#define COL_TEXT_B     230
