#include "vad.h"
#include "config.h"
#include <stddef.h>

void vad_init(vad_t *v)
{
    if (!v) {
        return;
    }
    v->started = false;
    v->speech_ms = 0;
    v->silence_ms = 0;
    v->total_ms = 0;
}

bool vad_feed(vad_t *v, uint32_t rms, uint32_t frame_ms)
{
    if (!v) {
        return true;
    }
    v->total_ms += frame_ms;

    bool speech = rms >= VAD_RMS_THRESHOLD;
    if (speech) {
        v->speech_ms += frame_ms;
        v->silence_ms = 0;
        if (v->speech_ms >= VAD_START_MS) {
            v->started = true;
        }
    } else {
        v->silence_ms += frame_ms;
    }

    // Hard cap: always terminate.
    if (v->total_ms >= REC_MAX_MS) {
        return true;
    }
    // Normal end: enough speech seen, then enough trailing quiet.
    if (v->started && v->silence_ms >= VAD_SILENCE_MS) {
        return true;
    }
    return false;
}
