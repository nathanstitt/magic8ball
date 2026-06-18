#include "display.h"
#include "bsp/display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "config.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static uint16_t                 *s_fb[2] = {NULL, NULL};  // double buffer in PSRAM
static int                       s_cur = 0;
static SemaphoreHandle_t         s_flush_done = NULL;

// DMA-done callback (runs in ISR context). Returns whether a higher-priority
// task was woken, per the esp_lcd_panel_io_color_trans_done_cb_t contract.
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &high_task_awoken);
    return high_task_awoken == pdTRUE;
}

int display_init(void)
{
    bsp_display_config_t cfg = { .max_transfer_sz = DISP_W * DISP_H * 2 };
    if (bsp_display_new(&cfg, &s_panel, &s_io) != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new failed");
        return -1;
    }

    s_flush_done = xSemaphoreCreateBinary();
    if (!s_flush_done) {
        ESP_LOGE(TAG, "semaphore create failed");
        return -1;
    }

    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    if (esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "register event callbacks failed");
        return -1;
    }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_disp_on_off(s_panel, true);
    bsp_display_backlight_on();

    s_fb[0] = (uint16_t *)heap_caps_malloc(DISP_W * DISP_H * 2, MALLOC_CAP_SPIRAM);
    s_fb[1] = (uint16_t *)heap_caps_malloc(DISP_W * DISP_H * 2, MALLOC_CAP_SPIRAM);
    if (!s_fb[0] || !s_fb[1]) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return -1;
    }

    return 0;
}

uint16_t *display_back_buffer(void)
{
    return s_fb[s_cur ^ 1];  // render into the buffer NOT currently shown
}

void display_flush_and_swap(void)
{
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_fb[s_cur ^ 1]);
    if (ret != ESP_OK) {
        // No transfer was started, so no callback will fire. Log and still
        // swap so rendering never freezes.
        ESP_LOGW(TAG, "draw_bitmap failed: %s", esp_err_to_name(ret));
        s_cur ^= 1;
        return;
    }
    // draw_bitmap is async over QSPI: wait for DMA completion before swapping.
    // Use a 100ms timeout and proceed regardless so a missed callback never hangs.
    if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "flush DMA wait timed out");
    }
    s_cur ^= 1;
}

void display_set_brightness(uint8_t level)
{
    bsp_display_brightness_set(level * 100 / 255);
}
