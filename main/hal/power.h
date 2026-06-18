#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  power_init(void);                 // AXP2101 bring-up; 0 on success
void power_display_sleep(bool sleep);  // true = panel off, false = on

#ifdef __cplusplus
}
#endif
