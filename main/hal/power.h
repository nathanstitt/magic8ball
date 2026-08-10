#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  power_init(void);                 // AXP2101 bring-up; 0 on success, -1 if absent
void power_display_sleep(bool sleep);  // true = panel off, false = on
int  power_battery_percent(void);      // fuel-gauge SoC 0-100, or -1 if unavailable
bool power_is_charging(void);          // true if on USB / charging or charged
void power_shutdown(void);             // cut battery rail (PMIC off); returns if no PMIC

#ifdef __cplusplus
}
#endif
