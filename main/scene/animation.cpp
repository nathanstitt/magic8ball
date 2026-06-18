#include "animation.h"
#include "config.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Scalar / easing helpers
// ---------------------------------------------------------------------------

static float clamp01(float t)
{
    if (t < 0.0f) {
        return 0.0f;
    }
    if (t > 1.0f) {
        return 1.0f;
    }
    return t;
}

float ease_out_cubic(float t)
{
    t = clamp01(t);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

// ---------------------------------------------------------------------------
// 3x3 matrix helpers — COLUMN-MAJOR float[9].
// Element at (row, col) is m[col * 3 + row]:
//   col0 = m[0..2], col1 = m[3..5], col2 = m[6..8].
// ---------------------------------------------------------------------------

// out = a * b  (standard matrix product, column-major storage).
// out(i,j) = sum_k a(i,k) * b(k,j)  ->  out[j*3+i] = sum_k a[k*3+i] * b[j*3+k].
static void mat3_mul(float out[9], const float a[9], const float b[9])
{
    float r[9];
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 3; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 3; k++) {
                sum += a[k * 3 + row] * b[col * 3 + k];
            }
            r[col * 3 + row] = sum;
        }
    }
    for (int i = 0; i < 9; i++) {
        out[i] = r[i];
    }
}

void mat3_identity(float m[9])
{
    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[4] = 1.0f;
    m[5] = 0.0f;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[8] = 1.0f;
}

void mat3_rotate_x(float m[9], float ang)
{
    float c = cosf(ang);
    float s = sinf(ang);
    // R_x column-major: {1,0,0, 0,c,s, 0,-s,c}
    float rx[9];
    rx[0] = 1.0f;
    rx[1] = 0.0f;
    rx[2] = 0.0f;
    rx[3] = 0.0f;
    rx[4] = c;
    rx[5] = s;
    rx[6] = 0.0f;
    rx[7] = -s;
    rx[8] = c;
    mat3_mul(m, m, rx);
}

void mat3_rotate_y(float m[9], float ang)
{
    float c = cosf(ang);
    float s = sinf(ang);
    // R_y column-major: {c,0,-s, 0,1,0, s,0,c}
    float ry[9];
    ry[0] = c;
    ry[1] = 0.0f;
    ry[2] = -s;
    ry[3] = 0.0f;
    ry[4] = 1.0f;
    ry[5] = 0.0f;
    ry[6] = s;
    ry[7] = 0.0f;
    ry[8] = c;
    mat3_mul(m, m, ry);
}

void mat3_rotate_z(float m[9], float ang)
{
    float c = cosf(ang);
    float s = sinf(ang);
    // R_z column-major: {c,s,0, -s,c,0, 0,0,1}
    float rz[9];
    rz[0] = c;
    rz[1] = s;
    rz[2] = 0.0f;
    rz[3] = -s;
    rz[4] = c;
    rz[5] = 0.0f;
    rz[6] = 0.0f;
    rz[7] = 0.0f;
    rz[8] = 1.0f;
    mat3_mul(m, m, rz);
}

// ---------------------------------------------------------------------------
// vec3 helpers (operate on a contiguous float[3], e.g. a matrix column)
// ---------------------------------------------------------------------------

static float vec3_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void vec3_normalize(float v[3])
{
    float len = sqrtf(vec3_dot(v, v));
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        v[0] *= inv;
        v[1] *= inv;
        v[2] *= inv;
    }
}

