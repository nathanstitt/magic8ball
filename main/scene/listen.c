#include "listen.h"
#include "config.h"
#include <stddef.h>

void listen_init(listen_t *l)
{
    for (unsigned i = 0; i < sizeof(*l); i++) {
        ((char *)l)[i] = 0;
    }
    l->state = LISTEN_IDLE;
}

event_t listen_tick(listen_t *l, bool wake_detected, bool speech_active, uint32_t dt_ms)
{
    switch (l->state) {
    case LISTEN_IDLE:
        if (wake_detected) {
            l->state = LISTEN_LISTENING;
            l->listen_ms = 0;
            l->speech_ms = 0;
            l->silence_ms = 0;
            return EV_SHAKE;
        }
        return EV_NONE;

    case LISTEN_LISTENING:
        l->listen_ms += dt_ms;
        if (l->listen_ms >= LISTEN_MAX_MS) {
            l->state = LISTEN_IDLE;
            return EV_NONE;
        }
        if (speech_active) {
            l->speech_ms += dt_ms;
            l->silence_ms = 0;
        } else {
            l->silence_ms += dt_ms;
        }
        // End the ask only after the user has actually spoken long enough
        // (min-speech guard) and has then been quiet long enough.
        if (l->speech_ms >= MIN_SPEECH_MS && l->silence_ms >= SILENCE_MS) {
            l->state = LISTEN_IDLE;
            return EV_NONE;   // release: existing debounce/think-min reveals the answer
        }
        return EV_SHAKE;
    }
    return EV_NONE;
}
