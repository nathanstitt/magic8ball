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

// Advance one tick. `voice_busy` is true while the voice task is recording/awaiting
// Gemini; it holds the ponder open past PONDER_MS (bounded by VOICE_ASK_MAX_MS) so a
// real answer is never cut off by the timer. Returns EV_SHAKE while pondering, else
// EV_NONE. The caller ORs this into the tick's event (a real tap/shake still wins).
event_t listen_tick(listen_t *l, bool wake_detected, bool speech_active,
                    bool voice_busy, uint32_t dt_ms);

// True while a voice ask is in progress (wake fired, awaiting the answer). Lets
// callers react to the listening phase (e.g. show the swirl) without reaching
// into the FSM's internal state representation.
bool    listen_is_active(const listen_t *l);

#ifdef __cplusplus
}
#endif
