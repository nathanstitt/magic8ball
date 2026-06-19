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

event_t listen_tick(listen_t *l, bool wake_detected, bool speech_active,
                    bool voice_busy, uint32_t dt_ms)
{
    if (l->state == LISTEN_IDLE) {
        if (!wake_detected) {
            return EV_NONE;   // no wake: stay idle, ignore any stray speech
        }
        // Wake fired: arm a fresh ask. Fall through so this tick's own speech
        // and elapsed time count toward the guards below.
        l->state = LISTEN_LISTENING;
        l->listen_ms = 0;
        l->speech_ms = 0;
        l->silence_ms = 0;
    }

    // LISTEN_LISTENING. Both terminal paths below (the max-window backstop here
    // and the min-speech+silence end further down) intentionally return the same
    // EV_NONE + LISTEN_IDLE: the caller does not distinguish "answered because you
    // stopped talking" from "answered because we waited long enough" — releasing
    // the held EV_SHAKE is all the downstream state machine needs to reveal.
    l->listen_ms += dt_ms;

    // While the voice task is busy, ignore the normal ponder/silence ends; only the
    // hard voice cap can end the ask (so a slow Gemini round-trip is never cut off).
    if (voice_busy) {
        if (l->listen_ms >= VOICE_ASK_MAX_MS) {
            l->state = LISTEN_IDLE;
            return EV_NONE;
        }
        return EV_SHAKE;
    }

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
    // Wake-only flow: no speech signal ever arrives (the one-shot wake detector
    // gives none), so ponder for a fixed PONDER_MS after the wake, then reveal.
    if (l->speech_ms == 0 && l->listen_ms >= PONDER_MS) {
        l->state = LISTEN_IDLE;
        return EV_NONE;
    }
    // Speech-driven end (dormant unless a VAD supplies speech_active): reveal once
    // the user has spoken long enough (min-speech guard) and then gone quiet.
    if (l->speech_ms >= MIN_SPEECH_MS && l->silence_ms >= SILENCE_MS) {
        l->state = LISTEN_IDLE;
        return EV_NONE;   // release: existing debounce/think-min reveals the answer
    }
    return EV_SHAKE;
}

bool listen_is_active(const listen_t *l)
{
    return l->state == LISTEN_LISTENING;
}
