#include "gemini.h"
#include "gemini_wav.h"
#include "provcfg.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "gemini";

// Pull the first "text" string out of the generateContent JSON response. Minimal
// hand-parse (no cJSON dependency): find "text": "...". Copies into out (truncated,
// unescaped for the common \n / \" cases). Returns true if a non-empty value found.
static bool extract_text(const char *json, char *out, size_t cap)
{
    const char *key = strstr(json, "\"text\"");
    if (!key) {
        return false;
    }
    const char *colon = strchr(key, ':');
    if (!colon) {
        return false;
    }
    const char *q = strchr(colon, '"');
    if (!q) {
        return false;
    }
    q++;
    size_t o = 0;
    while (*q && *q != '"' && o + 1 < cap) {
        if (*q == '\\' && q[1]) {
            q++;
            char c = *q;
            if (c == 'n' || c == 't') {
                out[o++] = ' ';
            } else {
                out[o++] = c;
            }
        } else {
            out[o++] = *q;
        }
        q++;
    }
    out[o] = '\0';
    return o > 0;
}

struct resp_ctx {
    char    *buf;
    size_t   cap;
    size_t   len;
    int64_t  first_byte_us;   // timestamp of the first response-data event (0 = none yet)
    int64_t  last_byte_us;    // timestamp of the most recent response-data event
};

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        struct resp_ctx *ctx = (struct resp_ctx *)evt->user_data;
        int64_t now = esp_timer_get_time();
        if (ctx->first_byte_us == 0) {
            ctx->first_byte_us = now;
        }
        ctx->last_byte_us = now;
        size_t n = evt->data_len;
        if (ctx->len + n >= ctx->cap) {
            n = ctx->cap - ctx->len - 1;
        }
        if (n > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, n);
            ctx->len += n;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

int gemini_ask(const int16_t *pcm, size_t samples, char *out, size_t cap)
{
    if (!pcm || samples == 0 || !out || cap == 0) {
        return -1;
    }
    char api_key[GEMINI_API_KEY_MAX] = {0};
    if (!provcfg_load_api_key(api_key, sizeof(api_key))) {
        ESP_LOGW(TAG, "no API key provisioned; fallback");
        return -1;
    }

    int64_t t_start = esp_timer_get_time();

    size_t pcm_bytes = samples * sizeof(int16_t);
    size_t wav_bytes = 44 + pcm_bytes;
    uint8_t *wav = (uint8_t *)heap_caps_malloc(wav_bytes, MALLOC_CAP_SPIRAM);
    size_t b64_cap = wav_base64_len(wav_bytes);
    char *b64 = (char *)heap_caps_malloc(b64_cap, MALLOC_CAP_SPIRAM);
    if (!wav || !b64) {
        ESP_LOGE(TAG, "OOM building request");
        free(wav);
        free(b64);
        return -1;
    }
    wav_write_header(wav, MIC_SAMPLE_RATE_HZ, samples);
    memcpy(wav + 44, pcm, pcm_bytes);
    wav_base64_encode(wav, wav_bytes, b64);
    free(wav);

    size_t body_cap = b64_cap + 512;
    char *body = (char *)heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM);
    if (!body) {
        ESP_LOGE(TAG, "OOM body");
        free(b64);
        return -1;
    }
    int body_len = snprintf(body, body_cap,
        "{\"contents\":[{\"parts\":["
        "{\"text\":\"%s\"},"
        "{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"%s\"}}"
        "]}]}",
        GEMINI_PROMPT, b64);
    free(b64);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        ESP_LOGE(TAG, "body format failed");
        free(body);
        return -1;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://%s/v1beta/models/%s:generateContent?key=%s",
             GEMINI_HOST, GEMINI_MODEL, api_key);

    static const size_t RESP_CAP = 4096;
    char *resp = (char *)heap_caps_malloc(RESP_CAP, MALLOC_CAP_SPIRAM);
    if (!resp) {
        free(body);
        return -1;
    }
    resp[0] = '\0';
    struct resp_ctx ctx = { resp, RESP_CAP, 0, 0, 0 };

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.event_handler = on_http_event;
    cfg.user_data = &ctx;
    cfg.timeout_ms = 15000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, body_len);

    int64_t t_perform = esp_timer_get_time();   // prep done; about to connect+send+recv
    int rc = -1;
    esp_err_t err = esp_http_client_perform(client);
    int64_t t_done = esp_timer_get_time();
    int status = esp_http_client_get_status_code(client);

    // Timing breakdown (ms). prep = WAV+base64+JSON build. ttfb = connect + upload +
    // Gemini processing until the first response byte (the long pole). recv = first to
    // last response byte. The triangle could begin rising at t_perform (request sent)
    // to overlap the ttfb window. wav_bytes shows the upload size driving the upload.
    int prep_ms = (int)((t_perform - t_start) / 1000);
    int ttfb_ms = ctx.first_byte_us ? (int)((ctx.first_byte_us - t_perform) / 1000) : -1;
    int recv_ms = (ctx.first_byte_us && ctx.last_byte_us)
                  ? (int)((ctx.last_byte_us - ctx.first_byte_us) / 1000) : -1;
    int total_ms = (int)((t_done - t_start) / 1000);
    ESP_LOGI(TAG, "timing: prep=%dms ttfb=%dms recv=%dms total=%dms (upload=%uKB)",
             prep_ms, ttfb_ms, recv_ms, total_ms, (unsigned)(wav_bytes / 1024));

    if (err == ESP_OK && status == 200) {
        if (extract_text(resp, out, cap)) {
            ESP_LOGI(TAG, "answer: \"%s\"", out);
            rc = 0;
        } else {
            ESP_LOGW(TAG, "no text in response: %.200s", resp);
        }
    } else {
        ESP_LOGW(TAG, "HTTP err=%s status=%d", esp_err_to_name(err), status);
    }

    esp_http_client_cleanup(client);
    free(body);
    free(resp);
    return rc;
}
