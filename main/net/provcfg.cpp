#include "provcfg.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "provcfg";

#define NS_NAME       "m8cfg"
#define KEY_SSID      "ssid"
#define KEY_PASS      "pass"
#define KEY_TOKEN     "token"

static bool s_ready = false;

int provcfg_init(void)
{
    // Probe that the namespace is usable. We open per-operation below (cheap, and
    // avoids holding a handle open for the device's lifetime), so this just
    // validates NVS is mounted.
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_NAME, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", NS_NAME, esp_err_to_name(err));
        return -1;
    }
    nvs_close(h);
    s_ready = true;
    return 0;
}

// Read one string key into a fixed buffer. Returns true if present and non-empty.
static bool read_str(const char *key, char *out, size_t cap)
{
    if (!s_ready || cap == 0) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NS_NAME, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return false;
    }
    out[cap - 1] = '\0';   // nvs_get_str NUL-terminates within len, but be safe
    return out[0] != '\0';
}

bool provcfg_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    if (pass && pass_cap) {
        pass[0] = '\0';
    }
    // Password may legitimately be empty (open network); only the SSID gates.
    bool have_ssid = read_str(KEY_SSID, ssid, ssid_cap);
    if (have_ssid && pass && pass_cap) {
        read_str(KEY_PASS, pass, pass_cap);
    }
    return have_ssid;
}

int provcfg_save(const char *ssid, const char *pass)
{
    if (!s_ready || !ssid) {
        return -1;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_NAME, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return -1;
    }
    err = nvs_set_str(h, KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "saved credentials for SSID '%s'", ssid);
    return 0;
}

int provcfg_clear(void)
{
    if (!s_ready) {
        return -1;
    }
    nvs_handle_t h;
    if (nvs_open(NS_NAME, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    nvs_erase_key(h, KEY_SSID);
    nvs_erase_key(h, KEY_PASS);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? 0 : -1;
}

bool provcfg_load_token(char *tok, size_t cap)
{
    return read_str(KEY_TOKEN, tok, cap);
}
