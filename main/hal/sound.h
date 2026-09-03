#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Speaker output (ES8311 over the I2S bus shared with the wake-word mic).
// Currently exactly one cue: the answer-reveal bloop, synthesized by scene/bloop.c.

// Pre-render the bloop PCM and start the audio task. Returns 0 on success, -1 if
// the board has no usable speaker -- in which case sound is a permanent no-op and
// sound_play_bloop() stays safe to call. Sound is strictly optional: every failure
// path here degrades to a silent (but fully working) ball.
//
// Call AFTER wakeword_init(): the speaker and mic share one I2S peripheral, and
// esp_codec_dev rejects an output open whose sample rate differs from an
// already-open input. Initializing second means the bus is already pinned to the
// rate the bloop is rendered at.
int  sound_init(void);

// Ask the audio task to play the bloop. NON-BLOCKING: a NULL check, a flag check
// and one task notification, bounded at microseconds. Safe to call from the logic
// loop, whose tick budget (DT_MAX_MS) must never absorb a blocking I2S write --
// the ~450ms write happens on the audio task, at a priority below logic/render.
// If a bloop is already playing the request is dropped (no queueing, no overlap).
void sound_play_bloop(void);

#ifdef __cplusplus
}
#endif
