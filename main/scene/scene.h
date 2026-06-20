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
    float    x;
    float    y;
    float    z;
    float    vx;
    float    vy;
    uint8_t  alpha;
    uint32_t flit_ms;   // ms since this star last re-rolled its drift (listening)
    uint32_t rng;       // per-particle LCG state for flit velocity re-rolls
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

    // Listening flag + starfield fade (presentation-only, set by app_main during a
    // VOICE listen; never touched by the state machine). `listening` is true while
    // the FSM is pondering. `star_fade` (0..255) is the starfield brightness: full
    // while listening, then ramped down by app_main across the answer's rise so the
    // stars dissolve rather than pop out when the triangle floats in. At 0 the
    // particles render as the faint idle motes. The fading/flitting motion also
    // keeps each frame distinct, so the render core's static-skip never skips it.
    bool     listening;
    uint8_t  star_fade;
    // Submitting flag (presentation-only, set by app_main while the Gemini request is
    // in flight, i.e. after the user stops talking). Recolors the starfield white and
    // speeds it up so "thinking/uploading" looks distinct from "listening" (blue).
    bool     submitting;
    // Deferred-text rise: true while the die has risen/locked but the answer text
    // hasn't arrived yet (voice latency overlap). Drives a pulsing glow + held text
    // fade-in. Mirrored from sm_t.text_pending each tick.
    bool     text_pending;

    // Particles.
    particle_t particles[SCENE_MAX_PARTICLES];
    int        particle_count;
} scene_t;

#ifdef __cplusplus
}
#endif
