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

#ifdef __cplusplus
}
#endif
