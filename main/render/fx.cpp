#include "fx.h"
#include "../config.h"
#include "../gfx/color.h"
#include "../gfx/draw.h"
#include <math.h>
#include <stddef.h>

// Radius (px) out to which the blue glow halo around the die fades to nothing.
#define HALO_RADIUS   220.0f

// Background: near-black liquid with a soft blue glow halo centered on the die
// (reference look). `murk` (0..255) drives how strong the halo is — clouded
// during the shake/think, clearing as the answer locks in. No full-screen fog.
void fx_draw_background(fb_t *fb, uint8_t murk)
{
    uint16_t bg_center = rgb565(COL_BG_CTR_R, COL_BG_CTR_G, COL_BG_CTR_B);
    uint16_t bg_edge = rgb565(COL_BG_EDGE_R, COL_BG_EDGE_G, COL_BG_EDGE_B);

    // Base near-black radial gradient (cheap, one pass).
    draw_radial_gradient(fb, DISP_CX, DISP_CY, DISP_RADIUS, bg_center, bg_edge);

    // Additive blue halo around the die center. Brightest in a ring near the
    // die, fading to nothing by HALO_RADIUS. Strength scales with murk so the
    // liquid looks lit/agitated while thinking and calm (faint) when showing.
    int hr = (int)(COL_HALO_R);
    int hg = (int)(COL_HALO_G);
    int hb = (int)(COL_HALO_B);
    float murk_f = (float)murk / 255.0f;
    float base_glow = 0.35f + 0.65f * murk_f;   // always a little halo, more while clouded
    float inv_halo = 1.0f / HALO_RADIUS;

    for (int y = 0; y < fb->h; y++) {
        uint16_t *prow = fb->px + (size_t)y * fb->w;
        float dy = (float)(y - DISP_CY);
        for (int x = 0; x < fb->w; x++) {
            float dx = (float)(x - DISP_CX);
            float d = sqrtf(dx * dx + dy * dy) * inv_halo;
            if (d >= 1.0f) {
                continue;
            }
            // Smooth falloff: 1 at center -> 0 at HALO_RADIUS (raised for a
            // softer, rounder glow).
            float f = 1.0f - d;
            f = f * f;
            uint8_t a = (uint8_t)(base_glow * f * 255.0f);
            if (a == 0) {
                continue;
            }
            uint16_t halo = rgb565((uint8_t)hr, (uint8_t)hg, (uint8_t)hb);
            prow[x] = rgb565_blend(prow[x], halo, a);
        }
    }
}

void fx_draw_particles(fb_t *fb, const scene_t *sc)
{
    for (int i = 0; i < sc->particle_count; i++) {
        const particle_t *p = &sc->particles[i];
        draw_particle(fb, (int)p->x, (int)p->y, p->alpha);
    }
}

void fx_draw_glass_arc(fb_t *fb)
{
    draw_glass_arc(fb, DISP_CX, DISP_CY, DISP_RADIUS);
}

void fx_draw_circle_clip(fb_t *fb)
{
    draw_circle_clip(fb, DISP_CX, DISP_CY, DISP_RADIUS);
}
