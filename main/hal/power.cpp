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
    if (pct > 100) {
        return -1;   // gauge not yet settled / no battery
    }
    return pct;
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

void power_shutdown(void)
{
    if (!s_ok) {
        return;
    }
    ESP_LOGW(TAG, "AXP2101 power off (BATFET force-off)");
    // Force BATFET off (reg 0x12 bit0): cuts the battery rail -> whole board goes
    // dark. Re-plugging USB (VBUS) turns the AXP2101 back on and reboots.
    uint8_t batfet = 0;
    if (axp2101_get_batfet_ctrl(&batfet) == ESP_OK) {
        batfet |= (1 << 0);   // BIT0 = force BATFET off
        axp2101_set_batfet_ctrl(batfet);
    }
}

void power_display_sleep(bool sleep)
{
    display_set_brightness(sleep ? 0 : 255);
}
