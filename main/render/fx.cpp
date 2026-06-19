#include "fx.h"
#include "../config.h"
#include "../gfx/color.h"
#include "../gfx/draw.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

// Radius (px) out to which the blue glow halo around the die fades to nothing.
#define HALO_RADIUS   220.0f

static const char *FX_TAG = "fx";

// The complete background (radial gradient + a fixed-strength blue halo around
// the die) is baked ONCE into s_bg at fx_init(). The per-frame background is
// then just a memcpy — no sqrt, no per-pixel blend (it was costing ~0.5s/frame
// of sqrt, and a full-screen blend even after that).
static uint16_t *s_bg = NULL;

// Fixed halo strength (0..256 fixed point) baked into the static background.
#define HALO_BAKE_STRENGTH  200

void fx_init(void)
{
    size_t npx = (size_t)DISP_W * DISP_H;
    s_bg = (uint16_t *)heap_caps_malloc(npx * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_bg) {
        ESP_LOGE(FX_TAG, "fx precompute alloc failed");
        return;
    }
    uint16_t halo_color = rgb565(COL_HALO_R, COL_HALO_G, COL_HALO_B);

    int cr = (COL_BG_CTR_R >> 3), cg = (COL_BG_CTR_G >> 2), cb = (COL_BG_CTR_B >> 3);
    int er = (COL_BG_EDGE_R >> 3), eg = (COL_BG_EDGE_G >> 2), eb = (COL_BG_EDGE_B >> 3);
    float inv_r = 1.0f / (float)DISP_RADIUS;
    float inv_halo = 1.0f / HALO_RADIUS;
    static const int bayer[2][2] = {{0, 2}, {3, 1}};

    for (int y = 0; y < DISP_H; y++) {
        float dy = (float)(y - DISP_CY);
        for (int x = 0; x < DISP_W; x++) {
            float dx = (float)(x - DISP_CX);
            float dist = sqrtf(dx * dx + dy * dy);
            size_t i = (size_t)y * DISP_W + x;

            // Base radial gradient.
            float d = dist * inv_r;
            if (d > 1.0f) {
                d = 1.0f;
            }
            float dith = (float)(bayer[y & 1][x & 1] - 1) * 0.06f;
            float t = d + dith;
            if (t < 0.0f) {
                t = 0.0f;
            }
            if (t > 1.0f) {
                t = 1.0f;
            }
            int r = (int)(cr + (er - cr) * t + 0.5f);
            int g = (int)(cg + (eg - cg) * t + 0.5f);
            int b = (int)(cb + (eb - cb) * t + 0.5f);
            uint16_t px = (uint16_t)((r << 11) | (g << 5) | b);

            // Bake the halo in at a fixed strength.
            float hd = dist * inv_halo;
            if (hd < 1.0f) {
                float f = 1.0f - hd;
                f = f * f;
                uint8_t a = (uint8_t)((int)(f * 255.0f) * HALO_BAKE_STRENGTH >> 8);
                if (a > 0) {
                    px = rgb565_blend(px, halo_color, a);
                }
            }
            // The bake math above is native RGB565; store s_bg in panel byte order
            // so the per-frame fx_draw_background memcpy needs no swap.
            s_bg[i] = fb_pack(px);
        }
    }
    ESP_LOGI(FX_TAG, "fx background baked");
}

// Background: a fast memcpy of the fully precomputed gradient+halo. `murk` is
// no longer used (the halo is baked at a fixed strength for performance).
void fx_draw_background(fb_t *fb, uint8_t murk)
{
    (void)murk;
    size_t npx = (size_t)fb->w * fb->h;

    if (!s_bg) {
        uint16_t edge = fb_pack(rgb565(COL_BG_EDGE_R, COL_BG_EDGE_G, COL_BG_EDGE_B));
        for (size_t i = 0; i < npx; i++) {
            fb->px[i] = edge;
        }
        return;
    }
    memcpy(fb->px, s_bg, npx * sizeof(uint16_t));
}


void fx_draw_particles(fb_t *fb, const scene_t *sc)
{
    if (sc->star_fade > 0) {
        // Starfield: brighter blue stars, fading out (star_fade) as the answer
        // rises. Half the stars (odd index) are large (4x4), half small (2x2) for
        // size variety. Each star's stored (faint) alpha maps onto the STAR_ALPHA
        // range so they twinkle, then is scaled by star_fade for the dissolve.
        // White + faster while the Gemini request is in flight; blue while listening.
        uint16_t star = sc->submitting
            ? rgb565(COL_STAR_SUBMIT_R, COL_STAR_SUBMIT_G, COL_STAR_SUBMIT_B)
            : rgb565(COL_STAR_R, COL_STAR_G, COL_STAR_B);
        for (int i = 0; i < sc->particle_count; i++) {
            const particle_t *p = &sc->particles[i];
            int base = STAR_ALPHA_MIN +
                ((int)p->alpha * (STAR_ALPHA_MAX - STAR_ALPHA_MIN)) / 255;
            uint8_t a = (uint8_t)(base * (int)sc->star_fade / 255);
            int size = (i & 1) ? STAR_SIZE_BIG : STAR_SIZE;
            int x0 = (int)p->x - size / 2;
            int y0 = (int)p->y - size / 2;
            for (int sy = 0; sy < size; sy++) {
                for (int sx = 0; sx < size; sx++) {
                    draw_particle_color(fb, x0 + sx, y0 + sy, a, star);
                }
            }
        }
        return;
    }
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
