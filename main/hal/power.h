#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  power_init(void);                 // AXP2101 bring-up; 0 on success, -1 if absent
void power_display_sleep(bool sleep);  // true = panel off, false = on
int  power_battery_percent(void);      // fuel-gauge SoC 1-100, or -1 if unavailable/unsettled
int  power_battery_mv(void);           // VBAT ADC in mV, or -1 if unavailable
bool power_is_charging(void);          // true if actively charging or charge-done
bool power_is_vbus_present(void);      // true if external 5V (USB) is connected
bool power_is_battery_present(void);   // true if a cell is detected on the BAT pin
void power_shutdown(void);             // soft power-off (PMIC); returns if no PMIC

#ifdef __cplusplus
}
#endif
