// =============================================================================
// pyramid.cpp — 3D engine core (Magic 8 Ball).
//
// This is the ONLY translation unit allowed to use glm. It consumes the
// scene_t's pyramid fields and emits lit, perspective-projected, painter-sorted
// triangles via draw_triangle_lit. Pure C++ (gnu++17, -fno-exceptions
// -fno-rtti). No LVGL, no heap allocation, no draw call other than
// draw_triangle_lit.
// =============================================================================

#include <glm/glm.hpp>

#include <math.h>
#include <stdint.h>

#include "pyramid.h"
#include "../config.h"
#include "../gfx/color.h"
#include "../gfx/draw.h"

// --- Tetrahedron geometry (object space, unit circumradius) ------------------
// 4 vertices of a regular tetrahedron, circumradius = 1.
static const glm::vec3 TETRA_VERTS[4] = {
    {  0.0f,       1.0f,      0.0f     },  // apex (top)
    {  0.9428f,   -0.3333f,   0.0f     },  // base front-right
    { -0.4714f,   -0.3333f,   0.8165f  },  // base back-left
    { -0.4714f,   -0.3333f,  -0.8165f  },  // base back-right
};

// 4 faces (CCW winding = front-facing toward +Z... see cull note below).
static const int TETRA_FACES[4][3] = {
    { 0, 2, 1 },  // front face  <- answer face, locked toward viewer
    { 0, 1, 3 },  // right face
    { 0, 3, 2 },  // left face
    { 1, 2, 3 },  // base (bottom)
};

// Collected per-visible-face data for the painter's sort.
struct FaceEntry {
    int      a;          // screen-vertex index 0
    int      b;          // screen-vertex index 1
    int      c;          // screen-vertex index 2
    float    avg_z;      // mean world-space z of the three vertices
    uint16_t lit_color;  // diffuse-lit base color
    float    fresnel;    // rim term
    float    spec;       // specular term
};

void pyramid_render(fb_t *fb, const scene_t *sc)
{
    if (fb == nullptr || sc == nullptr) {
        return;
    }

    // 1. Build rotation matrix from pyr_rot[9]. glm::mat3's 9-float ctor is
    //    column-major and pyr_rot is stored column-major (pyr_rot[0..2]=col0,
    //    [3..5]=col1, [6..8]=col2), so they map straight through in order.
    glm::mat3 R(sc->pyr_rot[0], sc->pyr_rot[1], sc->pyr_rot[2],
                sc->pyr_rot[3], sc->pyr_rot[4], sc->pyr_rot[5],
                sc->pyr_rot[6], sc->pyr_rot[7], sc->pyr_rot[8]);

    // 2. Transform vertices to world space (rotate + uniform scale).
    float radius = PYRAMID_RADIUS * sc->pyr_scale;
    glm::vec3 world[4];
    for (int i = 0; i < 4; i++) {
        world[i] = R * (TETRA_VERTS[i] * radius);
    }

    // 3. Perspective project each vertex to screen coords.
    //    w = focal / (focal - z): larger z grows w, so +z is TOWARD the
    //    viewer (a closer vertex projects bigger on screen). Guard the
    //    denominator so a vertex at/behind the projection plane can't blow up.
    glm::vec2 screen[4];
    float focal = PYRAMID_FOCAL;
    for (int i = 0; i < 4; i++) {
        float denom = focal - world[i].z;
        if (denom <= 1.0f) {
            denom = 1.0f;
        }
        float w = focal / denom;
        screen[i].x = sc->pyr_cx + world[i].x * w;
        screen[i].y = sc->pyr_cy + world[i].y * w;
    }

    // Light direction (toward the light), normalized once.
    glm::vec3 L = glm::normalize(
        glm::vec3(PYRAMID_LIGHT_X, PYRAMID_LIGHT_Y, PYRAMID_LIGHT_Z));
    uint16_t base = rgb565(COL_TRI_R, COL_TRI_G, COL_TRI_B);

    // 4. Per face: cull, light. Collect visible faces for sorting.
    FaceEntry entries[4];
    int n = 0;

    for (int f = 0; f < 4; f++) {
        int i0 = TETRA_FACES[f][0];
        int i1 = TETRA_FACES[f][1];
        int i2 = TETRA_FACES[f][2];

        // Face normal in world space.
        glm::vec3 e1 = world[i1] - world[i0];
        glm::vec3 e2 = world[i2] - world[i0];
        glm::vec3 N = glm::normalize(glm::cross(e1, e2));

        // Back-face cull: +z is toward the viewer; front faces have N.z > 0.
        // A normal with N.z <= 0 points away from the viewer -> skip.
        if (N.z <= 0.0f) {
            continue;
        }

        // Diffuse (Lambert) lighting.
        float NdotL = glm::max(glm::dot(N, L), 0.0f);
        float intensity = PYRAMID_AMBIENT + PYRAMID_DIFFUSE * NdotL;

        // Fresnel rim: brighten edges when the face is nearly edge-on to viewer.
        float fresnel = powf(1.0f - fabsf(N.z), FRESNEL_POWER);

        // Specular highlight: sharp bright spot when reflection aims at viewer.
        glm::vec3 refl = glm::reflect(-L, N);
        float spec = powf(glm::max(refl.z, 0.0f), PYRAMID_SPECULAR_POWER);
        spec *= PYRAMID_SPECULAR_INTENSITY;

        // Diffuse darkening only via rgb565_scale.
        uint16_t lit = rgb565_scale(base, intensity);

        // Average world-space z for painter ordering.
        float avg_z = (world[i0].z + world[i1].z + world[i2].z) / 3.0f;

        entries[n].a = i0;
        entries[n].b = i1;
        entries[n].c = i2;
        entries[n].avg_z = avg_z;
        entries[n].lit_color = lit;
        entries[n].fresnel = fresnel;
        entries[n].spec = spec;
        n++;
    }

    // 5. Painter's sort. +z is toward the viewer (see projection above), so
    //    the FARTHEST face has the SMALLEST avg_z and must be drawn FIRST.
    //    Insertion-sort ascending by avg_z (smallest avg_z at index 0, drawn
    //    first; nearest face at the end, drawn last/on top). At most 3 visible
    //    faces after cull, so this is trivial.
    for (int i = 1; i < n; i++) {
        FaceEntry key = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].avg_z > key.avg_z) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }

    // 6. Rasterize visible faces back-to-front (farthest first, nearest last).
    for (int i = 0; i < n; i++) {
        const FaceEntry &e = entries[i];
        draw_triangle_lit(fb,
                          (int)screen[e.a].x, (int)screen[e.a].y,
                          (int)screen[e.b].x, (int)screen[e.b].y,
                          (int)screen[e.c].x, (int)screen[e.c].y,
                          e.lit_color, sc->pyr_alpha,
                          e.fresnel, e.spec, sc->pyr_glow);
    }
}
