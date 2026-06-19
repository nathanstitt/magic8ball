#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the wake-word + VAD models and the mic pump. 0 on success, -1 on
// failure. On failure the app falls back to shake/tap only.
int  wakeword_init(void);

// Pumps any pending mic audio through the models. Call once per core-0 tick
// BEFORE the accessors below. Cheap when no audio is ready.
void wakeword_update(void);

// True exactly once on the tick the wake phrase is recognized (edge-triggered,
// like touch_was_tapped()).
bool wakeword_detected(void);

// Current voice-activity state from the VAD model (true while speech is present).
bool wakeword_speech_active(void);

// Suppress/allow automatic re-arm of the one-shot detector. While suppressed,
// wakeword_update() will not restart the detector after a hit, leaving the mic free
// for the recorder. Call wakeword_set_armed(false) before a voice ask, true after.
void wakeword_set_armed(bool armed);

#ifdef __cplusplus
}
#endif
