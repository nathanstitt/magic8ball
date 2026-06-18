#include "fx.h"
#include "../config.h"
#include "../gfx/color.h"
#include "../gfx/draw.h"

// Dark navy fog blended over the whole frame as `murk` rises. Each channel is
// well below the background, so blending it in strictly darkens every pixel as
// the alpha (murk) increases.
#define FX_MURK_R 2
#define FX_MURK_G 4
#define FX_MURK_B 12

void fx_draw_background(fb_t *fb, uint8_t murk)
{
    uint16_t center = rgb565(COL_BG_CTR_R, COL_BG_CTR_G, COL_BG_CTR_B);
    uint16_t edge = rgb565(COL_BG_EDGE_R, COL_BG_EDGE_G, COL_BG_EDGE_B);

    // One cheap gradient pass.
    draw_radial_gradient(fb, DISP_CX, DISP_CY, DISP_RADIUS, center, edge);

    if (murk == 0) {
        return;
    }

    // One pass: blend a dark fog over the gradient.
    //   out = blend(bg, fog, murk)
    // Since every fog channel is darker than the background, larger murk yields
    // strictly lower channel values -> a darker pixel.
    uint16_t fog = rgb565(FX_MURK_R, FX_MURK_G, FX_MURK_B);
    int count = fb->w * fb->h;
    for (int i = 0; i < count; i++) {
        fb->px[i] = rgb565_blend(fb->px[i], fog, murk);
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
