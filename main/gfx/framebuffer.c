#include "framebuffer.h"

void fb_init(fb_t *fb, uint16_t *backing, int w, int h)
{
    fb->px = backing;
    fb->w = w;
    fb->h = h;
}

void fb_clear(fb_t *fb, uint16_t color)
{
    int n = fb->w * fb->h;
    for (int i = 0; i < n; i++) {
        fb->px[i] = color;
    }
}

void fb_set_px(fb_t *fb, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) {
        return;
    }
    fb->px[y * fb->w + x] = color;
}

uint16_t fb_get_px(const fb_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) {
        return 0;
    }
    return fb->px[y * fb->w + x];
}