static void vec3_cross(float out[3], const float a[3], const float b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Re-orthonormalize the three COLUMNS of a column-major float[9] via
// Gram-Schmidt: col0 = normalize(col0);
//               col1 = normalize(col1 - dot(col1,col0)*col0);
//               col2 = cross(col0, col1).
static void mat3_orthonormalize(float m[9])
{
    float c0[3] = { m[0], m[1], m[2] };
    float c1[3] = { m[3], m[4], m[5] };
    float c2[3];

    vec3_normalize(c0);

    float d = vec3_dot(c1, c0);
    c1[0] -= d * c0[0];
    c1[1] -= d * c0[1];
    c1[2] -= d * c0[2];
    vec3_normalize(c1);

    vec3_cross(c2, c0, c1);

    m[0] = c0[0];
    m[1] = c0[1];
    m[2] = c0[2];
    m[3] = c1[0];
    m[4] = c1[1];
    m[5] = c1[2];
    m[6] = c2[0];
    m[7] = c2[1];
    m[8] = c2[2];
}

// ---------------------------------------------------------------------------
// Pose driver
// ---------------------------------------------------------------------------

void anim_apply(scene_t *sc, uint32_t state_ms, uint32_t dt_ms)
{
    switch (sc->state) {
    case ST_IDLE:
    case ST_SLEEP:
        sc->pyr_cx = (float)DISP_CX;
        sc->pyr_cy = PYRAMID_START_Y;
        sc->pyr_scale = 1.0f;
        sc->pyr_alpha = 0;
        sc->pyr_glow = 0.0f;
        sc->text_alpha = 0;
        sc->murk = (sc->state == ST_SLEEP) ? 255 : 40;
        mat3_identity(sc->pyr_rot);
        break;

    case ST_SHAKING:
        sc->pyr_cx = (float)DISP_CX;
        sc->pyr_cy = PYRAMID_START_Y;     // stays submerged during churn
        sc->pyr_scale = 1.0f;
        sc->pyr_alpha = 0;
        sc->pyr_glow = 0.0f;
        sc->text_alpha = 0;
        sc->murk = 220;                   // clouded
        break;

    case ST_TUMBLING: {
        float p = clamp01((float)state_ms / (float)TUMBLE_MS);
        float e = ease_out_cubic(p);
        sc->pyr_cx = (float)DISP_CX;
        sc->pyr_cy = lerpf(PYRAMID_START_Y, (float)DISP_CY, e);
        sc->pyr_scale = 1.0f;
        sc->pyr_alpha = (uint8_t)((float)COL_TRI_ALPHA * e);
        sc->pyr_glow = 0.0f;              // no glow during tumble
        sc->text_alpha = 0;

        // Accumulate spin, eased to zero as p -> 1.
        float spin_factor = 1.0f - e;
        float dt_s = (float)dt_ms / 1000.0f;
        mat3_rotate_x(sc->pyr_rot, sc->pyr_rx_rate * spin_factor * dt_s);
        mat3_rotate_y(sc->pyr_rot, sc->pyr_ry_rate * spin_factor * dt_s);
        mat3_rotate_z(sc->pyr_rot, sc->pyr_rz_rate * spin_factor * dt_s);

        sc->murk = (uint8_t)(220.0f * (1.0f - e));
        break;
    }

    case ST_LOCKING: {
        float p = clamp01((float)state_ms / (float)LOCK_MS);
        float e = ease_out_cubic(p);
        float ident[9];
        mat3_identity(ident);
        // Lerp each element toward identity, then re-orthonormalize columns.
        for (int i = 0; i < 9; i++) {
            sc->pyr_rot[i] = sc->pyr_rot[i] * (1.0f - p) + ident[i] * p;
        }
        mat3_orthonormalize(sc->pyr_rot);

        sc->pyr_cx = (float)DISP_CX;
        sc->pyr_cy = (float)DISP_CY;
        sc->pyr_scale = 1.0f;
        sc->pyr_alpha = 255;
        sc->pyr_glow = e;
        sc->text_alpha = (uint8_t)(255.0f * e);
        sc->murk = 0;
        break;
    }

    case ST_SHOWING:
        sc->pyr_cx = (float)DISP_CX;
        sc->pyr_cy = (float)DISP_CY;
        sc->pyr_scale = 1.0f;
        sc->pyr_alpha = 255;
        sc->pyr_glow = 1.0f;
        sc->text_alpha = 255;
        sc->murk = 0;
        mat3_identity(sc->pyr_rot);
        break;
    }
}

// ---------------------------------------------------------------------------
// Particles (seed/step VERBATIM from prior; z left zero by seed)
// ---------------------------------------------------------------------------

// Deterministic pseudo-spread so host tests are reproducible (no rand()).
void anim_seed_particles(scene_t *sc)
{
    sc->particle_count = PARTICLE_COUNT;
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        // Spread across the circle using a cheap hash of the index.
        uint32_t h = (uint32_t)(i * 2654435761u);
        float fx = (float)((h >> 8) & 0x1FF);          // 0..511
        float fy = (float)((h >> 17) & 0x1FF);
        sc->particles[i].x = (fx / 511.0f) * (DISP_W - 1);
        sc->particles[i].y = (fy / 511.0f) * (DISP_H - 1);
        sc->particles[i].vx = ((int)((h >> 1) & 7) - 3) * 4.0f;  // -12..+12 px/s
        sc->particles[i].vy = ((int)((h >> 4) & 7) - 3) * 4.0f;
        sc->particles[i].alpha = 40 + (h & 0x3F);               // faint
    }
}

void anim_step_particles(scene_t *sc, uint32_t dt_ms, int state)
{
    float dt = (float)dt_ms / 1000.0f;
    float gain = (state == ST_SHAKING) ? 4.0f : 1.0f;  // agitate while churning
    for (int i = 0; i < sc->particle_count; i++) {
        particle_t *p = &sc->particles[i];
        p->x += p->vx * dt * gain;
        p->y += p->vy * dt * gain;
        // Wrap within the buffer so they never leave the scene.
        while (p->x < 0) {
            p->x += DISP_W;
        }
        while (p->x >= DISP_W) {
            p->x -= DISP_W;
        }
        while (p->y < 0) {
            p->y += DISP_H;
        }
        while (p->y >= DISP_H) {
            p->y -= DISP_H;
        }
    }
}
