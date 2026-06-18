#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *px;   // row-major RGB565, length w*h, owned elsewhere
    int w;
    int h;
} fb_t;

void fb_init(fb_t *fb, uint16_t *backing, int w, int h);
void fb_clear(fb_t *fb, uint16_t color);
void fb_set_px(fb_t *fb, int x, int y, uint16_t color);
// Returns the pixel at (x,y), or 0 (black) for out-of-bounds reads.
uint16_t fb_get_px(const fb_t *fb, int x, int y);

#ifdef __cplusplus
}
#endif
