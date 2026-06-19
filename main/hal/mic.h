#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int  mic_init(void);   // 0 on success, -1 on failure (board mic codec over I2S/I2C)

// Pull `max` int16 mono samples into `out`. BLOCKS until that many are captured
// (~16 ms for a full model window at 16 kHz) or the codec read times out.
// Returns `max` on success, 0 on failure.
size_t mic_read(int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif
