#include "unity.h"
#include "animation.h"
#include "scene.h"
#include "config.h"
#include <math.h>

// ---------------------------------------------------------------------------
// easing
// ---------------------------------------------------------------------------

TEST_CASE("ease_out_cubic endpoints and clamp", "[anim]")
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ease_out_cubic(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, ease_out_cubic(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ease_out_cubic(-1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, ease_out_cubic(2.0f));
    TEST_ASSERT_TRUE(ease_out_cubic(0.5f) > 0.5f);   // front-loaded
}

// ---------------------------------------------------------------------------
// mat3 helpers (column-major: element (row,col) = m[col*3+row])
// ---------------------------------------------------------------------------

static float mat_at(const float m[9], int row, int col)
{
    return m[col * 3 + row];
}

// Apply column-major matrix to a vector: out(i) = sum_k m(i,k)*v(k).
static void apply_mat(const float m[9], const float v[3], float out[3])
{
    for (int i = 0; i < 3; i++) {
        out[i] = mat_at(m, i, 0) * v[0] + mat_at(m, i, 1) * v[1] + mat_at(m, i, 2) * v[2];
    }
}

TEST_CASE("mat3_identity is the identity", "[anim][mat]")
{
    float m[9];
    mat3_identity(m);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, (r == c) ? 1.0f : 0.0f, mat_at(m, r, c));
        }
    }
}

TEST_CASE("Rz(90) maps +X to +Y (right-handed)", "[anim][mat]")
{
    float m[9];
    mat3_identity(m);
    mat3_rotate_z(m, (float)M_PI / 2.0f);
    float x[3] = { 1.0f, 0.0f, 0.0f };
    float out[3];
    apply_mat(m, x, out);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, out[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[2]);
}

TEST_CASE("Rx(90) maps +Y to +Z", "[anim][mat]")
{
    float m[9];
    mat3_identity(m);
    mat3_rotate_x(m, (float)M_PI / 2.0f);
    float y[3] = { 0.0f, 1.0f, 0.0f };
    float out[3];
    apply_mat(m, y, out);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, out[2]);
}

TEST_CASE("Ry(90) maps +Z to +X", "[anim][mat]")
{
    float m[9];
    mat3_identity(m);
    mat3_rotate_y(m, (float)M_PI / 2.0f);
    float z[3] = { 0.0f, 0.0f, 1.0f };
    float out[3];
    apply_mat(m, z, out);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[2]);
}

TEST_CASE("rotations accumulate (two 45-degree = one 90)", "[anim][mat]")
{
    float m[9];
    mat3_identity(m);
    mat3_rotate_z(m, (float)M_PI / 4.0f);
    mat3_rotate_z(m, (float)M_PI / 4.0f);
    float x[3] = { 1.0f, 0.0f, 0.0f };
    float out[3];
    apply_mat(m, x, out);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, out[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, out[1]);
}

// ---------------------------------------------------------------------------
// pose by state
// ---------------------------------------------------------------------------

TEST_CASE("idle pose keeps the pyramid submerged and hidden", "[anim]")
{
    scene_t sc = {0};
    sc.state = ST_IDLE;
    anim_apply(&sc, 200, 16);
    TEST_ASSERT_EQUAL_UINT8(0, sc.pyr_alpha);
    TEST_ASSERT_EQUAL_UINT8(0, sc.text_alpha);
    TEST_ASSERT_TRUE(sc.pyr_cy > DISP_CY);          // below center (submerged)
    TEST_ASSERT_EQUAL_UINT8(40, sc.murk);
}

TEST_CASE("sleep pose is maximally clouded", "[anim]")
{
    scene_t sc = {0};
    sc.state = ST_SLEEP;
    anim_apply(&sc, 100, 16);
    TEST_ASSERT_EQUAL_UINT8(255, sc.murk);
    TEST_ASSERT_EQUAL_UINT8(0, sc.pyr_alpha);
}

TEST_CASE("shaking pose: submerged, hidden, clouded", "[anim]")
{
    scene_t sc = {0};
    sc.state = ST_SHAKING;
    anim_apply(&sc, 300, 16);
    TEST_ASSERT_EQUAL_UINT8(0, sc.pyr_alpha);
    TEST_ASSERT_TRUE(sc.pyr_cy > DISP_CY);
    TEST_ASSERT_EQUAL_UINT8(220, sc.murk);
}

TEST_CASE("tumbling rises the pyramid and fades it in over time", "[anim]")
{
    scene_t early = {0};
    early.state = ST_TUMBLING;
    mat3_identity(early.pyr_rot);
    anim_apply(&early, 1, 16);

    scene_t late = {0};
    late.state = ST_TUMBLING;
    mat3_identity(late.pyr_rot);
    anim_apply(&late, TUMBLE_MS, 16);

    TEST_ASSERT_TRUE(late.pyr_cy < early.pyr_cy);          // rose upward
    TEST_ASSERT_TRUE(late.pyr_alpha > early.pyr_alpha);    // faded in
    TEST_ASSERT_FLOAT_WITHIN(2.0f, (float)DISP_CY, late.pyr_cy);  // settles at center
    TEST_ASSERT_EQUAL_FLOAT(0.0f, late.pyr_glow);          // no glow during tumble
}

