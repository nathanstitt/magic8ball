#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  touch_init(void);   // 0 on success, -1 on failure
// True exactly once per fresh touch-down (edge-triggered tap).
bool touch_was_tapped(void);

#ifdef __cplusplus
}
#endif
