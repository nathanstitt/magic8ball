#include "touch.h"
#include "esp_log.h"
#include "esp_lcd_touch.h"

// bsp/touch.h declares bsp_touch_new() but its signature references
// bsp_display_cfg_t, a type defined only in the LVGL-pulling umbrella header
// (bsp/esp32_s3_touch_amoled_1_75.h). To stay LVGL-free we forward-declare the
// opaque config struct and the function ourselves and pass NULL for the config
// (the BSP accepts NULL and uses its defaults).
typedef struct bsp_display_cfg_t bsp_display_cfg_t;
extern "C" esp_err_t bsp_touch_new(const bsp_display_cfg_t *cfg, esp_lcd_touch_handle_t *ret_touch);

static const char *TAG = "touch";
static esp_lcd_touch_handle_t s_tp = NULL;
static bool s_was_down = false;

int touch_init(void)
{
    if (bsp_touch_new(NULL, &s_tp) != ESP_OK || !s_tp) {
        ESP_LOGE(TAG, "bsp_touch_new failed");
        return -1;
    }
    ESP_LOGI(TAG, "touch ready");
    return 0;
}

bool touch_was_tapped(void)
{
    if (!s_tp) {
        return false;
    }

    if (esp_lcd_touch_read_data(s_tp) != ESP_OK) {
        s_was_down = false;
        return false;
    }

    esp_lcd_touch_point_data_t pts[1];
    uint8_t cnt = 0;
    esp_lcd_touch_get_data(s_tp, pts, &cnt, 1);

    bool down = (cnt > 0);
    bool tapped = (down && !s_was_down);
    s_was_down = down;
    return tapped;
}
