// =============================================================================
// net.cpp — networking facade for the Magic 8 Ball.
//
// Owns: the length-1 message queue (HTTP task -> logic core), the AMOLED status
// line, the inbound-message sanitizer, and the boot orchestration that wires
// wifi/mdns/http_server/captive_dns together. app_main includes only net.h.
// =============================================================================

#include "net.h"
#include "wifi.h"
#include "http_server.h"
#include "captive_dns.h"
#include "provcfg.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "net";

// The SoftAP gateway (esp_netif default) and the setup SSID, also referenced by
// the captive DNS and portal. Kept here as the single source of truth.
#define SETUP_AP_SSID   "Magic-8-Ball-Setup"
#define MDNS_HOSTNAME   "magic8ball"

// Length-1 queue: producer (HTTP task) xQueueOverwrite's, consumer (logic task)
// drains. Latest-wins, by-value copy — no shared buffer, no lifetime question.
static QueueHandle_t s_msg_q = NULL;

// Status line shown on the AMOLED. Two fixed buffers; the published pointer is
// swapped atomically (pointer-sized store) between them / NULL. Never freed.
static char s_status_ip[32];                       // "192.168.1.42"
static const char *const s_status_setup =
    "Setup: join " SETUP_AP_SSID " -> 192.168.4.1";
static const char *volatile s_status = NULL;

// ---- sanitize -------------------------------------------------------------
// Copy `raw` into `out` keeping only printable ASCII (0x20..0x7E) that the
// Montserrat font actually has; collapse any run of control/whitespace/non-ASCII
// to a single space; trim leading/trailing space; truncate to cap-1. This keeps
// the wrap clean (no glyph holes from UTF-8) and the truncation predictable.
static void net_sanitize_into(char *out, size_t cap, const char *raw)
{
    if (cap == 0) {
        return;
    }
    size_t o = 0;
    bool pending_space = false;
    bool seen = false;
    for (const unsigned char *p = (const unsigned char *)raw; *p; p++) {
        unsigned char c = *p;
        bool printable = (c >= 0x20 && c <= 0x7E);
        if (printable && c != ' ') {
            if (pending_space && seen && o < cap - 1) {
                out[o++] = ' ';
            }
            pending_space = false;
            if (o >= cap - 1) {
                break;
            }
            out[o++] = (char)c;
            seen = true;
        } else {
            // space, control, or non-ASCII byte -> fold into one pending space
            pending_space = true;
        }
    }
    out[o] = '\0';
}

void net_inject_message(const char *raw)
{
    if (!s_msg_q || !raw) {
        return;
    }
    net_msg_t m;
    net_sanitize_into(m.text, sizeof(m.text), raw);
    if (m.text[0] == '\0') {
        ESP_LOGW(TAG, "ignoring empty/unprintable message");
        return;
    }
    ESP_LOGI(TAG, "message queued: \"%s\"", m.text);
    xQueueOverwrite(s_msg_q, &m);   // never blocks; replaces any older pending msg
}

static void net_on_http_message(const char *raw)
{
    net_inject_message(raw);
}

bool net_poll_message(net_msg_t *out)
{
    if (!s_msg_q || !out) {
        return false;
    }
    return xQueueReceive(s_msg_q, out, 0) == pdTRUE;
}

const char *net_status_line(void)
{
    return (const char *)s_status;
}

// ---- wifi callbacks -------------------------------------------------------

static void on_got_ip(const char *ip_str)
{
    strncpy(s_status_ip, ip_str, sizeof(s_status_ip) - 1);
    s_status_ip[sizeof(s_status_ip) - 1] = '\0';
    s_status = s_status_ip;
    ESP_LOGI(TAG, "STA online: %s", s_status_ip);

    // Advertise magic8ball.local so the user can POST without knowing the IP.
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(MDNS_HOSTNAME);
        mdns_instance_name_set("Magic 8 Ball");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS: http://%s.local/message", MDNS_HOSTNAME);
    } else {
        ESP_LOGW(TAG, "mDNS init failed (continuing; IP still works)");
    }

    // Serve the POST listener on the LAN.
    http_server_start(HTTP_MODE_MESSAGE, net_on_http_message);
}

static void on_ap_started(void)
{
    s_status = s_status_setup;
    ESP_LOGI(TAG, "setup AP up: %s", SETUP_AP_SSID);
    captive_dns_start();
    http_server_start(HTTP_MODE_PORTAL, NULL);
}

// ---- init -----------------------------------------------------------------

int net_init(void)
{
    // NVS first (Wi-Fi creds + our config live here). Recover from a layout/
    // version change (e.g. the first boot on the new 16MB partition table).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed");
        return -1;
    }
    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "event loop create failed");
        return -1;
    }

    provcfg_init();   // non-fatal; load/save degrade gracefully if NVS is flaky

    s_msg_q = xQueueCreate(1, sizeof(net_msg_t));
    if (!s_msg_q) {
        ESP_LOGE(TAG, "message queue create failed");
        return -1;
    }

    if (wifi_start(on_got_ip, on_ap_started) != 0) {
        ESP_LOGE(TAG, "wifi_start failed");
        return -1;
    }

    ESP_LOGI(TAG, "net_init done");
    return 0;
}
