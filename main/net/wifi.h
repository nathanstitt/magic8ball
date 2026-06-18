#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once the station has an IP. `ip_str` is a dotted-decimal IPv4 string,
// valid only for the duration of the call — copy it if you need to keep it.
typedef void (*wifi_got_ip_cb)(const char *ip_str);

// Called once the fallback SoftAP + config portal is up and reachable.
typedef void (*wifi_ap_started_cb)(void);

// Start Wi-Fi: try saved STA credentials (provcfg); on no-creds or repeated
// connect failure, fall back to a SoftAP for provisioning. Invokes on_ip when a
// station IP is obtained, or on_ap when the AP comes up. Returns 0 on success
// (either path), -1 if the Wi-Fi stack could not be initialized.
int  wifi_start(wifi_got_ip_cb on_ip, wifi_ap_started_cb on_ap);

// True once the station has an IP (STA mode, connected).
bool wifi_is_sta_connected(void);

// Persist new credentials (from the portal) and request a switch to STA. v1
// reboots to apply them cleanly, so this typically does not return control to a
// running STA in the same boot.
int  wifi_apply_credentials(const char *ssid, const char *pass);

// Scan for nearby networks (for the portal SSID list). Fills up to `max` entries
// (each up to 32 chars + NUL) and sets *out_count. Returns 0/-1. Best-effort.
int  wifi_scan(char ssids[][33], int max, int *out_count);

#ifdef __cplusplus
}
#endif
