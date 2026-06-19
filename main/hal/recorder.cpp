#include "recorder.h"
#include "mic.h"
#include "config.h"
#include "scene/vad.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "recorder";

// RMS of one frame of int16 samples.
static uint32_t frame_rms(const int16_t *s, size_t n)
{
    if (n == 0) {
        return 0;
    }
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = s[i];
        acc += (uint64_t)(v * v);
    }
    return (uint32_t)sqrt((double)(acc / n));
}

int recorder_capture(int16_t *buf, size_t cap, size_t *out_samples)
{
    if (!buf || !out_samples || cap < REC_FRAME_SAMPLES) {
        return -1;
    }
    vad_t v;
    vad_init(&v);

    size_t total = 0;
    const uint32_t frame_ms = REC_FRAME_SAMPLES * 1000 / MIC_SAMPLE_RATE_HZ;

    while (total + REC_FRAME_SAMPLES <= cap) {
        size_t got = mic_read(buf + total, REC_FRAME_SAMPLES);
        if (got == 0) {
            ESP_LOGW(TAG, "mic_read returned 0; aborting capture");
            break;
        }
        uint32_t rms = frame_rms(buf + total, got);
        total += got;
        if (vad_feed(&v, rms, frame_ms)) {
            break;
        }
    }

    *out_samples = total;
    if (total < REC_MIN_SAMPLES) {
        ESP_LOGW(TAG, "captured only %u samples (<%d); fallback",
                 (unsigned)total, REC_MIN_SAMPLES);
        return -1;
    }
    ESP_LOGI(TAG, "captured %u samples (~%ums)",
             (unsigned)total, (unsigned)(total * 1000 / MIC_SAMPLE_RATE_HZ));
    return 0;
}
