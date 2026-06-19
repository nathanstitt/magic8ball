#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int  mic_init(void);   // 0 on success, -1 on failure (ES7210 over I2S/I2C)

// Pull up to `max` int16 mono samples captured since the last call. Returns the
// count actually copied (0 if none ready). Non-blocking.
size_t mic_read(int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif
