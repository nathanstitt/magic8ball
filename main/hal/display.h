#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int       display_init(void);              // 0 on success, -1 on failure
uint16_t *display_back_buffer(void);       // the buffer to render into this frame
void      display_flush_and_swap(void);    // flush back buffer over QSPI, wait DMA done, swap
void      display_set_brightness(uint8_t level);  // 0..255

#ifdef __cplusplus
}
#endif
