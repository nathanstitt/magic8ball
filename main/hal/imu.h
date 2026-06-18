#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  imu_init(void);                 // 0 on success, -1 on failure
// Returns current total acceleration magnitude in milli-g (1000 = 1g).
int  imu_accel_magnitude_mg(void);
// Convenience: true if magnitude exceeds the shake threshold this read.
bool imu_is_shaking(void);

#ifdef __cplusplus
}
#endif
