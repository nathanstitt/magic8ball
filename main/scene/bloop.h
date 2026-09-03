#pragma once
#include "config.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Procedural "bloop bloop bloop" answer-reveal cue: BLOOP_COUNT short sine blips,
// each gliding DOWN in pitch (the glide is what makes it a bloop rather than a
// beep) while successive blips are transposed UP, so the gesture as a whole rises.
// Rendered as 16-bit mono PCM at BLOOP_SAMPLE_RATE_HZ. Pure math -- no hardware,
// no allocation, no RNG, no static state: host-testable, and every call produces
// bit-identical output. All tunables live in config.h. The hardware side (codec
// handle, playback task) is hal/sound.cpp.

// Fill `out` with the whole sequence. `max` is the capacity of `out` in SAMPLES.
// Writes min(max, BLOOP_TOTAL_SAMPLES) samples and returns that count; anything
// in `out` beyond what it returns is left untouched.
//
// The result is guaranteed to start and end at exactly 0 and to contain no step
// discontinuities, so feeding it straight to an I2S DMA cannot click.
size_t bloop_render(int16_t *out, size_t max);

// Render blip `i` (0..BLOOP_COUNT-1) alone, including its trailing silent gap.
// bloop_render() is just this run back-to-back for every i; it is exposed so host
// tests can assert per-blip envelope and pitch properties. hal/sound.cpp only ever
// calls bloop_render().
size_t bloop_render_blip(int i, int16_t *out, size_t max);

#ifdef __cplusplus
}
#endif
