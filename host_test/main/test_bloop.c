#include "unity.h"
#include "bloop.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Per-blip geometry, mirrored from bloop.c so the tests track config changes.
#define TONE_SAMPLES (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_BLIP_MS)
#define GAP_SAMPLES  (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_GAP_MS)
#define SLOT_SAMPLES (TONE_SAMPLES + GAP_SAMPLES)

// Largest legitimate sample-to-sample step. The steepest waveform we generate is
// a full-amplitude sine at the highest frequency reached (the last blip's start),
// whose max slope is A * 2*pi*f / rate. Anything beyond that is a discontinuity,
// i.e. an audible click.
#define MAX_STEP 16000

static int16_t buf[BLOOP_TOTAL_SAMPLES];

TEST_CASE("renders exactly the expected sample count", "[bloop]")
{
    memset(buf, 0x7f, sizeof(buf));
    size_t n = bloop_render(buf, BLOOP_TOTAL_SAMPLES);
    TEST_ASSERT_EQUAL_UINT(BLOOP_TOTAL_SAMPLES, n);
    TEST_ASSERT_EQUAL_UINT(BLOOP_COUNT * SLOT_SAMPLES, n);
}

TEST_CASE("honors a short buffer without overrunning", "[bloop]")
{
    // Guard sample just past the region we allow it to touch.
    static int16_t small[128 + 1];
    const size_t cap = 128;
    small[cap] = 0x5a5a;

    size_t n = bloop_render(small, cap);
    TEST_ASSERT_EQUAL_UINT(cap, n);
    TEST_ASSERT_EQUAL_INT16(0x5a5a, small[cap]);   // never wrote past cap
}

TEST_CASE("never clips and uses most of the headroom", "[bloop]")
{
    bloop_render(buf, BLOOP_TOTAL_SAMPLES);

    int peak = 0;
    for (size_t i = 0; i < BLOOP_TOTAL_SAMPLES; i++) {
        TEST_ASSERT_TRUE(buf[i] >= -32767 && buf[i] <= 32767);
        int a = abs((int)buf[i]);
        if (a > peak) {
            peak = a;
        }
    }
    // At/below the configured ceiling...
    TEST_ASSERT_LESS_OR_EQUAL_INT(BLOOP_PEAK_AMPLITUDE, peak);
    // ...but actually near it: catches an envelope bug that renders everything
    // at a fraction of the intended level (which would be inaudible on-device).
    TEST_ASSERT_GREATER_THAN_INT(BLOOP_PEAK_AMPLITUDE * 9 / 10, peak);
}

TEST_CASE("begins and ends at exact silence", "[bloop]")
{
    bloop_render(buf, BLOOP_TOTAL_SAMPLES);
    // Exact, not approximate: this is what the hard tail fade in the envelope
    // buys us, and it is what keeps the I2S DMA start/stop from clicking.
    TEST_ASSERT_EQUAL_INT16(0, buf[0]);
    TEST_ASSERT_EQUAL_INT16(0, buf[BLOOP_TOTAL_SAMPLES - 1]);
}

TEST_CASE("has no step discontinuities anywhere", "[bloop]")
{
    bloop_render(buf, BLOOP_TOTAL_SAMPLES);
    // The broadest anti-click assertion: catches a missing attack ramp, a missing
    // tail fade, a phase accumulator reset mid-blip, or a bad gap length.
    for (size_t i = 1; i < BLOOP_TOTAL_SAMPLES; i++) {
        int d = abs((int)buf[i] - (int)buf[i - 1]);
        TEST_ASSERT_LESS_THAN_INT(MAX_STEP, d);
    }
}

TEST_CASE("gaps between blips are true silence", "[bloop]")
{
    bloop_render(buf, BLOOP_TOTAL_SAMPLES);
    for (int i = 0; i < BLOOP_COUNT; i++) {
        size_t gap_start = (size_t)i * SLOT_SAMPLES + TONE_SAMPLES;
        for (size_t n = gap_start; n < gap_start + GAP_SAMPLES; n++) {
            TEST_ASSERT_EQUAL_INT16(0, buf[n]);
        }
    }
}

