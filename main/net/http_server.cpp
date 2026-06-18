// =============================================================================
// http_server.cpp — single esp_http_server in one of two modes.
//
//   MESSAGE: normal LAN operation. POST /message {text} -> on_msg callback.
//   PORTAL:  setup AP. GET / config form, POST /connect saves creds + reboots,
//            and the common OS captive-detection probes redirect to the form so
//            the "Sign in to network" sheet pops.
// =============================================================================

#include "http_server.h"
#include "wifi.h"
#include "provcfg.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "httpd";

#define AP_GATEWAY      "192.168.4.1"
#define MAX_BODY        256          // /message and /connect bodies are small

static httpd_handle_t s_server = NULL;
static http_msg_cb    s_on_msg = NULL;

// Minimal config portal. No external assets, no JS — one form posting urlencoded
// ssid/password to /connect.
static const char PORTAL_HTML[] =
    "<!DOCTYPE html><html><head><meta name=viewport "
    "content=\"width=device-width,initial-scale=1\">"
    "<title>Magic 8 Ball setup</title>"
    "<style>body{font-family:sans-serif;max-width:22rem;margin:2rem auto;padding:0 1rem}"
    "input{width:100%;padding:.6rem;margin:.4rem 0;box-sizing:border-box}"
    "button{width:100%;padding:.7rem;font-size:1rem}</style></head>"
    "<body><h2>Magic 8 Ball</h2><p>Join your Wi-Fi:</p>"
    "<form method=POST action=/connect>"
    "<input name=ssid placeholder=\"Wi-Fi name (SSID)\" autocomplete=off required>"
    "<input name=password type=password placeholder=\"Password\" autocomplete=off>"
    "<button type=submit>Connect</button></form></body></html>";

// ---- helpers --------------------------------------------------------------

// Read the full request body into buf (NUL-terminated). Returns length, or -1.
static int recv_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len >= cap) {
        return -1;   // too large for our fixed buffer
    }
    size_t off = 0;
    while (off < req->content_len) {
        int r = httpd_req_recv(req, buf + off, req->content_len - off);
        if (r <= 0) {
            return -1;
        }
        off += (size_t)r;
    }
    buf[off] = '\0';
    return (int)off;
}

// In-place URL-decode: %XX -> byte, '+' -> space.
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            char hex[3] = {p[1], p[2], 0};
            *o++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

// Extract a urlencoded field value by key from "a=b&c=d". Writes decoded value to
// out (truncated to cap-1). Returns true if found.
static bool form_field(const char *body, const char *key, char *out, size_t cap)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *end = strchr(v, '&');
            size_t n = end ? (size_t)(end - v) : strlen(v);
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            url_decode(out);
            return true;
        }
        p = strchr(p, '&');
        if (p) {
            p++;
        }
    }
    return false;
}

static esp_err_t redirect_to_portal(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_GATEWAY "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---- MESSAGE-mode handlers ------------------------------------------------

static esp_err_t h_message(httpd_req_t *req)
{
    // Optional shared-secret token. Absent by default -> endpoint stays open.
    // Enabling it is config-only (set "token" in NVS); structured as one early
    // check here.
    char want[64];
    if (provcfg_load_token(want, sizeof(want))) {
        char got[64] = {0};
        if (httpd_req_get_hdr_value_str(req, "X-Auth-Token", got, sizeof(got)) != ESP_OK ||
            strcmp(got, want) != 0) {
            httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "bad token");
            return ESP_FAIL;
        }
    }

    char body[MAX_BODY];
    int n = recv_body(req, body, sizeof(body));
    if (n < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no/oversized body");
        return ESP_FAIL;
    }
    if (s_on_msg) {
        s_on_msg(body);   // net facade sanitizes + queues
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_send(req, "ok\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t h_health(httpd_req_t *req)
{
    httpd_resp_send(req, "Magic 8 Ball: POST text to /message\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ---- PORTAL-mode handlers -------------------------------------------------

static esp_err_t h_portal_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Reboot shortly after responding, so the "connecting" page reaches the client.
static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static esp_err_t h_connect(httpd_req_t *req)
{
    char body[MAX_BODY];
    int n = recv_body(req, body, sizeof(body));
    if (n < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form");
        return ESP_FAIL;
    }
    char ssid[33] = {0};
    char pass[65] = {0};
    if (!form_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    form_field(body, "password", pass, sizeof(pass));   // optional (open network)

    wifi_apply_credentials(ssid, pass);   // saves to NVS

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><body style=\"font-family:sans-serif;text-align:center;"
        "margin-top:3rem\"><h2>Connecting&hellip;</h2><p>The Magic 8 Ball is "
        "restarting and will show its IP on screen. This setup network will "
        "disappear.</p></body></html>",
        HTTPD_RESP_USE_STRLEN);

    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// Catch-all 404 -> redirect any unknown URL to the portal (closes the loop for
// every OS captive probe the explicit handlers below don't cover).
static esp_err_t h_404_redirect(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return redirect_to_portal(req);
}

// ---- registration ---------------------------------------------------------

static void register_message(void)
{
    httpd_uri_t u_msg = { .uri = "/message", .method = HTTP_POST,
                          .handler = h_message, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &u_msg);
    httpd_uri_t u_health = { .uri = "/", .method = HTTP_GET,
                             .handler = h_health, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &u_health);
}

static void register_portal(void)
{
    httpd_uri_t u_root = { .uri = "/", .method = HTTP_GET,
                           .handler = h_portal_root, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &u_root);
    httpd_uri_t u_connect = { .uri = "/connect", .method = HTTP_POST,
                              .handler = h_connect, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &u_connect);

    // OS captive-detection probes -> redirect so the sign-in sheet appears.
    static const char *probes[] = {
        "/hotspot-detect.html",   // iOS/macOS
        "/generate_204", "/gen_204",  // Android
        "/ncsi.txt", "/connecttest.txt",  // Windows
    };
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        httpd_uri_t u = { .uri = probes[i], .method = HTTP_GET,
                          .handler = [](httpd_req_t *r) { return redirect_to_portal(r); },
                          .user_ctx = NULL };
        httpd_register_uri_handler(s_server, &u);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, h_404_redirect);
}

int http_server_start(http_mode_t mode, http_msg_cb on_msg)
{
    if (s_server) {
        http_server_stop();
    }
    s_on_msg = on_msg;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 5120;          // headroom over default for our handlers
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;      // message(2) or portal(2 + 5 probes)

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        s_server = NULL;
        return -1;
    }
    if (mode == HTTP_MODE_MESSAGE) {
        register_message();
        ESP_LOGI(TAG, "HTTP server up (message mode): POST /message");
    } else {
        register_portal();
        ESP_LOGI(TAG, "HTTP server up (portal mode)");
    }
    return 0;
}

int http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return 0;
}
