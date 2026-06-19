#include "unity.h"
#include "vad.h"
#include "config.h"

#define FMS 30  // frame duration in ms (30ms @ 16kHz matches REC_FRAME_SAMPLES)

// Feed frames until vad_feed returns true, or max_frames is reached.
// Returns number of frames fed when it stopped, or 0 if it never stopped.
static int feed_until_done(vad_t *v, uint32_t rms, int max_frames)
{
    for (int i = 1; i <= max_frames; i++) {
        if (vad_feed(v, rms, FMS)) {
            return i;
        }
    }
    return 0;
}

TEST_CASE("silence only runs to the hard cap", "[vad]")
{
    vad_t v;
    vad_init(&v);

    // How many 30ms frames fit in REC_MAX_MS
    int cap_frames = REC_MAX_MS / FMS;

    // Feed silence well beyond the VAD_SILENCE_MS window — it must NOT trigger
    // the silence rule (never started), only the hard cap.
    // We give it up to 2x the cap to confirm it only fires at/after the cap.
    int stopped = feed_until_done(&v, 0, cap_frames * 2);

    // It must have stopped (not run forever).
    TEST_ASSERT_NOT_EQUAL(0, stopped);

    // It must NOT have stopped before the hard cap was reached.
    // cap_frames frames * FMS ms = REC_MAX_MS, so stop frame >= cap_frames.
    TEST_ASSERT_GREATER_OR_EQUAL(cap_frames, stopped);

    // And it must not have fired before the hard-cap boundary (silence rule
    // would fire much earlier if started were ever set incorrectly).
    TEST_ASSERT_FALSE(v.started);
}

TEST_CASE("speech then silence ends recording", "[vad]")
{
    vad_t v;
    vad_init(&v);

    uint32_t loud = VAD_RMS_THRESHOLD + 100;

    // Feed enough loud frames to cross VAD_START_MS.
    int speech_frames = (VAD_START_MS / FMS) + 2;
    for (int i = 0; i < speech_frames; i++) {
        bool done = vad_feed(&v, loud, FMS);
        TEST_ASSERT_FALSE(done);  // must not end during speech
    }
    TEST_ASSERT_TRUE(v.started);

    // Now feed silence; it should stop within ~VAD_SILENCE_MS.
    // Allow up to 2x VAD_SILENCE_MS worth of frames as a generous bound.
    int max_silence_frames = (VAD_SILENCE_MS * 2) / FMS;
    int stopped = feed_until_done(&v, 0, max_silence_frames);

    TEST_ASSERT_NOT_EQUAL(0, stopped);  // must have stopped
    // Total silence fed at stop <= VAD_SILENCE_MS + one frame (boundary rounding)
    uint32_t silence_fed = (uint32_t)stopped * FMS;
    TEST_ASSERT_LESS_OR_EQUAL(VAD_SILENCE_MS + FMS, silence_fed);
}

TEST_CASE("brief blip does not end recording early", "[vad]")
{
    vad_t v;
    vad_init(&v);

    uint32_t loud = VAD_RMS_THRESHOLD + 100;

    // ONE loud frame — shorter than VAD_START_MS so started stays false.
    TEST_ASSERT_LESS_THAN(VAD_START_MS, FMS);
    bool done = vad_feed(&v, loud, FMS);
    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_FALSE(v.started);

    // Feed silence for a full VAD_SILENCE_MS window.
    int silence_frames = VAD_SILENCE_MS / FMS;
    for (int i = 0; i < silence_frames; i++) {
        done = vad_feed(&v, 0, FMS);
        // Must NOT stop: silence rule requires started == true.
        TEST_ASSERT_FALSE(done);
    }
    TEST_ASSERT_FALSE(v.started);
}
