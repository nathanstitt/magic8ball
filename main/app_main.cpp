// =============================================================================
// app_main.cpp — orchestration and dual-core task split (Magic 8 Ball).
//
//   Core 0 (PRO_CPU): state machine + animation + event reading (logic).
//   Core 1 (APP_CPU): render the latest scene + flush to the panel.
//
// The logic core publishes a fresh scene_t under a mutex; the render core takes
// a private copy under the same mutex and renders it. If logic outruns render,
// frames are coalesced (render always sees the latest). If render outruns logic,
// it harmlessly re-renders the same scene. Both are acceptable.
// =============================================================================

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#include "config.h"
#include "scene/scene.h"
#include "scene/statemachine.h"
#include "render/render.h"
#include "render/fx.h"
#include "gfx/framebuffer.h"
#include "hal/display.h"
#include "hal/imu.h"
#include "hal/touch.h"
#include "hal/power.h"

static const char *TAG = "magic8";

// Shared scene snapshot, guarded by s_scene_mtx. Written by core 0, read by core 1.
static scene_t          s_shared_scene;
static SemaphoreHandle_t s_scene_mtx = NULL;

static bool s_have_imu = false;
static bool s_have_touch = false;

static uint32_t rng(void)
{
    return esp_random();
}

// ---- Core 0: logic ---------------------------------------------------------
static void task_logic(void *arg)
{
    (void)arg;
    sm_t sm;
    sm_init(&sm, rng);

    int64_t last_us = esp_timer_get_time();
    state_t prev_state = sm_state(&sm);

    while (true) {
        int64_t now_us = esp_timer_get_time();
        uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;
        if (dt_ms == 0) {
            dt_ms = 1;
        }

        event_t ev = EV_NONE;
        if (s_have_touch && touch_was_tapped()) {
            ev = EV_TAP;
        }
        if (s_have_imu && imu_is_shaking()) {
            ev = EV_SHAKE;
        }

        sm_tick(&sm, ev, dt_ms);

        // Panel power follows sleep state.
        state_t st = sm_state(&sm);
        if (st != prev_state) {
            if (st == ST_SLEEP) {
                power_display_sleep(true);
            } else if (prev_state == ST_SLEEP) {
                power_display_sleep(false);
            }
            prev_state = st;
        }

        // Publish the latest scene for the render core.
        xSemaphoreTake(s_scene_mtx, portMAX_DELAY);
        memcpy(&s_shared_scene, sm_scene(&sm), sizeof(scene_t));
        xSemaphoreGive(s_scene_mtx);

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---- Core 1: render --------------------------------------------------------
static void task_render(void *arg)
{
    (void)arg;
    fb_t fb;
    scene_t local;

    // Last scene we actually rendered, for static-frame skipping. The AMOLED
    // holds the last flushed image, so an unchanged scene needs no work.
    scene_t last_rendered;
    memset(&last_rendered, 0, sizeof(last_rendered));
    bool have_rendered = false;

    // Rolling profiling accumulators (logged once per second).
    int64_t acc_render_us = 0;
    int64_t acc_flush_us = 0;
    int frames = 0;
    int64_t window_start = esp_timer_get_time();

    while (true) {
        int64_t start_us = esp_timer_get_time();

        // Snapshot the latest published scene under the mutex.
        xSemaphoreTake(s_scene_mtx, portMAX_DELAY);
        memcpy(&local, &s_shared_scene, sizeof(scene_t));
        xSemaphoreGive(s_scene_mtx);

        // Skip rendering entirely when the scene is identical to the last frame
        // we drew (idle, showing — particles are frozen in these states). The
        // panel keeps displaying the last flushed frame.
        bool unchanged = have_rendered && (memcmp(&local, &last_rendered, sizeof(scene_t)) == 0);

        // While asleep the panel is off; skip rendering to save power.
        if (local.state != ST_SLEEP && !unchanged) {
            uint16_t *buf = display_back_buffer();
            fb_init(&fb, buf, DISP_W, DISP_H);

            int64_t t0 = esp_timer_get_time();
            render_frame(&fb, &local);
            int64_t t1 = esp_timer_get_time();
            display_flush_and_swap();
            memcpy(&last_rendered, &local, sizeof(scene_t));
            have_rendered = true;
            int64_t t2 = esp_timer_get_time();

            acc_render_us += (t1 - t0);
            acc_flush_us += (t2 - t1);
            frames++;
        }

        // Once per second, report rendered FPS and avg render/flush times
        // (build with -DDEBUG_FPS to enable). frames==0 means every frame was
        // skipped (static) — effectively idle.
        int64_t now = esp_timer_get_time();
        if (now - window_start >= 1000000) {
#ifdef DEBUG_FPS
            if (frames > 0) {
                int avg_render = (int)(acc_render_us / frames / 1000);
                int avg_flush = (int)(acc_flush_us / frames / 1000);
                int fps = (int)((int64_t)frames * 1000000 / (now - window_start));
                ESP_LOGI(TAG, "fps=%d  render=%dms  flush=%dms  (state=%d)",
                         fps, avg_render, avg_flush, (int)local.state);
            } else {
                ESP_LOGI(TAG, "idle (frames skipped, static)  state=%d", (int)local.state);
            }
#endif
            acc_render_us = 0;
            acc_flush_us = 0;
            frames = 0;
            window_start = now;
        }

        // Pace to the target frame rate (time-based; render may run long).
        int64_t spent_ms = (esp_timer_get_time() - start_us) / 1000;
        int delay_ms = FRAME_MS - (int)spent_ms;
        vTaskDelay(pdMS_TO_TICKS(delay_ms > 1 ? delay_ms : 1));
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Magic 8 Ball starting");

    if (display_init() != 0) {
        ESP_LOGE(TAG, "display init failed - cannot run");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    fx_init();   // precompute background gradient + halo LUT (needs PSRAM)
    power_init();
    s_have_imu = (imu_init() == 0);
    s_have_touch = (touch_init() == 0);
    if (!s_have_imu) {
        ESP_LOGW(TAG, "IMU absent - shake disabled");
    }
    if (!s_have_touch) {
        ESP_LOGW(TAG, "touch absent - tap disabled");
    }

    s_scene_mtx = xSemaphoreCreateMutex();
    if (!s_scene_mtx) {
        ESP_LOGE(TAG, "scene mutex create failed");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Initialize the shared scene to a sane idle snapshot before either task
    // reads it (render core might run its first iteration before logic publishes).
    memset(&s_shared_scene, 0, sizeof(scene_t));
    s_shared_scene.state = ST_IDLE;

    // Logic on core 0, render on core 1.
    xTaskCreatePinnedToCore(task_logic, "logic", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_render, "render", 8192, NULL, 5, NULL, 1);

    vTaskDelete(NULL);
}
