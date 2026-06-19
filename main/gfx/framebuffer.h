#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Row-major RGB565 stored in the panel's BYTE ORDER (big-endian: the CO5300
    // wants the high byte first). All color math (rgb565(), the blends) works in
    // NATIVE little-endian RGB565; convert at this boundary with fb_pack/fb_unpack.
    // Storing pre-swapped lets the flush DMA straight to the panel with no
    // per-frame byte-swap pass (the old ~25-33ms cost). Length w*h, owned elsewhere.
    uint16_t *px;
    int w;
    int h;
} fb_t;

// Convert a native RGB565 value to/from the framebuffer's stored (panel) byte
// order. The transform is its own inverse (a byte swap), so pack==unpack; two
// names document intent at each call site. 0 (black) is swap-invariant.
static inline uint16_t fb_pack(uint16_t native)   { return __builtin_bswap16(native); }
static inline uint16_t fb_unpack(uint16_t stored) { return __builtin_bswap16(stored); }

void fb_init(fb_t *fb, uint16_t *backing, int w, int h);
void fb_clear(fb_t *fb, uint16_t color);    // color is native; stored swapped
void fb_set_px(fb_t *fb, int x, int y, uint16_t color);   // color is native
// Returns the pixel at (x,y) in NATIVE RGB565, or 0 (black) for out-of-bounds.
uint16_t fb_get_px(const fb_t *fb, int x, int y);

#ifdef __cplusplus
}
#endif
