#include "display.h"
#include "bsp/display.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
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

    return 0;
}

uint16_t *display_back_buffer(void)
{
    return s_fb[s_cur ^ 1];  // render into the buffer NOT currently shown
}

// Flush the back buffer to the panel in horizontal strips. A full-frame
// (466*466*2 = 434 KB) single QSPI transfer exhausts the SPI DMA descriptor
// pool (ESP_ERR_NO_MEM), so we send bands per frame. Each band is a separate
// async transfer; we wait for its DMA-done callback before issuing the next,
// then swap once the whole frame has shipped.
//
// STRIP_ROWS must keep each strip's start byte offset cache-aligned:
// 8 rows * 466 px * 2 B = 7456 B = 233 * 32, a multiple of the 32 B cache line,
// so strips begin on aligned boundaries (required for clean PSRAM->DMA sync).
#define STRIP_ROWS          8

// Load-bearing for the pipelined flush: each full strip must be an exact
// multiple of the cache line. If it isn't, prepare_strip()'s rounded-up
// esp_cache_msync would spill into the NEXT (not-yet-byte-swapped) strip while
// that strip's DMA could be reading it -> corruption. Don't change STRIP_ROWS
// to a value that breaks this without revisiting prepare_strip().
static_assert(((STRIP_ROWS * DISP_W * 2) % PSRAM_CACHE_ALIGN) == 0,
              "a full strip must be a whole number of cache lines");

// Byte-swap a strip (little-endian framebuffer -> big-endian the CO5300 wants)
// and flush the swapped bytes from CPU cache to PSRAM so the SPI DMA reads
// current data. The strip start is cache-aligned (see STRIP_ROWS); the size is
// rounded up to a cache line.
static void prepare_strip(uint16_t *buf, int y, int y_end)
{
    uint16_t *strip = buf + (size_t)y * DISP_W;
    int strip_px = (y_end - y) * DISP_W;
    for (int i = 0; i < strip_px; i++) {
        strip[i] = __builtin_bswap16(strip[i]);
    }
    size_t strip_bytes = (size_t)strip_px * 2;
    size_t aligned = (strip_bytes + PSRAM_CACHE_ALIGN - 1) & ~((size_t)PSRAM_CACHE_ALIGN - 1);
    esp_cache_msync(strip, aligned, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

static inline int strip_end(int y)
{
    int e = y + STRIP_ROWS;
    return (e > DISP_H) ? DISP_H : e;
}

void display_flush_and_swap(void)
{
    uint16_t *buf = s_fb[s_cur ^ 1];

    // Pipelined flush: keep one strip's DMA in flight while the CPU prepares
    // (byte-swaps + cache-syncs) the NEXT strip. This hides most of the ~27ms
    // per-frame swap cost under the ~23ms of DMA transfer time. Each strip's
    // draw_bitmap is async (we wait on the previous strip before issuing the
    // next, so only one transfer is outstanding at a time — within the SPI
    // queue depth and keeping the swap in lock-step with the DMA).

    // Prepare + kick the first strip.
    int y = 0;
    int ye = strip_end(y);
    prepare_strip(buf, y, ye);
    bool inflight = false;
    if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_W, ye, buf + (size_t)y * DISP_W) == ESP_OK) {
        inflight = true;
    }

    for (int ny = y + STRIP_ROWS; ny < DISP_H; ny += STRIP_ROWS) {
        int nye = strip_end(ny);
        // Prepare the next strip WHILE the current strip's DMA is transferring.
        prepare_strip(buf, ny, nye);
        // Now wait for the in-flight strip to finish before issuing the next.
        if (inflight) {
            if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "flush DMA wait timed out");
            }
            inflight = false;
        }
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, ny, DISP_W, nye, buf + (size_t)ny * DISP_W) == ESP_OK) {
            inflight = true;
        }
    }

    // Wait for the final strip's DMA.
    if (inflight) {
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "flush DMA wait timed out (last)");
        }
    }

    s_cur ^= 1;
}

void display_set_brightness(uint8_t level)
{
    bsp_display_brightness_set(level * 100 / 255);
}
