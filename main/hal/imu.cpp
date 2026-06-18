#include "imu.h"
#include "config.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <math.h>
#include <stdint.h>

// bsp_i2c_get_handle() is declared only in the LVGL-pulling umbrella header,
// so forward-declare it here instead of including bsp/esp-bsp.h.
extern "C" i2c_master_bus_handle_t bsp_i2c_get_handle(void);

static const char *TAG = "imu";
static bool s_ok;
static i2c_master_dev_handle_t s_dev;

#define I2C_HZ          400000

#define QMI8658_ADDR_PRIMARY   0x6B
#define QMI8658_ADDR_ALT       0x6A
#define QMI8658_WHOAMI_VAL     0x05

#define REG_WHOAMI  0x00
#define REG_CTRL1   0x02
#define REG_CTRL2   0x03
#define REG_CTRL7   0x08
#define REG_AX_L    0x35

static esp_err_t imu_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, 100);
}

static esp_err_t imu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

int imu_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "BSP I2C bus not available");
        return -1;
    }

    uint8_t addr = QMI8658_ADDR_PRIMARY;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = I2C_HZ;
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device 0x%02X failed: %s", addr, esp_err_to_name(ret));
        return -1;
    }

    uint8_t who = 0;
    ret = imu_read_reg(REG_WHOAMI, &who, 1);
    if (ret != ESP_OK || who != QMI8658_WHOAMI_VAL) {
        // Try alternate address
        i2c_master_bus_rm_device(s_dev);
        addr = QMI8658_ADDR_ALT;
        dev_cfg.device_address = addr;
        ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "add device 0x%02X failed", addr);
            return -1;
        }
        ret = imu_read_reg(REG_WHOAMI, &who, 1);
        if (ret != ESP_OK || who != QMI8658_WHOAMI_VAL) {
            ESP_LOGE(TAG, "QMI8658 WHO_AM_I mismatch: got 0x%02X (expected 0x%02X)", who, QMI8658_WHOAMI_VAL);
            return -1;
        }
    }

    // Configure: address auto-increment, ±2g @ ~125 Hz, accel-only
    imu_write_reg(REG_CTRL1, 0x40);
    imu_write_reg(REG_CTRL2, 0x06);
    imu_write_reg(REG_CTRL7, 0x01);

    s_ok = true;
    ESP_LOGI(TAG, "QMI8658 at 0x%02X ready", addr);
    return 0;
}

// Read the three accel axes; returns false on I2C error. Raw LSB (±2g,
// 16384 LSB/g). out_sumsq = ax^2+ay^2+az^2 (fits in int64; max ~3.2e9).
static bool imu_read_accel(int64_t *out_sumsq)
{
    if (!s_ok) {
        return false;
    }
    uint8_t b[6];
    if (imu_read_reg(REG_AX_L, b, 6) != ESP_OK) {
        return false;
    }
    int16_t ax = (int16_t)((b[1] << 8) | b[0]);
    int16_t ay = (int16_t)((b[3] << 8) | b[2]);
    int16_t az = (int16_t)((b[5] << 8) | b[4]);
    *out_sumsq = (int64_t)ax * ax + (int64_t)ay * ay + (int64_t)az * az;
    return true;
}

int imu_accel_magnitude_mg(void)
{
    int64_t sumsq = 0;
    if (!imu_read_accel(&sumsq)) {
        return 0;
    }
    // ±2g, 16384 LSB/g → scale to mg (x1000).
    float mag = sqrtf((float)sumsq);
    return (int)(mag * 1000.0f / 16384.0f);
}

bool imu_is_shaking(void)
{
    // Hot path (polled every tick): compare SQUARED magnitude against the
    // squared threshold so we avoid sqrt entirely.
    //   mag_mg > T  <=>  sumsq * (1000/16384)^2 > T^2  <=>  sumsq > T^2 * (16384/1000)^2
    int64_t sumsq = 0;
    if (!imu_read_accel(&sumsq)) {
        return false;
    }
    static const float LSB_PER_MG = 16384.0f / 1000.0f;
    int64_t thresh_sq = (int64_t)((float)SHAKE_THRESHOLD_MG * SHAKE_THRESHOLD_MG
                                  * LSB_PER_MG * LSB_PER_MG);
    return sumsq > thresh_sq;
}
