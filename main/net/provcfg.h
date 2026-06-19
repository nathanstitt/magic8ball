#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Persisted Wi-Fi provisioning, stored in the NVS namespace "m8cfg" (separate
// from the Wi-Fi driver's own NVS keys). All functions are non-fatal and log on
// failure, matching the HAL idiom.

// Open the NVS handle. Returns 0 on success, -1 on failure. nvs_flash_init() must
// have run first (net_init does this).
int  provcfg_init(void);

// Load saved credentials. Returns true if a non-empty SSID is stored (and copies
// ssid/pass into the caller's buffers, NUL-terminated, truncated to capacity).
bool provcfg_load(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

// Save credentials and commit. Returns 0 on success, -1 on failure.
int  provcfg_save(const char *ssid, const char *pass);

// Forget stored credentials (factory-reset the Wi-Fi config). Returns 0/-1.
int  provcfg_clear(void);

// Optional shared-secret token for the POST endpoint. Returns true and copies the
// token if one is set; false if absent (the default — endpoint stays open). The
// HTTP handler is structured so enabling auth is a config-only change.
bool provcfg_load_token(char *tok, size_t cap);

// Gemini API key (voice answers). Stored in the same "m8cfg" NVS namespace.
// load returns true and copies the key if a non-empty one is stored.
bool provcfg_load_api_key(char *key, size_t cap);
int  provcfg_save_api_key(const char *key);

#ifdef __cplusplus
}
#endif
