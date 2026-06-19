#include "display.h"
#include "bsp/display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "config.h"

// PSRAM data cache line is 32 bytes on the ESP32-S3. Framebuffers and every
// flushed strip must be cache-aligned, and the CPU-side byte swap must be
// flushed (C2M) to PSRAM before the SPI DMA reads it — otherwise stale cache
// lines show up as diagonal corruption streaks.
#define PSRAM_CACHE_ALIGN   32

static const char *TAG = "display";

static esp_lcd_panel_handle_t    s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static uint16_t                 *s_fb[2] = {NULL, NULL};  // double buffer in PSRAM
static int                       s_cur = 0;
static SemaphoreHandle_t         s_flush_done = NULL;

// Two internal-SRAM bounce buffers (one strip each), ping-ponged across the
// pipelined flush: the byte-swap reads a strip from the PSRAM framebuffer and
// writes the swapped bytes into one of these, then DMA transmits FROM here while
// the next strip is swapped into the OTHER buffer. This avoids the in-place
// read-modify-write over slow PSRAM (the measured ~33ms cost) and needs no
// esp_cache_msync (internal SRAM is cache-coherent for DMA). Allocated DMA-capable.
static uint16_t                 *s_strip_buf[2] = {NULL, NULL};

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

// Flush band height (rows per strip). A full-frame 434 KB single QSPI transfer
// exhausts the SPI DMA descriptor pool (ESP_ERR_NO_MEM), so we send the frame in
// STRIP_ROWS-high bands, each an async transfer pipelined with the next strip's
// byte-swap. 8 rows * 466 px * 2 B = 7456 B per strip (the SRAM bounce-buffer size).
#define STRIP_ROWS          8

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

    // bsp_display_new() already did set_gap(0x06,0) + reset + init +
    // disp_on_off(true). Do NOT repeat those (re-running the vendor init can
    // disturb panel state). It does NOT enable the backlight, so we do.
    bsp_display_backlight_on();

    // NOTE: a 90deg hardware rotation (MADCTL MV swap, e.g. via
    // bsp_display_rotation_set) is possible but, on this panel, every MV+mirror
    // combination leaves a ~6px strip of unaddressed RAM along one edge (the
    // visible window is offset within 472x466 RAM and the 466 framebuffer can't
    // cover the swapped axis cleanly). Not worth the artifact — left at the
    // default orientation.

    // Round the allocation up to a whole cache line so the final strip's
    // cache-line-aligned msync (below) never runs past the buffer end.
    size_t fb_bytes = (size_t)DISP_W * DISP_H * 2;
    fb_bytes = (fb_bytes + PSRAM_CACHE_ALIGN - 1) & ~((size_t)PSRAM_CACHE_ALIGN - 1);
    s_fb[0] = (uint16_t *)heap_caps_aligned_alloc(PSRAM_CACHE_ALIGN, fb_bytes, MALLOC_CAP_SPIRAM);
    s_fb[1] = (uint16_t *)heap_caps_aligned_alloc(PSRAM_CACHE_ALIGN, fb_bytes, MALLOC_CAP_SPIRAM);
    if (!s_fb[0] || !s_fb[1]) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return -1;
    }

    // Two internal-SRAM bounce buffers, one strip each (STRIP_ROWS full rows).
    size_t strip_bytes = (size_t)STRIP_ROWS * DISP_W * 2;
    s_strip_buf[0] = (uint16_t *)heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_strip_buf[1] = (uint16_t *)heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_strip_buf[0] || !s_strip_buf[1]) {
        ESP_LOGE(TAG, "strip bounce buffer alloc failed");
        return -1;
    }

    return 0;
}

uint16_t *display_back_buffer(void)
{
    return s_fb[s_cur ^ 1];  // render into the buffer NOT currently shown
}


// Byte-swap a strip from the PSRAM framebuffer (little-endian) into a DMA-capable
// internal-SRAM bounce buffer in the big-endian order the CO5300 wants. Reading
// PSRAM once and writing SRAM is far cheaper than the old in-place PSRAM
// read-modify-write, and DMAing from coherent internal SRAM needs no cache sync.
// `dst` must hold at least (y_end-y)*DISP_W pixels.
static void prepare_strip(const uint16_t *buf, uint16_t *dst, int y, int y_end)
{
    const uint16_t *strip = buf + (size_t)y * DISP_W;
    int strip_px = (y_end - y) * DISP_W;
    for (int i = 0; i < strip_px; i++) {
        dst[i] = __builtin_bswap16(strip[i]);
    }
}

static inline int strip_end(int y)
{
    int e = y + STRIP_ROWS;
    return (e > DISP_H) ? DISP_H : e;
}

void display_flush_and_swap(void)
{
    const uint16_t *buf = s_fb[s_cur ^ 1];

    // Pipelined flush: keep one strip's DMA in flight while the CPU byte-swaps the
    // NEXT strip from PSRAM into the OTHER SRAM bounce buffer. Two buffers
    // ping-pong (bi) so a strip being prepared never clobbers the source of the
    // in-flight DMA. DMA reads from coherent internal SRAM, so no cache sync.

#ifdef DEBUG_FLUSH_PROFILE
    int64_t t_swap = 0;
    int64_t t_wait = 0;
#endif
    int bi = 0;   // which bounce buffer the current strip uses

    // Prepare + kick the first strip.
    int y = 0;
    int ye = strip_end(y);
#ifdef DEBUG_FLUSH_PROFILE
    int64_t _s0 = esp_timer_get_time();
#endif
    prepare_strip(buf, s_strip_buf[bi], y, ye);
#ifdef DEBUG_FLUSH_PROFILE
    t_swap += esp_timer_get_time() - _s0;
#endif
    bool inflight = false;
    if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_W, ye, s_strip_buf[bi]) == ESP_OK) {
        inflight = true;
    }

    for (int ny = y + STRIP_ROWS; ny < DISP_H; ny += STRIP_ROWS) {
        int nye = strip_end(ny);
        bi ^= 1;   // prepare into the buffer NOT currently being DMA'd
        // Prepare the next strip WHILE the current strip's DMA is transferring.
#ifdef DEBUG_FLUSH_PROFILE
        int64_t _s1 = esp_timer_get_time();
#endif
        prepare_strip(buf, s_strip_buf[bi], ny, nye);
#ifdef DEBUG_FLUSH_PROFILE
        t_swap += esp_timer_get_time() - _s1;
#endif
        // Now wait for the in-flight strip to finish before issuing the next.
        if (inflight) {
#ifdef DEBUG_FLUSH_PROFILE
            int64_t _w0 = esp_timer_get_time();
#endif
            if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "flush DMA wait timed out");
            }
#ifdef DEBUG_FLUSH_PROFILE
            t_wait += esp_timer_get_time() - _w0;
#endif
            inflight = false;
        }
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, ny, DISP_W, nye, s_strip_buf[bi]) == ESP_OK) {
            inflight = true;
        }
    }

    // Wait for the final strip's DMA.
    if (inflight) {
#ifdef DEBUG_FLUSH_PROFILE
        int64_t _wl = esp_timer_get_time();
#endif
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "flush DMA wait timed out (last)");
        }
#ifdef DEBUG_FLUSH_PROFILE
        t_wait += esp_timer_get_time() - _wl;
#endif
    }

#ifdef DEBUG_FLUSH_PROFILE
    ESP_LOGI(TAG, "flush: swap=%dus dma_wait=%dus", (int)t_swap, (int)t_wait);
#endif
    s_cur ^= 1;
}

void display_set_brightness(uint8_t level)
{
    bsp_display_brightness_set(level * 100 / 255);
}
