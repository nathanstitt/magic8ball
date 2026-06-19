#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Write a 44-byte canonical PCM WAV header for `samples` of 16-bit mono audio at
// `sample_rate` into `hdr` (must be >= 44 bytes). Returns 44.
size_t wav_write_header(uint8_t *hdr, uint32_t sample_rate, uint32_t samples);

// Base64-encode `in_len` bytes of `in` into `out` (NUL-terminated). `out` must be
// at least wav_base64_len(in_len) bytes. Returns the encoded length (excl. NUL).
size_t wav_base64_encode(const uint8_t *in, size_t in_len, char *out);

// Required `out` capacity (incl. NUL) for base64 of `in_len` bytes.
static inline size_t wav_base64_len(size_t in_len)
{
    return ((in_len + 2) / 3) * 4 + 1;
}

#ifdef __cplusplus
}
#endif
