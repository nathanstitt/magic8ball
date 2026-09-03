#include "power.h"
#include "display.h"
#include "config.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

extern "C" {
#include "axp2101.h"
#include "axp2101_registers.h"
}

static const char *TAG = "power";

// bsp_i2c_get_handle() is declared only in the LVGL-pulling umbrella header, so
// forward-declare it (same trick as imu.cpp / touch.cpp) to stay LVGL-free.
extern "C" i2c_master_bus_handle_t bsp_i2c_get_handle(void);

static bool s_ok = false;   // AXP2101 present + configured

int power_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "BSP I2C bus not available");
        return -1;
    }

    // NOTE: the component's axp2101_init() also forces GPIO21 as its IRQ input
    // (hardcoded for the author's ESP32-P4/C5 board). That pin isn't the AXP2101
    // INT on this Waveshare board, so bind the device directly via the lower-level
    // init and skip the GPIO step -- we poll status, we don't use the IRQ line.
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = AXP2101_SLAVE_ADDRESS;   // 0x34
    dev_cfg.scl_speed_hz    = 400000;
    if (axp2101_init_i2c(bus, &dev_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 i2c bind failed");
        return -1;
    }
    if (axp2101_check_chip_id() != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 not found at 0x34");
        return -1;
    }

    // Charge config for the connected cell (<=500 mAh): 300mA CC, 4.2V CV.
    // 300mA stays <=1C so we never overdrive a small LiPo. termination current
    // 25mA ends the charge cleanly at full.
    esp_err_t err = ESP_OK;
    err |= axp2101_set_charge_current((axp2101_charge_current_t)BATT_CHARGE_CURRENT);
    err |= axp2101_set_charge_voltage((axp2101_charge_voltage_t)BATT_CHARGE_VOLTAGE);
    err |= axp2101_set_termination_current(AXP2101_TERM_CURR_25MA, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 charge config partial (err=0x%x)", err);
    }

    // Turn on the VBAT ADC + fuel gauge so power_battery_percent() reads valid
    // data (reg 0x18 bit7 = gauge enable; this call also enables VBAT/VBUS/VSYS
    // ADC channels).
    if (axp2101_enable_pmu_adc_channels() != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 ADC/gauge enable failed");
    }

    s_ok = true;
    ESP_LOGI(TAG, "AXP2101 ready: charge 300mA @ 4.2V, fuel gauge on (%d%%)",
             power_battery_percent());
    return 0;
}

int power_battery_percent(void)
{
    if (!s_ok) {
        return -1;
    }
    // Fuel-gauge state-of-charge, reg 0xA4 (0-100).
    uint8_t pct = 0;
    if (axp2101_get_battery_percentage(&pct) != ESP_OK) {
        return -1;
    }
    // 0 and >100 both mean "no usable reading", not "empty". The gauge reports 0
    // for the first seconds after power-on while it settles, and 0xFF when no
    // battery is attached -- treating either as a real SoC would shut the board
    // down on a healthy pack. A genuinely flat cell is caught by power_battery_mv()
    // and, failing that, by the AXP2101's own undervoltage cutoff.
    if (pct == 0 || pct > 100) {
        return -1;
    }
    return pct;
}

int power_battery_mv(void)
{
    if (!s_ok) {
        return -1;
    }
    // VBAT ADC, regs 0x34/0x35: 12-bit result at 1 LSB = 1mV on the AXP2101.
    // VBAT_H holds the high 4 bits (right-aligned in the byte), VBAT_L the low 8.
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (axp2101_get_vbat_h(&hi) != ESP_OK) {
        return -1;
    }
    if (axp2101_get_vbat_l(&lo) != ESP_OK) {
        return -1;
    }
    int mv = (int)(((uint16_t)(hi & 0x0F) << 8) | lo);
    if (mv == 0) {
        return -1;   // ADC not converted yet
    }
    return mv;
}

bool power_is_charging(void)
{
    if (!s_ok) {
        return false;
    }
    axp2101_batt_status_t st;
    if (axp2101_get_battery_status(&st) != ESP_OK) {
        return false;
    }
    return st == AXP2101_BATT_CHARGING || st == AXP2101_BATT_CHARGED;
}

bool power_is_vbus_present(void)
{
    if (!s_ok) {
        return false;
    }
    // Status 1 (0x00) bit5 = VBUS good. This is the right question for "are we on
    // external power" -- power_is_charging() reads the *charge phase*, which reads
    // idle on a full or nearly-full pack even with USB plugged in.
    uint8_t st1 = 0;
    if (axp2101_get_pmu_status_1(&st1) != ESP_OK) {
        return false;
    }
    return (st1 & (1 << 5)) != 0;
}

bool power_is_battery_present(void)
{
    if (!s_ok) {
        return false;
    }
    uint8_t st1 = 0;
    if (axp2101_get_pmu_status_1(&st1) != ESP_OK) {
        return false;
    }
    return (st1 & (1 << 3)) != 0;   // bit3 = battery present
}

void power_shutdown(void)
{
    if (!s_ok) {
        return;
    }
    ESP_LOGW(TAG, "AXP2101 soft power off");
    // Soft power-off via PWROFF_EN (reg 0x22 bit0): drops the regulators but leaves
    // the PMIC's own power-on paths armed, so a PWRON press or a VBUS insert brings
    // the board back up.
    //
    // Deliberately NOT the BATFET force-off (reg 0x12 bit0) that used to live here:
    // that latches the battery FET open and the latch survives VBUS insertion, so
    // the board stays dark on re-plug and needs a ~6s forced PWRON (or pulling the
    // cell) to recover. That is the "never starts back up" failure this replaces.
    uint8_t pwroff = 0;
    if (axp2101_get_pwroff_en(&pwroff) == ESP_OK) {
        pwroff |= (1 << 0);
        axp2101_set_pwroff_en(pwroff);
    }
}

void power_display_sleep(bool sleep)
{
    display_set_brightness(sleep ? 0 : 255);
}
