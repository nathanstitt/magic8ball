#pragma once
#include <stdbool.h>
#include "config.h"   // NET_MSG_MAX

#ifdef __cplusplus
extern "C" {
#endif

// A single inbound message handed from the HTTP server (its own task) to the
// logic core. POD, fixed-size, copied by value through a length-1 FreeRTOS queue
// — no pointers, so there is no ownership/lifetime question across tasks.
typedef struct {
    char text[NET_MSG_MAX];   // NUL-terminated, already sanitized + truncated
} net_msg_t;

// Bring up NVS, netif, the event loop, Wi-Fi (STA-with-AP-fallback), mDNS and the
// HTTP server. Returns 0 if networking started (either STA or the setup AP), -1 if
// it could not start at all. Non-fatal: on -1 the caller keeps running offline so
// taps/shakes still work. Call once, after the HAL is up and before the tasks
// start (it registers the default event loop and may take a few seconds for STA).
int net_init(void);

// Drain the newest inbound message (non-blocking). Returns true and fills *out if
// one was waiting; false otherwise. Called by the logic task at the top of its
// loop. Latest-wins: only the most recent POST is ever delivered.
bool net_poll_message(net_msg_t *out);

// The status line to show on the AMOLED (e.g. "192.168.1.42" once connected, or
// the setup hint while the config AP is up). Returns a pointer to a stable,
// internally-owned, NUL-terminated string that is never freed and only swapped
// atomically. May return NULL when there is nothing to show.
const char *net_status_line(void);

#ifdef __cplusplus
}
#endif
