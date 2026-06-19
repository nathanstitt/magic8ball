#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "statemachine.h"   // event_t

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LISTEN_IDLE,        // waiting for a wake word
    LISTEN_LISTENING,   // wake fired; pondering while the user asks
} listen_state_t;

typedef struct {
    listen_state_t state;
    uint32_t       listen_ms;    // total time in LISTENING (drives LISTEN_MAX_MS)
    uint32_t       speech_ms;    // accumulated speech since wake (min-speech guard)
    uint32_t       silence_ms;   // accumulated quiet since last speech
} listen_t;

void listen_init(listen_t *l);

// Advance one tick. Returns EV_SHAKE while the ball should be pondering (wake
// fired and the ask isn't finished), otherwise EV_NONE. The caller ORs this into
// the tick's event (a real tap/shake still wins).
event_t listen_tick(listen_t *l, bool wake_detected, bool speech_active, uint32_t dt_ms);

#ifdef __cplusplus
}
#endif