TEST_CASE("tumbling actually rotates the matrix away from identity", "[anim]")
{
    scene_t sc = {0};
    sc.state = ST_TUMBLING;
    mat3_identity(sc.pyr_rot);
    sc.pyr_rx_rate = 2.0f;
    sc.pyr_ry_rate = 1.0f;
    sc.pyr_rz_rate = 0.1f;
    // Several frames early in the tumble (spin_factor near 1).
    for (int i = 0; i < 10; i++) {
        anim_apply(&sc, (uint32_t)(i * 16), 16);
    }
    // The matrix should no longer be identity (m[0] noticeably != 1).
    TEST_ASSERT_TRUE(fabsf(sc.pyr_rot[0] - 1.0f) > 0.01f || fabsf(sc.pyr_rot[4] - 1.0f) > 0.01f);
}

TEST_CASE("locking blooms glow and text, snaps toward face-on", "[anim]")
{
    scene_t sc = {0};
    sc.state = ST_LOCKING;
    // Start from a tilted matrix.
    mat3_identity(sc.pyr_rot);
    mat3_rotate_y(sc.pyr_rot, 0.6f);

    scene_t end = sc;
    anim_apply(&sc, 1, 16);              // start of lock
    anim_apply(&end, LOCK_MS, 16);       // end of lock

    TEST_ASSERT_TRUE(end.pyr_glow > sc.pyr_glow);
    TEST_ASSERT_TRUE(end.text_alpha > sc.text_alpha);
    TEST_ASSERT_EQUAL_UINT8(255, end.pyr_alpha);
    TEST_ASSERT_EQUAL_UINT8(0, end.murk);
    // By the end the matrix is re-orthonormalized toward identity: the columns
    // remain unit length (orthonormal), so |col0| ~ 1.
    float c0len = sqrtf(end.pyr_rot[0] * end.pyr_rot[0]
                      + end.pyr_rot[1] * end.pyr_rot[1]
                      + end.pyr_rot[2] * end.pyr_rot[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, c0len);
}

TEST_CASE("showing holds centered, fully present, glowing, identity rotation", "[anim]")
{
    scene_t sc = {0};
    sc.state = ST_SHOWING;
    anim_apply(&sc, 100, 16);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, (float)DISP_CY, sc.pyr_cy);
    TEST_ASSERT_EQUAL_UINT8(255, sc.pyr_alpha);
    TEST_ASSERT_EQUAL_UINT8(255, sc.text_alpha);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, sc.pyr_glow);
    // identity rotation held
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, sc.pyr_rot[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, sc.pyr_rot[4]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, sc.pyr_rot[8]);
}

// ---------------------------------------------------------------------------
// particles (unchanged behavior)
// ---------------------------------------------------------------------------

TEST_CASE("particles seed within display bounds", "[anim]")
{
    scene_t sc = {0};
    anim_seed_particles(&sc);
    TEST_ASSERT_EQUAL_INT(PARTICLE_COUNT, sc.particle_count);
    for (int i = 0; i < sc.particle_count; i++) {
        TEST_ASSERT_TRUE(sc.particles[i].x >= 0 && sc.particles[i].x < DISP_W);
        TEST_ASSERT_TRUE(sc.particles[i].y >= 0 && sc.particles[i].y < DISP_H);
    }
}

TEST_CASE("particle step moves by velocity over dt", "[anim]")
{
    scene_t sc = {0};
    sc.particle_count = 1;
    sc.particles[0].x = 100;
    sc.particles[0].y = 100;
    sc.particles[0].vx = 10;
    sc.particles[0].vy = -20;
    sc.particles[0].alpha = 80;
    anim_step_particles(&sc, 1000, ST_IDLE);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 110.0f, sc.particles[0].x);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.0f, sc.particles[0].y);
}

TEST_CASE("particle step wraps across an edge and stays in bounds", "[anim]")
{
    scene_t sc = {0};
    sc.particle_count = 1;
    sc.particles[0].x = 5;
    sc.particles[0].y = 5;
    sc.particles[0].vx = -10;
    sc.particles[0].vy = -10;
    sc.particles[0].alpha = 80;
    anim_step_particles(&sc, 1000, ST_IDLE);
    TEST_ASSERT_TRUE(sc.particles[0].x >= 0 && sc.particles[0].x < DISP_W);
    TEST_ASSERT_TRUE(sc.particles[0].y >= 0 && sc.particles[0].y < DISP_H);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, (float)(DISP_W - 5), sc.particles[0].x);
}
