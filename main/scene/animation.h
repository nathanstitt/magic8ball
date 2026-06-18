#pragma once
#include <stdint.h>
#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// Easing: standard ease-out cubic, input clamped to [0,1].
float ease_out_cubic(float t);

// Fill pyramid pose, alphas, glow, and murk for the current state given
// ms-in-state. dt_ms is required to integrate the tumble rotation per frame.
void anim_apply(scene_t *sc, uint32_t state_ms, uint32_t dt_ms);

// Initialize the particle field once (positions/velocities/alpha).
void anim_seed_particles(scene_t *sc);

// Advance particles by dt_ms; agitation amplified while state == ST_SHAKING.
void anim_step_particles(scene_t *sc, uint32_t dt_ms, int state);

// 3x3 matrix helpers, COLUMN-MAJOR float[9] storage (no glm dependency).
// Element at (row, col) is m[col * 3 + row].
void mat3_identity(float m[9]);
// Each rotate composes onto the existing matrix: m = m * R_axis(ang).
void mat3_rotate_x(float m[9], float ang);
void mat3_rotate_y(float m[9], float ang);
void mat3_rotate_z(float m[9], float ang);

#ifdef __cplusplus
}
#endif
