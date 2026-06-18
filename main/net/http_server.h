#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Delivers a POSTed /message body (NUL-terminated, raw — the receiver sanitizes).
typedef void (*http_msg_cb)(const char *text);

typedef enum {
    HTTP_MODE_MESSAGE,   // normal LAN operation: POST /message
    HTTP_MODE_PORTAL,    // setup AP: GET / config form, POST /connect, captive detect
} http_mode_t;

// Start the single HTTP server in the given mode. In MESSAGE mode, `on_msg` is
// called for each POST /message (must be non-NULL). In PORTAL mode, `on_msg` is
// ignored (pass NULL). Returns 0/-1. Call http_server_stop() before restarting
// in a different mode.
int  http_server_start(http_mode_t mode, http_msg_cb on_msg);
int  http_server_stop(void);

#ifdef __cplusplus
}
#endif
