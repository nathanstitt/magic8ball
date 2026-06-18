// =============================================================================
// pyramid.cpp — the glowing answer triangle (Magic 8 Ball).
//
// Draws a clean, symmetric apex-DOWN triangle (the classic die face) filled
// with the radial blue gradient, surrounded by a soft outward bloom into the
// liquid. A gentle 3D wobble (from scene.pyr_rot) leans the triangle to reveal
// depth as it rises, settling perfectly symmetric when locked. The triangle is
// 3 coplanar vertices, so the locked pose is always a true symmetric triangle
// (unlike a real tetrahedron face, whose corners project unevenly).
//
// glm is used only for the small vertex rotation. Pure C++ (gnu++17,
// -fno-exceptions -fno-rtti). No LVGL, no heap allocation.
// =============================================================================

#include <glm/glm.hpp>

#include <math.h>
#include <stdint.h>

#include "pyramid.h"
#include "../config.h"
#include "../gfx/color.h"
#include "../gfx/draw.h"

// Apex-DOWN equilateral triangle, circumradius 1, in the z=0 plane.
//   v0 = apex (bottom, +y is down on screen), v1 = top-right, v2 = top-left.
static const glm::vec3 TRI_VERTS[3] = {
    {  0.0f,      1.0f,  0.0f },   // apex (bottom)
    {  0.86603f, -0.5f,  0.0f },   // top-right
    { -0.86603f, -0.5f,  0.0f },   // top-left
};

void pyramid_render(fb_t *fb, const scene_t *sc)
{
    if (fb == nullptr || sc == nullptr) {
        return;
    }

    // Wobble rotation from the scene (identity when locked -> symmetric).
    glm::mat3 R(sc->pyr_rot[0], sc->pyr_rot[1], sc->pyr_rot[2],
                sc->pyr_rot[3], sc->pyr_rot[4], sc->pyr_rot[5],
                sc->pyr_rot[6], sc->pyr_rot[7], sc->pyr_rot[8]);

    float radius = PYRAMID_RADIUS * sc->pyr_scale;
    float focal = PYRAMID_FOCAL;

    int sx[3];
    int sy[3];
    for (int i = 0; i < 3; i++) {
        glm::vec3 w = R * (TRI_VERTS[i] * radius);
        float denom = focal - w.z;
        if (denom <= 1.0f) {
            denom = 1.0f;
        }
        float p = focal / denom;   // perspective: +z toward viewer
        sx[i] = (int)(sc->pyr_cx + w.x * p);
        sy[i] = (int)(sc->pyr_cy + w.y * p);
    }

    uint16_t center = rgb565(COL_TRI_CTR_R, COL_TRI_CTR_G, COL_TRI_CTR_B);
    uint16_t edge = rgb565(COL_TRI_EDGE_R, COL_TRI_EDGE_G, COL_TRI_EDGE_B);
    uint16_t bevel = rgb565(COL_TRI_BEVEL_R, COL_TRI_BEVEL_G, COL_TRI_BEVEL_B);
    uint16_t glow = rgb565(COL_GLOW_R, COL_GLOW_G, COL_GLOW_B);

    // --- Smooth outer bloom: the die's light bleeding into the liquid. Driven
    // by pyr_glow (0 during the rise -> 1 when locked) so the glow is SKIPPED
    // entirely while tumbling (keeping those frames cheap) and blooms outward as
    // the answer locks in. Reach and alpha grow with sqrt(pyr_glow) so the glow
    // is a visible halo as soon as it starts (a linear ramp made the first
    // bits a near-invisible 4px sliver). ---
    if (sc->pyr_glow > 0.01f) {
        float g = sqrtf(sc->pyr_glow);   // front-loaded ramp: visible early
        float reach = GLOW_DIST * g;
        if (reach < 1.0f) {
            reach = 1.0f;
        }
        uint8_t glow_peak = (uint8_t)((float)GLOW_PEAK_ALPHA * g
                                      * ((float)sc->pyr_alpha / 255.0f));
        draw_triangle_glow(fb,
                           sx[0], sy[0], sx[1], sy[1], sx[2], sy[2],
                           glow, reach, glow_peak);
    }

    // --- The die itself: radial gradient + soft bevel. ---
    draw_triangle_gradient(fb,
                           sx[0], sy[0], sx[1], sy[1], sx[2], sy[2],
                           center, edge, bevel, GRAD_BEVEL_STRENGTH, sc->pyr_alpha);
}