TEST_CASE("contains BLOOP_COUNT distinct energy bursts", "[bloop]")
{
    bloop_render(buf, BLOOP_TOTAL_SAMPLES);

    // Walk in 5ms frames and count contiguous runs of audible frames. This is the
    // structural "bloop bloop bloop" shape, independent of the exact envelope.
    const size_t frame = BLOOP_SAMPLE_RATE_HZ / 1000 * 5;
    const int threshold = BLOOP_PEAK_AMPLITUDE / 20;

    int runs = 0;
    bool in_run = false;
    for (size_t f = 0; f + frame <= BLOOP_TOTAL_SAMPLES; f += frame) {
        int peak = 0;
        for (size_t n = f; n < f + frame; n++) {
            int a = abs((int)buf[n]);
            if (a > peak) {
                peak = a;
            }
        }
        bool loud = (peak > threshold);
        if (loud && !in_run) {
            runs++;
        }
        in_run = loud;
    }
    TEST_ASSERT_EQUAL_INT(BLOOP_COUNT, runs);
}

// Zero-crossing rate over [start,end) as a cheap frequency estimate (no FFT).
static int freq_est(size_t start, size_t end)
{
    int crossings = 0;
    for (size_t n = start + 1; n < end; n++) {
        if ((buf[n - 1] < 0) != (buf[n] < 0)) {
            crossings++;
        }
    }
    return crossings * BLOOP_SAMPLE_RATE_HZ / (2 * (int)(end - start));
}

TEST_CASE("every blip stays inside the speaker's usable band", "[bloop]")
{
    bloop_render(buf, BLOOP_TOTAL_SAMPLES);

    // Guards the design constraint that a micro speaker in a plastic ball has
    // almost no output below ~450 Hz: if someone retunes the pitch constants down,
    // the bloop goes thin or inaudible ON THE DEVICE while still sounding fine in
    // headphones -- exactly the kind of regression nobody catches by ear at a desk.
    //
    // Measure each blip's ENDPOINTS, not its average. Every blip glides downward,
    // so the average sits comfortably mid-band and would happily hide an endpoint
    // that has already fallen off the speaker's cliff.
    const int eighth = TONE_SAMPLES / 8;
    for (int i = 0; i < BLOOP_COUNT; i++) {
        size_t tone_start = (size_t)i * SLOT_SAMPLES;
        // Skip the attack ramp at the head and the near-silent decay at the tail,
        // where crossing detection gets noisy.
        int f_start = freq_est(tone_start + (BLOOP_SAMPLE_RATE_HZ / 1000 * BLOOP_ATTACK_MS),
                               tone_start + eighth);
        int f_end = freq_est(tone_start + TONE_SAMPLES - 2 * eighth,
                             tone_start + TONE_SAMPLES - eighth);

        // The bottom of the sweep is the number that actually matters.
        TEST_ASSERT_GREATER_THAN_INT(450, f_end);
        TEST_ASSERT_LESS_THAN_INT(1600, f_start);
        // And it really is a DOWNWARD glide -- that is what makes it a "bloop".
        TEST_ASSERT_GREATER_THAN_INT(f_end, f_start);
    }
}

TEST_CASE("is deterministic", "[bloop]")
{
    static int16_t a[BLOOP_TOTAL_SAMPLES];
    static int16_t b[BLOOP_TOTAL_SAMPLES];
    bloop_render(a, BLOOP_TOTAL_SAMPLES);
    bloop_render(b, BLOOP_TOTAL_SAMPLES);
    // Rendering once at boot is only safe because this holds.
    TEST_ASSERT_EQUAL_INT16_ARRAY(a, b, BLOOP_TOTAL_SAMPLES);
}
