#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Send recorded PCM (16-bit mono @ MIC_SAMPLE_RATE_HZ) to Gemini in one
// generateContent POST with the GEMINI_PROMPT persona, and copy the model's answer
// into `out` (NUL-terminated, truncated to `cap`). Returns 0 on success, -1 on any
// failure (no key, no network, HTTP error, empty/unparseable answer). Blocks for
// the round-trip; call only from the voice task.
int gemini_ask(const int16_t *pcm, size_t samples, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
