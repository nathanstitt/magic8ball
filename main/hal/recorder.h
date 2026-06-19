#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Record the user's question after wake. Reads the mic in REC_FRAME_SAMPLES frames,
// runs the energy VAD, and stops on end-of-speech or the REC_MAX_MS cap. Writes
// captured 16-bit mono samples into `buf` (capacity `cap` samples). On return,
// *out_samples holds how many were captured. Returns 0 on success (>= REC_MIN_SAMPLES
// captured), -1 if too little usable audio was captured (caller should fall back).
//
// Blocks for the duration of the recording (run on the dedicated voice task, never
// the logic loop). The mic must be exclusively owned by the caller for this window
// (wake-word re-arm suppressed).
int recorder_capture(int16_t *buf, size_t cap, size_t *out_samples);

#ifdef __cplusplus
}
#endif
