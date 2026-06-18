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

// Flush the back buffer to the panel in horizontal strips. A full-frame
// (466*466*2 = 434 KB) single QSPI transfer exhausts the SPI DMA descriptor
// pool (ESP_ERR_NO_MEM), so we send DISP_FLUSH_STRIPS bands per frame. Each
// band is a separate async transfer; we wait for its DMA-done callback before
// issuing the next, then swap once the whole frame has shipped.
#define DISP_FLUSH_STRIPS   8
#define STRIP_ROWS          ((DISP_H + DISP_FLUSH_STRIPS - 1) / DISP_FLUSH_STRIPS)

void display_flush_and_swap(void)
{
    uint16_t *buf = s_fb[s_cur ^ 1];

    for (int y = 0; y < DISP_H; y += STRIP_ROWS) {
        int y_end = y + STRIP_ROWS;
        if (y_end > DISP_H) {
            y_end = DISP_H;
        }
        const uint16_t *strip = buf + (size_t)y * DISP_W;
        esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_W, y_end, strip);
        if (ret != ESP_OK) {
            // No transfer started -> no callback will fire for this strip.
            ESP_LOGW(TAG, "draw_bitmap strip y=%d failed: %s", y, esp_err_to_name(ret));
            continue;
        }
        // draw_bitmap is async over QSPI: wait for this strip's DMA completion
        // before issuing the next. 100ms timeout so a missed callback never hangs.
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "flush DMA wait timed out (strip y=%d)", y);
        }
    }

    s_cur ^= 1;
}

void display_set_brightness(uint8_t level)
{
    bsp_display_brightness_set(level * 100 / 255);
}
