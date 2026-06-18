#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ST_IDLE,
    ST_SHAKING,
    ST_TUMBLING,   // pyramid rising + spinning
    ST_LOCKING,    // rotation decelerating to face-on, glow blooming
    ST_SHOWING,    // locked, text fully visible
    ST_SLEEP,
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

    // Visual state.
    uint8_t  pyr_alpha;    // 0 = invisible, 255 = fully opaque
    float    pyr_glow;     // 0..1, drives edge glow width/intensity
    uint8_t  text_alpha;
    uint8_t  murk;

    // Answer.
    const char *text;

    // Particles.
    particle_t particles[SCENE_MAX_PARTICLES];
    int        particle_count;
} scene_t;

#ifdef __cplusplus
}
#endif
