#pragma once
#include <stdint.h>

// Pack 8-bit RGB into RGB565.
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Alpha-blend src over dst. alpha: 0 = dst, 255 = src. Channels blended in 565 space.
static inline uint16_t rgb565_blend(uint16_t dst, uint16_t src, uint8_t alpha)
{
    uint32_t a = alpha;
    uint32_t ia = 255 - a;

    uint32_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    uint32_t sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;

    uint32_t r = (sr * a + dr * ia + 127) / 255;
    uint32_t g = (sg * a + dg * ia + 127) / 255;
    uint32_t b = (sb * a + db * ia + 127) / 255;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Scale all channels by intensity [0..1] (clamped). For diffuse darkening only.
static inline uint16_t rgb565_scale(uint16_t color, float intensity)
{
    if (intensity < 0.0f) {
        intensity = 0.0f;
    }
    if (intensity > 1.0f) {
        intensity = 1.0f;
    }
    uint32_t cr = (color >> 11) & 0x1F;
    uint32_t cg = (color >> 5) & 0x3F;
    uint32_t cb = color & 0x1F;
    uint32_t r = (uint32_t)(cr * intensity + 0.5f);
    uint32_t g = (uint32_t)(cg * intensity + 0.5f);
    uint32_t b = (uint32_t)(cb * intensity + 0.5f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Add brightness (boost is an additive amount, typically 0..~3), clamping each
// channel to its 5/6-bit max. For glow/specular brightening only.
// boost is a fraction of full-channel range: add boost*31 to 5-bit channels and
// boost*63 to the 6-bit channel, then clamp (boost=1.0 adds full white).
static inline uint16_t rgb565_add(uint16_t color, float boost)
{
    if (boost < 0.0f) {
        boost = 0.0f;
    }
    int cr = (color >> 11) & 0x1F;
    int cg = (color >> 5) & 0x3F;
    int cb = color & 0x1F;
    int r = cr + (int)(boost * 31.0f + 0.5f);
    int g = cg + (int)(boost * 63.0f + 0.5f);
    int b = cb + (int)(boost * 31.0f + 0.5f);
    if (r > 0x1F) {
        r = 0x1F;
    }
    if (g > 0x3F) {
        g = 0x3F;
    }
    if (b > 0x1F) {
        b = 0x1F;
    }
    return (uint16_t)((r << 11) | (g << 5) | b);
}
