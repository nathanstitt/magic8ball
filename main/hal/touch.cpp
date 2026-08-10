#include "touch.h"
#include "config.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

// We initialize the CST9217 directly instead of calling the BSP's
// bsp_touch_new(): in BSP 3.0.0 that function asserts cfg != NULL and reads
// cfg->touch_flags from a bsp_display_cfg_t, a struct defined only in the
// LVGL-pulling umbrella header. Driving the touch controller ourselves keeps
// the firmware LVGL-free and gives us the same result (no swap/mirror).
//
// The shared I2C bus is already created by display init (bsp_display_new ->
// bsp_i2c_init), so we just fetch its handle. bsp_i2c_get_handle() is declared
// only in the umbrella header, so we forward-declare it here.
extern "C" i2c_master_bus_handle_t bsp_i2c_get_handle(void);

// Board touch pins (from bsp/esp32_s3_touch_amoled_1_75.h): RST shared with LCD.
#define TOUCH_RST_GPIO   GPIO_NUM_40
#define TOUCH_INT_GPIO   GPIO_NUM_11
#define TOUCH_I2C_HZ     400000

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp = NULL;
static bool s_was_down = false;   // level at the previous poll (for edge detect)
static bool s_down = false;       // level latched by the last touch_poll()
static bool s_tapped = false;     // fresh-down edge latched by the last touch_poll()

int touch_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not available (display must init first)");
        return -1;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = TOUCH_I2C_HZ;

    esp_lcd_panel_io_handle_t io = NULL;
    if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) {
        ESP_LOGE(TAG, "touch panel IO create failed");
        return -1;
    }

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = DISP_W;
    tp_cfg.y_max = DISP_H;
    tp_cfg.rst_gpio_num = TOUCH_RST_GPIO;
    tp_cfg.int_gpio_num = TOUCH_INT_GPIO;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;

    if (esp_lcd_touch_new_i2c_cst9217(io, &tp_cfg, &s_tp) != ESP_OK || !s_tp) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_cst9217 failed");
        return -1;
    }

    ESP_LOGI(TAG, "CST9217 touch ready");
    return 0;
}

void touch_poll(void)
{
    if (!s_tp) {
        s_down = false;
        s_tapped = false;
        s_was_down = false;
        return;
    }

    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) {
        s_down = false;
        s_tapped = false;
        s_was_down = false;
        return;
    }

    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(s_tp, pts, &cnt, 1);

    bool down = (cnt > 0);
    s_tapped = (down && !s_was_down);
    s_down = down;
    s_was_down = down;
}

bool touch_was_tapped(void)
{
    return s_tapped;
}

bool touch_is_down(void)
{
    return s_down;
}
