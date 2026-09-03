#include "sound.h"
#include "config.h"
#include "scene/bloop.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// The audio codec is driven through the Waveshare BSP. Those entry points are
// declared only in the LVGL-pulling umbrella header, so we forward-declare the
// narrow ones we need (matching the mic.cpp / imu.cpp / touch.cpp pattern). The
// esp_codec_dev headers are LVGL-free, so we include them directly. The
// i2s_std_config_t* arg of bsp_audio_init is passed as NULL (board defaults),
// so we declare it as const void * to avoid pulling the i2s header.
extern "C" esp_err_t bsp_i2c_init(void);
extern "C" esp_err_t bsp_audio_init(const void *i2s_config);
extern "C" esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

static const char *TAG = "sound";
static esp_codec_dev_handle_t s_codec = NULL;
static TaskHandle_t s_task = NULL;
static int16_t *s_pcm = NULL;

// Set by the caller (logic core) before notifying, cleared by the audio task when
// playback finishes. Claiming it on the caller side closes the window between the
// notify and the task actually being scheduled. There is exactly one writer of
// `true` (the logic core) and one writer of `false` (the audio task), so there is
// no read-modify-write race and a plain volatile bool is sufficient.
static volatile bool s_playing = false;

static void task_sound(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Always close first. esp_codec_dev_open() early-returns OK if the device
        // believes it is already open, so a previously half-failed open (opened
        // flag set, format rejected) would otherwise wedge us silent forever.
        // close() on an already-closed device is a no-op.
        esp_codec_dev_close(s_codec);

        // Must match the mic's format EXACTLY. The two codecs share one I2S
        // peripheral: esp_codec_dev compares this against the open input and
        // returns ESP_CODEC_DEV_NOT_SUPPORT on a sample-rate mismatch, and a
        // channel/bit mismatch would push it down a path that reconfigures --
        // i.e. interrupts -- the running mic.
        esp_codec_dev_sample_info_t fs = {};
        fs.bits_per_sample = 16;
        fs.channel = 1;
        fs.channel_mask = 0;
        fs.sample_rate = BLOOP_SAMPLE_RATE_HZ;
        fs.mclk_multiple = 0;

        int ret = esp_codec_dev_open(s_codec, &fs);
        if (ret != ESP_CODEC_DEV_OK) {
            // Not fatal and not latched: a later reveal retries. No log throttle
            // needed -- the FSM already rate-limits this to once per answer.
            ESP_LOGW(TAG, "speaker open failed: %d (silent this time)", ret);
            s_playing = false;
            continue;
        }

        // A fresh handle's stored volume is 0, which esp_codec_dev maps to -96 dB.
        // Without this the writes below are inaudible. The ES8311 has a hardware
        // volume register, so this is a register write and our buffer is untouched.
        esp_codec_dev_set_out_vol(s_codec, BLOOP_VOLUME_PCT);

        for (size_t off = 0; off < BLOOP_TOTAL_SAMPLES; off += SOUND_WRITE_CHUNK) {
            size_t n = BLOOP_TOTAL_SAMPLES - off;
            if (n > SOUND_WRITE_CHUNK) {
                n = SOUND_WRITE_CHUNK;
            }
            if (esp_codec_dev_write(s_codec, s_pcm + off,
                                    (int)(n * sizeof(int16_t))) != ESP_CODEC_DEV_OK) {
                ESP_LOGW(TAG, "speaker write failed; truncating bloop");
                break;
            }
        }

        // esp_codec_dev_write returns once the data is QUEUED to DMA, not once it
        // has been played, so closing immediately would cut off the tail.
        vTaskDelay(pdMS_TO_TICKS(SOUND_DRAIN_MS));

        // Closing drops the power amp (the ES8311 driver owns that GPIO), which is
        // why we open and close per play rather than holding the codec open: this
        // device idles almost all the time, and a continuously powered class-D amp
        // on a micro speaker hisses audibly in a quiet room. If the amp's turn-on
        // transient ever proves worse than the hiss, the alternative is to stay
        // open and esp_codec_dev_set_out_mute() while idle.
        esp_codec_dev_close(s_codec);
        s_playing = false;
    }
}

int sound_init(void)
{
    // Same idempotent bring-up mic.cpp does. Both calls are no-ops if the display
    // (I2C) or the mic (I2S) already ran them, but doing them here means the
    // speaker still works on a board where the wake-word mic never came up -- in
    // that case nothing else has called bsp_audio_init at all.
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bsp_i2c_init failed: %s", esp_err_to_name(err));
        return -1;
    }
    err = bsp_audio_init(NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %s", esp_err_to_name(err));
        return -1;
    }

    s_codec = bsp_audio_codec_speaker_init();
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "bsp_audio_codec_speaker_init returned NULL");
        return -1;
    }

    // Internal RAM, not PSRAM: I2S DMA reads straight out of this buffer, and
    // internal RAM sidesteps the cache-coherency dance the display flush needs.
    // At ~14KB it is well under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL anyway; the
    // explicit caps just make that independent of the threshold.
    s_pcm = (int16_t *)heap_caps_malloc(BLOOP_TOTAL_SAMPLES * sizeof(int16_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_pcm == NULL) {
        ESP_LOGE(TAG, "OOM rendering bloop (%d bytes)",
                 (int)(BLOOP_TOTAL_SAMPLES * sizeof(int16_t)));
        s_codec = NULL;
        return -1;
    }

    // Rendered once: the cue is deterministic, so the play path stays a bare
    // write with no arithmetic and no allocation.
    size_t n = bloop_render(s_pcm, BLOOP_TOTAL_SAMPLES);
    if (n != BLOOP_TOTAL_SAMPLES) {
        ESP_LOGE(TAG, "bloop_render wrote %d of %d samples",
                 (int)n, (int)BLOOP_TOTAL_SAMPLES);
        heap_caps_free(s_pcm);
        s_pcm = NULL;
        s_codec = NULL;
        return -1;
    }

    if (xTaskCreatePinnedToCore(task_sound, "sound", SOUND_TASK_STACK, NULL,
                                SOUND_TASK_PRIO, &s_task, SOUND_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "sound task create failed");
        heap_caps_free(s_pcm);
        s_pcm = NULL;
        s_codec = NULL;
        s_task = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "speaker ready (%d Hz, %d blips, %d ms, vol %d%%)",
             BLOOP_SAMPLE_RATE_HZ, BLOOP_COUNT,
             BLOOP_COUNT * (BLOOP_BLIP_MS + BLOOP_GAP_MS), BLOOP_VOLUME_PCT);
    return 0;
}

void sound_play_bloop(void)
{
    if (s_task == NULL || s_playing) {
        return;
    }
    s_playing = true;
    xTaskNotifyGive(s_task);
}
