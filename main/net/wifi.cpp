// =============================================================================
// wifi.cpp — STA-with-AP-fallback Wi-Fi lifecycle for the Magic 8 Ball.
//
// Boot: load saved creds; if present, try STA (with a bounded retry); on
// no-creds or repeated failure, bring up a SoftAP so the user can provision via
// the captive portal. Connection state surfaces through the on_ip / on_ap
// callbacks (the net facade uses them to start mDNS / http / dns).
// =============================================================================

#include "wifi.h"
#include "provcfg.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"
#include <string.h>

static const char *TAG = "wifi";

#define SETUP_AP_SSID    "Magic-8-Ball-Setup"
#define SETUP_AP_CHANNEL 1
#define SETUP_AP_MAXCONN 4
#define STA_MAX_RETRY    5

#define BIT_CONNECTED    BIT0
#define BIT_FAIL         BIT1

static EventGroupHandle_t s_eg = NULL;
static int                s_retry = 0;
static bool               s_sta_connected = false;
static wifi_got_ip_cb     s_on_ip = NULL;
static wifi_ap_started_cb s_on_ap = NULL;
static esp_netif_t       *s_netif_sta = NULL;
static esp_netif_t       *s_netif_ap = NULL;

// ---- event handlers -------------------------------------------------------

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            // Retry a bounded number of times at boot; after we have ever gotten
            // an IP, s_retry was reset to 0 on GOT_IP, so transient drops re-arm
            // the full budget and we keep reconnecting rather than giving up.
            if (s_retry < STA_MAX_RETRY) {
                s_retry++;
                ESP_LOGW(TAG, "STA disconnected, retry %d/%d", s_retry, STA_MAX_RETRY);
                esp_wifi_connect();
            } else {
                s_sta_connected = false;
                xEventGroupSetBits(s_eg, BIT_FAIL);
            }
            break;
        case WIFI_EVENT_AP_START:
            if (s_on_ap) {
                s_on_ap();
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "a station joined the setup AP");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        char ip[16];
        esp_ip4addr_ntoa(&ev->ip_info.ip, ip, sizeof(ip));
        s_retry = 0;
        s_sta_connected = true;
        xEventGroupSetBits(s_eg, BIT_CONNECTED);
        if (s_on_ip) {
            s_on_ip(ip);
        }
    }
}

// ---- mode bring-up --------------------------------------------------------

static void start_ap(void)
{
    esp_wifi_stop();   // harmless if STA never started; clears any half state
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy((char *)cfg.ap.ssid, SETUP_AP_SSID, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(SETUP_AP_SSID);
    cfg.ap.channel = SETUP_AP_CHANNEL;
    cfg.ap.max_connection = SETUP_AP_MAXCONN;
    cfg.ap.authmode = WIFI_AUTH_OPEN;   // open network: easiest to join for setup
    esp_wifi_set_config(WIFI_IF_AP, &cfg);

    ESP_LOGI(TAG, "starting setup AP '%s'", SETUP_AP_SSID);
    esp_wifi_start();   // -> WIFI_EVENT_AP_START -> s_on_ap()
}

int wifi_start(wifi_got_ip_cb on_ip, wifi_ap_started_cb on_ap)
{
    s_on_ip = on_ip;
    s_on_ap = on_ap;

    s_eg = xEventGroupCreate();
    if (!s_eg) {
        return -1;
    }

    s_netif_sta = esp_netif_create_default_wifi_sta();
    s_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed");
        return -1;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    char ssid[33] = {0};
    char pass[65] = {0};
    if (provcfg_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "trying saved SSID '%s'", ssid);
        esp_wifi_set_mode(WIFI_MODE_STA);

        wifi_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
        strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
        esp_wifi_start();   // -> STA_START -> esp_wifi_connect()

        EventBits_t bits = xEventGroupWaitBits(
            s_eg, BIT_CONNECTED | BIT_FAIL, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(15000));
        if (bits & BIT_CONNECTED) {
            return 0;   // s_on_ip already fired
        }
        ESP_LOGW(TAG, "STA connect failed/timed out; falling back to setup AP");
    } else {
        ESP_LOGI(TAG, "no saved credentials; starting setup AP");
    }

    start_ap();
    return 0;
}

bool wifi_is_sta_connected(void)
{
    return s_sta_connected;
}

int wifi_apply_credentials(const char *ssid, const char *pass)
{
    if (provcfg_save(ssid, pass) != 0) {
        return -1;
    }
    // v1: reboot to apply cleanly (avoids fragile live AP->STA teardown). The
    // caller (portal POST handler) sends its response first, then we restart.
    ESP_LOGI(TAG, "credentials saved; restarting to connect");
    return 0;
}

int wifi_scan(char ssids[][33], int max, int *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (max <= 0) {
        return -1;
    }
    wifi_scan_config_t scan = {};
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        return -1;
    }
    uint16_t n = (uint16_t)max;
    wifi_ap_record_t recs[16];
    if (n > 16) {
        n = 16;
    }
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        return -1;
    }
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        if (recs[i].ssid[0] == '\0') {
            continue;
        }
        strncpy(ssids[count], (const char *)recs[i].ssid, 32);
        ssids[count][32] = '\0';
        count++;
    }
    if (out_count) {
        *out_count = count;
    }
    return 0;
}
