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
            l->silence_ms = 0;
            return EV_SHAKE;
        }
        return EV_NONE;

    case LISTEN_LISTENING:
        return EV_SHAKE;   // refined in later tasks
    }
    return EV_NONE;
}
