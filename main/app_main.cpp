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
#include <stdio.h>

#include "config.h"
#include "scene/scene.h"
#include "scene/statemachine.h"
#include "render/render.h"
#include "render/fx.h"
#include "gfx/framebuffer.h"
#include "gfx/text.h"
#include "gfx/color.h"
#include "hal/display.h"
#include "hal/imu.h"
#include "hal/touch.h"
#include "hal/power.h"
#include "hal/wakeword.h"
#include "hal/recorder.h"
#include "scene/listen.h"
#include "scene/answers.h"
#include "net/net.h"
#include "net/provcfg.h"
#include "net/gemini.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "magic8";

// Shared scene snapshot, guarded by s_scene_mtx. Written by core 0, read by core 1.
static scene_t          s_shared_scene;
static SemaphoreHandle_t s_scene_mtx = NULL;

static bool s_have_imu = false;
static bool s_have_touch = false;
static bool s_have_voice = false;

// Voice-ask coordination. The logic core sets s_voice_busy and notifies s_voice_task
// on a wake; the voice task records + asks Gemini, injects the answer (or a random
// fallback) via the net message queue, then clears s_voice_busy.
static volatile bool   s_voice_busy = false;
static TaskHandle_t    s_voice_task = NULL;

// Factory-reset gesture overlay. While true, the logic loop owns the panel (drawing
// the "hold to reset" countdown) and the render task must NOT flush, so the two
// don't fight over the display. Set/cleared only by the logic loop.
static volatile bool   s_reset_overlay = false;
// Set by the logic loop when the overlay ends, to force one render-task redraw (the
// overlay drew directly to the panel behind the render task's static-frame cache).
static volatile bool   s_force_render = false;

// Defined below app_main's helpers; used by task_logic above them.
static void reset_screen(const char *line1, const char *line2, uint16_t color);
static void factory_reset(void);

static uint32_t rng(void)
{
    return esp_random();
}

// ---- Core 0: voice ask (blocking record + Gemini, off the logic loop) -------
static void task_voice(void *arg)
{
    (void)arg;
    // 256KB capture buffer in PSRAM (REC_MAX_SAMPLES * 2 bytes), allocated once.
    int16_t *pcm = (int16_t *)heap_caps_malloc(REC_MAX_SAMPLES * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM);
    if (!pcm) {
        ESP_LOGE(TAG, "voice: OOM capture buffer; voice answers disabled");
        s_voice_task = NULL;   // let the logic core's `&& s_voice_task` guard skip us
        vTaskDelete(NULL);
        return;
    }
    // A private picker for fallback answers (independent of the state machine's).
    answers_picker_t fb_picker;
    answers_picker_init(&fb_picker, rng);

    while (true) {
        // Wait until the logic core signals a wake.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        char answer[NET_MSG_MAX] = {0};
        bool got = false;

        // Own the mic: stop wake-word re-arm, record, then ask Gemini.
        wakeword_set_armed(false);
        size_t n = 0;
        if (recorder_capture(pcm, REC_MAX_SAMPLES, &n) == 0) {
            if (gemini_ask(pcm, n, answer, sizeof(answer)) == 0 && answer[0] != '\0') {
                got = true;
            }
        }
        wakeword_set_armed(true);

        if (!got) {
            // Fallback: a random classic answer. answers_pick returns an INDEX;
            // answers_get maps it to the string.
            int idx = answers_pick(&fb_picker);
            const char *classic = answers_get(idx);
            strncpy(answer, classic ? classic : "Reply hazy try again",
                    sizeof(answer) - 1);
            answer[sizeof(answer) - 1] = '\0';
            ESP_LOGW(TAG, "voice: using fallback answer \"%s\"", answer);
        }

        net_inject_message(answer);
        s_voice_busy = false;
    }
}

