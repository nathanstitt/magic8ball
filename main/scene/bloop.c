#include "bloop.h"
#include <math.h>

// Samples per blip: the tone itself, its trailing silence, and the whole slot.
#define BLIP_TONE_SAMPLES  (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_BLIP_MS)
#define BLIP_GAP_SAMPLES   (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_GAP_MS)
#define BLIP_SLOT_SAMPLES  (BLIP_TONE_SAMPLES + BLIP_GAP_SAMPLES)
#define BLIP_ATTACK_SAMPLES (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_ATTACK_MS)
#define BLIP_TAIL_SAMPLES   (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_TAIL_MS)

#define TWO_PI 6.28318530717958647692f

// Amplitude envelope for sample `n` of a blip's tone, in 0..1.
//
// Linear attack, exponential decay, then a hard linear fade over the final
// BLIP_TAIL_SAMPLES. Each part earns its place:
//   - the attack kills the step edge at note-on (a gated sine clicks);
//   - the exponential decay is what makes it read as a percussive "bloop"
//     rather than a sustained tone;
//   - the tail fade forces the very last sample to EXACTLY 0. The decay alone
//     only reaches e^-BLOOP_DECAY_K (~0.7% of peak, ~220 LSB), which is still a
//     small step edge when the DMA stops. Ending at a literal zero is also what
//     lets the host tests assert silence exactly rather than approximately.
static float envelope(int n)
{
    float env;
    if (n < BLIP_ATTACK_SAMPLES) {
        env = (float)n / (float)BLIP_ATTACK_SAMPLES;
    } else {
        float d = (float)(n - BLIP_ATTACK_SAMPLES) /
                  (float)(BLIP_TONE_SAMPLES - BLIP_ATTACK_SAMPLES);
        env = expf(-BLOOP_DECAY_K * d);
    }
    int tail_start = BLIP_TONE_SAMPLES - BLIP_TAIL_SAMPLES;
    if (n >= tail_start) {
        float t = (float)(n - tail_start) / (float)BLIP_TAIL_SAMPLES;
        env *= (1.0f - t);
    }
    return env;
}

size_t bloop_render_blip(int i, int16_t *out, size_t max)
{
    if (out == NULL || i < 0 || i >= BLOOP_COUNT) {
        return 0;
    }

    // This blip's starting pitch: each successive blip is transposed up a step.
    float f = BLOOP_BASE_HZ;
    for (int s = 0; s < i; s++) {
        f *= BLOOP_STEP_RATIO;
    }

    // Per-sample multiplier that glides f down to (f * BLOOP_SWEEP_RATIO) across
    // the tone. Pitch is perceived logarithmically, so the glide is exponential:
    // a linear Hz ramp audibly "slows down" at the end. Folding it into one
    // multiply per sample also keeps this loop free of powf().
    float ratio = powf(BLOOP_SWEEP_RATIO, 1.0f / (float)BLIP_TONE_SAMPLES);

    // Phase accumulator. Integrating the instantaneous frequency is the only
    // correct way to sweep: sinf(TWO_PI * f(t) * t) would produce both the wrong
    // instantaneous pitch and a discontinuity every time f changes.
    float phase = 0.0f;

    size_t n = 0;
    while (n < (size_t)BLIP_TONE_SAMPLES && n < max) {
        float v = sinf(phase) * envelope((int)n) * (float)BLOOP_PEAK_AMPLITUDE;
        // sinf can overshoot 1.0 by an ULP, so clamp. The rails are symmetric
        // (not -32768) to keep "never clips" a clean, provable assertion.
        if (v > 32767.0f) {
            v = 32767.0f;
        }
        if (v < -32767.0f) {
            v = -32767.0f;
        }
        out[n] = (int16_t)v;

        phase += TWO_PI * f / (float)BLOOP_SAMPLE_RATE_HZ;
        if (phase >= TWO_PI) {
            phase -= TWO_PI;
        }
        f *= ratio;
        n++;
    }

    // Trailing silence.
    while (n < (size_t)BLIP_SLOT_SAMPLES && n < max) {
        out[n] = 0;
        n++;
    }
    return n;
}

size_t bloop_render(int16_t *out, size_t max)
{
    if (out == NULL) {
        return 0;
    }
    size_t total = 0;
    for (int i = 0; i < BLOOP_COUNT; i++) {
        if (total >= max) {
            break;
        }
        total += bloop_render_blip(i, out + total, max - total);
    }
    return total;
}
