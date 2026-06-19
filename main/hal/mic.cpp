#include "mic.h"
#include "config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_types.h"

// The audio codec is driven through the Waveshare BSP. Those entry points are
// declared only in the LVGL-pulling umbrella header, so we forward-declare the
// narrow ones we need (matching the imu.cpp / touch.cpp pattern). The
// esp_codec_dev headers are LVGL-free, so we include them directly. The
// i2s_std_config_t* arg of bsp_audio_init is passed as NULL (board defaults),
// so we declare it as const void * to avoid pulling the i2s header.
extern "C" esp_err_t bsp_i2c_init(void);
extern "C" esp_err_t bsp_audio_init(const void *i2s_config);
extern "C" esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

static const char *TAG = "mic";
static esp_codec_dev_handle_t s_codec = NULL;

int mic_init(void)
{
    // The display already calls bsp_i2c_init(); a second call returns
    // ESP_ERR_INVALID_STATE, which is benign.
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

    s_codec = bsp_audio_codec_microphone_init();
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "bsp_audio_codec_microphone_init returned NULL");
        return -1;
    }

    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = 1;
    fs.channel_mask = 0;
    fs.sample_rate = MIC_SAMPLE_RATE_HZ;
    fs.mclk_multiple = 0;
    int ret = esp_codec_dev_open(s_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open failed: %d", ret);
        s_codec = NULL;
        return -1;
    }

    // Raise the mic input gain. At the codec default, captured speech measured at an
    // RMS of only ~15 (out of 32767) -- far too quiet for Gemini to transcribe (it
    // just hallucinated a generic answer). MIC_GAIN_DB lifts it to a usable level.
    ret = esp_codec_dev_set_in_gain(s_codec, MIC_GAIN_DB);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "esp_codec_dev_set_in_gain(%d dB) failed: %d (continuing)",
                 (int)MIC_GAIN_DB, ret);
    }

    ESP_LOGI(TAG, "mic ready (%d Hz, 16-bit mono, gain %d dB)",
             MIC_SAMPLE_RATE_HZ, (int)MIC_GAIN_DB);
    return 0;
}

size_t mic_read(int16_t *out, size_t max)
{
    if (s_codec == NULL || out == NULL || max == 0) {
        return 0;
    }
    // esp_codec_dev_read blocks until the whole buffer is filled and returns
    // ESP_CODEC_DEV_OK (0) on success -- it does NOT return a byte count.
    int ret = esp_codec_dev_read(s_codec, out, (int)(max * sizeof(int16_t)));
    if (ret != ESP_CODEC_DEV_OK) {
        return 0;
    }
    return max;
}
