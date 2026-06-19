#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pure energy-based endpointer for post-wake question capture. Fed one frame of
// RMS energy at a time (REC_FRAME_SAMPLES worth), it decides when the user has
// finished asking. No hardware/IO: host-testable. Tunables live in config.h
// (VAD_START_MS, VAD_SILENCE_MS, VAD_RMS_THRESHOLD).
typedef struct {
    bool     started;       // have we seen >= VAD_START_MS of speech yet?
    uint32_t speech_ms;     // cumulative speech seen
    uint32_t silence_ms;    // trailing silence since last speech frame
    uint32_t total_ms;      // total time fed (for the hard cap)
} vad_t;

void vad_init(vad_t *v);

// Feed one frame: rms is that frame's RMS, frame_ms its duration. Returns true
// when recording should STOP (utterance complete or hard cap reached). The hard
// cap (REC_MAX_MS) is enforced here so the recorder always terminates.
bool vad_feed(vad_t *v, uint32_t rms, uint32_t frame_ms);

#ifdef __cplusplus
}
#endif