// ---- Core 0: logic ---------------------------------------------------------
static void task_logic(void *arg)
{
    (void)arg;
    sm_t sm;
    sm_init(&sm, rng);

    listen_t listen;
    listen_init(&listen);

    int64_t last_us = esp_timer_get_time();
    state_t prev_state = sm_state(&sm);

    // A network message held until the scene is restful enough to play it (so we
    // never abort a running animation). Newest wins.
    net_msg_t pending_msg;
    bool have_pending = false;

    // The Wi-Fi status overlay (IP / setup hint) is only useful until the device
    // has shown its first answer; after that the IP clutters the scene, so we
    // latch this on the first SHOWING and stop publishing the status line.
    bool first_answer_shown = false;

    while (true) {
        int64_t now_us = esp_timer_get_time();
        uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
        last_us = now_us;
        if (dt_ms == 0) {
            dt_ms = 1;
        }
        // Clamp dt so one slow tick can't telescope the animation. The voice path
        // blocks ~16ms reading audio (and much longer on the detection/re-arm
        // tick), which would otherwise hand sm_tick a giant dt that fast-forwards
        // SHAKING->TUMBLING->LOCKING->SHOWING in a single frame (no visible
        // ponder/swirl). Capping at DT_MAX_MS keeps every phase animating.
        if (dt_ms > DT_MAX_MS) {
            dt_ms = DT_MAX_MS;
        }

        // Drain the newest inbound network message (non-blocking). Replacing any
        // older held message keeps "latest wins" even if one was waiting.
        net_msg_t inbound;
        if (net_poll_message(&inbound)) {
            pending_msg = inbound;
            have_pending = true;
        }

        event_t ev = EV_NONE;
        if (s_have_touch && touch_was_tapped()) {
            ev = EV_TAP;
        }
        if (s_have_imu && imu_is_shaking()) {
            ev = EV_SHAKE;
        }

        // Factory-reset gesture: hold the screen continuously. Past RESET_ARM_MS we
        // show a countdown overlay; held to RESET_HOLD_MS total, we clear creds and
        // reboot. Released before then, the overlay clears and normal play resumes.
        // Uses touch_is_down() (level), separate from the tap edge above.
        static uint32_t s_hold_ms = 0;
        static int s_shown_remain = -1;   // last countdown value drawn (-1 = none)
        if (s_have_touch && touch_is_down()) {
            s_hold_ms += dt_ms;
            if (s_hold_ms >= RESET_HOLD_MS) {
                factory_reset();   // never returns
            }
            if (s_hold_ms >= RESET_ARM_MS) {
                s_reset_overlay = true;   // take the panel from the render task
                int remain = (int)((RESET_HOLD_MS - s_hold_ms + 999) / 1000);
                // Redraw only when the countdown number changes (each draw is a slow
                // full-frame flush; redrawing every tick would saturate the loop).
                if (remain != s_shown_remain) {
                    s_shown_remain = remain;
                    char line2[24];
                    snprintf(line2, sizeof(line2), "RESET IN %d...", remain);
                    reset_screen("KEEP HOLDING", line2, rgb565(120, 170, 255));
                }
            }
        } else {
            if (s_reset_overlay) {
                // Released: hand the panel back and force the render task to redraw
                // (it caches the last scene and drew nothing, so the overlay would
                // otherwise stay frozen on the panel).
                s_reset_overlay = false;
                s_force_render = true;
            }
            s_hold_ms = 0;
            s_shown_remain = -1;
        }

        // Voice front-end: pump audio, translate wake+VAD into the existing
        // shake-hold-release pattern. A real tap/shake this tick wins.
        // NOTE: while the detector is actively listening, wakeword_update() blocks
        // ~16ms reading a model window, so this loop runs at ~60Hz (not the 1ms
        // idle cadence) during voice. Harmless: dt_ms is timer-based so timing
        // stays correct, and 60Hz is ample for tap/shake polling.
        if (s_have_voice) {
            wakeword_update();
            bool wake = wakeword_detected();
            // On a fresh wake (and not already mid-ask), kick the voice task.
            if (wake && !s_voice_busy && s_voice_task) {
                s_voice_busy = true;
                xTaskNotifyGive(s_voice_task);
            }
            event_t vev = listen_tick(&listen,
                                      wake,
                                      wakeword_speech_active(),
                                      s_voice_busy,
                                      dt_ms);
            if (ev == EV_NONE && vev != EV_NONE) {
                ev = vev;
            }
        }

        // Inject a held message only from a restful state and only when the user
        // isn't physically interacting this tick (a real tap/shake wins; the
        // message waits one more 1ms tick). Treating ST_SLEEP as restful gives
        // wake-on-message for free: sm_trigger_message does SLEEP->SHAKING, and
        // the panel-power transition below re-enables the display next tick.
        if (have_pending && ev == EV_NONE) {
            state_t st = sm_state(&sm);
            if (st == ST_IDLE || st == ST_SHOWING || st == ST_SLEEP) {
                sm_trigger_message(&sm, pending_msg.text);
                have_pending = false;
            }
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
            // The first time an answer is fully shown, retire the status overlay.
            if (st == ST_SHOWING) {
                first_answer_shown = true;
            }
            prev_state = st;
        }

        // Publish the latest scene for the render core. The status overlay
        // pointer is set here (post-copy) rather than in the state machine, so
        // scene/ stays networking-free: net_status_line() returns a stable,
        // never-freed string (or NULL). Suppress it once the first answer has
        // been shown so the IP doesn't clutter the scene afterward.
        // Listening starfield: flag the scene during a VOICE listen (the listen FSM
        // is in LISTEN_LISTENING) so the particles render as a brighter flitting
        // blue starfield. Set post-copy like `status`, so scene/ stays host-testable.
        bool voice_listening = s_have_voice && listen_is_active(&listen);

        // Starfield fade: full brightness while listening, then ease down once the
        // answer starts rising so the stars dissolve into the rise instead of
        // popping out. Decays over ~the tumble so it's gone by the time the die locks.
        static int s_star_fade = 0;
        if (voice_listening) {
            s_star_fade = 255;
        } else if (s_star_fade > 0) {
            int step = (int)((uint32_t)dt_ms * 255 / STAR_FADE_MS);
            s_star_fade = (step >= s_star_fade) ? 0 : (s_star_fade - step);
        }

        xSemaphoreTake(s_scene_mtx, portMAX_DELAY);
        memcpy(&s_shared_scene, sm_scene(&sm), sizeof(scene_t));
        s_shared_scene.status = first_answer_shown ? NULL : net_status_line();
        s_shared_scene.listening = voice_listening;
        s_shared_scene.star_fade = (uint8_t)s_star_fade;
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

        // While the factory-reset overlay is up, the logic loop owns the panel; back
        // off so we don't flush over its "hold to reset" countdown.
        if (s_reset_overlay) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Snapshot the latest published scene under the mutex.
        xSemaphoreTake(s_scene_mtx, portMAX_DELAY);
        memcpy(&local, &s_shared_scene, sizeof(scene_t));
        xSemaphoreGive(s_scene_mtx);

        // Skip rendering entirely when the scene is identical to the last frame
        // we drew (idle, showing — particles are frozen in these states). The
        // panel keeps displaying the last flushed frame.
        //
        // This bytewise memcmp (incl. floats + struct padding) is safe ONLY
        // because every scene write flows through memcpy from the state
        // machine's scene (so padding stays consistent) and static states
        // re-assign identical float constants each tick (bit-identical). It
        // fails SAFE: any mismatch just causes an extra render, never a missed
        // update. If scene_t fields are ever assigned individually elsewhere,
        // stale padding could cause spurious renders — revisit this then.
        bool unchanged = have_rendered && (memcmp(&local, &last_rendered, sizeof(scene_t)) == 0);

        // After the reset overlay drew directly to the panel, the scene may be
        // bit-identical to last_rendered (unchanged), yet the panel shows the
        // overlay. Force one redraw to restore the real scene.
        if (s_force_render) {
            s_force_render = false;
            unchanged = false;
        }

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

// Draw two centered lines of the 8x8 bitmap font on a freshly-cleared panel and
// flush. Used by the factory-reset gesture overlay. `color` is native RGB565.
// NOTE: this writes the panel directly; only call it while the render task is not
// also flushing (the reset gesture pauses normal rendering via s_reset_overlay).
static void reset_screen(const char *line1, const char *line2, uint16_t color)
{
    uint16_t *buf = display_back_buffer();
    fb_t fb;
    fb_init(&fb, buf, DISP_W, DISP_H);
    fb_clear(&fb, rgb565(0, 0, 0));

    const int size = 3;                 // 8x8 font * 3 = 24px tall glyphs
    const int line_h = 8 * size;
    int y1 = DISP_H / 2 - line_h;
    int y2 = DISP_H / 2 + line_h / 2;
    text_draw(&fb, line1, text_centered_x(&fb, line1, size), y1, size, color, 255);
    if (line2 && line2[0]) {
        text_draw(&fb, line2, text_centered_x(&fb, line2, size), y2, size, color, 255);
    }
    display_flush_and_swap();
}

// Clear saved Wi-Fi creds + Gemini key and reboot into the setup AP. Called by the
// logic loop once the screen has been held for RESET_HOLD_MS. Never returns.
static void factory_reset(void)
{
    // provcfg/NVS are already up by now (net_init ran at startup), but re-init
    // defensively -- both calls are idempotent.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    provcfg_init();
    provcfg_clear();           // forget Wi-Fi creds
    provcfg_save_api_key("");  // and the Gemini key
    ESP_LOGW(TAG, "credentials cleared; rebooting into setup AP");

    reset_screen("SETTINGS CLEARED", "JOIN MAGIC-8-BALL-SETUP", rgb565(120, 170, 255));
    vTaskDelay(pdMS_TO_TICKS(2500));   // let the user read it
    esp_restart();
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
    s_have_voice = (wakeword_init() == 0);
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

    // Bring up networking (Wi-Fi STA with captive-portal fallback, POST listener,
    // mDNS). Non-fatal: if it can't start, the ball still answers taps/shakes.
    if (net_init() != 0) {
        ESP_LOGW(TAG, "networking unavailable - offline (taps/shakes still work)");
    }

    // Logic on core 0, render on core 1. Logic holds an sm_t (a full scene_t,
    // plus the custom-message buffer) and two net_msg_t locals on its stack, so
    // give it headroom beyond the original 4096.
    xTaskCreatePinnedToCore(task_logic, "logic", 6144, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_render, "render", 8192, NULL, 5, NULL, 1);

    if (s_have_voice) {
        // Voice task on core 0 (logic core). It blocks on a notification, so it's
        // idle until a wake; the recorder + Gemini stacks need headroom.
        xTaskCreatePinnedToCore(task_voice, "voice", 8192, NULL, 4, &s_voice_task, 0);
    }

    vTaskDelete(NULL);
}
