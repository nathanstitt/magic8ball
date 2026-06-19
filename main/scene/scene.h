#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ST_IDLE,
    ST_SHAKING,
    ST_TUMBLING,    // pyramid rising + spinning
    ST_LOCKING,     // rotation decelerating to face-on, glow blooming
    ST_SHOWING,     // locked, text fully visible
    ST_SLEEP,
    ST_DISMISSING,  // tap dismissed the answer: triangle + text fade back to liquid
} state_t;

typedef struct {
    float   x;
    float   y;
    float   z;
    float   vx;
    float   vy;
    uint8_t alpha;
} particle_t;

#define SCENE_MAX_PARTICLES 24

typedef struct {
    state_t state;

    // Pyramid world position (screen coords of center).
    float    pyr_cx;       // screen x (always DISP_CX)
    float    pyr_cy;       // screen y (rises from below during TUMBLING)
    float    pyr_scale;    // 1.0 = full size

    // 3x3 rotation matrix, COLUMN-MAJOR storage:
    //   pyr_rot[0..2] = col0, pyr_rot[3..5] = col1, pyr_rot[6..8] = col2.
    // Element at (row, col) is pyr_rot[col * 3 + row]. Identity at rest.
    float    pyr_rot[9];

    // Tumble angular velocities (rad/s), seeded per answer.
    float    pyr_rx_rate;
    float    pyr_ry_rate;
    float    pyr_rz_rate;

    // Random off-screen entry point for this ask: the triangle slides in from
    // here toward the center during ST_TUMBLING (chosen per-ask in the state
    // machine). pyr_jit_phase varies the positional-shake phase per ask.
    float    pyr_start_x;
    float    pyr_start_y;
    float    pyr_jit_phase;

    // Visual state.
    uint8_t  pyr_alpha;    // 0 = invisible, 255 = fully opaque
    float    pyr_glow;     // 0..1, drives edge glow width/intensity
    uint8_t  text_alpha;
    uint8_t  murk;

    // Answer.
    const char *text;
    // Bumped whenever `text` is (re)pointed. The render core's static-frame-skip
    // memcmp's the whole scene_t including the `text` POINTER value; two distinct
    // custom messages can reuse the same backing buffer (so the pointer is
    // identical), which could wrongly skip a redraw when only the bytes change.
    // This counter rides along in that memcmp and guarantees a new ask differs.
    uint32_t    text_seq;

    // Optional bottom status overlay (Wi-Fi IP / setup hint), drawn small and dim
    // by the renderer. NULL = nothing to show. Set by app_main from the net layer
    // AFTER the scene is copied from the state machine — the state machine never
    // touches it, so scene/ stays networking-free and host-testable.
    const char *status;

    // Listening swirl (presentation-only, set by app_main during a VOICE listen;
    // never touched by the state machine). `listening` selects the animated swirl
    // background over the static gradient; `swirl_phase` advances each tick so the
    // swirl animates AND so the render core's static-frame-skip memcmp sees each
    // listening frame as different (forcing a redraw). See render/fx.cpp.
    bool     listening;
    float    swirl_phase;

    // Particles.
    particle_t particles[SCENE_MAX_PARTICLES];
    int        particle_count;
} scene_t;

#ifdef __cplusplus
}
#endif
