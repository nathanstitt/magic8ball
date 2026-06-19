#include "unity.h"
#include "gemini_wav.h"
#include <string.h>

TEST_CASE("WAV header tags and sizes are correct", "[gemini_wav]")
{
    uint8_t hdr[44];
    uint32_t sample_rate = 16000;
    uint32_t samples = 16000;
    size_t ret = wav_write_header(hdr, sample_rate, samples);

    TEST_ASSERT_EQUAL(44, ret);

    // RIFF tag
    TEST_ASSERT_EQUAL('R', hdr[0]);
    TEST_ASSERT_EQUAL('I', hdr[1]);
    TEST_ASSERT_EQUAL('F', hdr[2]);
    TEST_ASSERT_EQUAL('F', hdr[3]);

    // WAVE tag
    TEST_ASSERT_EQUAL('W', hdr[8]);
    TEST_ASSERT_EQUAL('A', hdr[9]);
    TEST_ASSERT_EQUAL('V', hdr[10]);
    TEST_ASSERT_EQUAL('E', hdr[11]);

    // fmt  tag
    TEST_ASSERT_EQUAL('f', hdr[12]);
    TEST_ASSERT_EQUAL('m', hdr[13]);
    TEST_ASSERT_EQUAL('t', hdr[14]);
    TEST_ASSERT_EQUAL(' ', hdr[15]);

    // data tag
    TEST_ASSERT_EQUAL('d', hdr[36]);
    TEST_ASSERT_EQUAL('a', hdr[37]);
    TEST_ASSERT_EQUAL('t', hdr[38]);
    TEST_ASSERT_EQUAL('a', hdr[39]);

    // data chunk size at offset 40 (16000 samples * 2 bytes = 32000)
    uint32_t data_len = (uint32_t)hdr[40]
                      | ((uint32_t)hdr[41] << 8)
                      | ((uint32_t)hdr[42] << 16)
                      | ((uint32_t)hdr[43] << 24);
    TEST_ASSERT_EQUAL_UINT32(32000, data_len);

    // RIFF chunk size at offset 4 (36 + 32000 = 32036)
    uint32_t riff_size = (uint32_t)hdr[4]
                       | ((uint32_t)hdr[5] << 8)
                       | ((uint32_t)hdr[6] << 16)
                       | ((uint32_t)hdr[7] << 24);
    TEST_ASSERT_EQUAL_UINT32(36 + 32000, riff_size);
}

TEST_CASE("base64 known vector Man -> TWFu", "[gemini_wav]")
{
    char out[8];
    size_t len = wav_base64_encode((const uint8_t *)"Man", 3, out);

    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_STRING("TWFu", out);
}

TEST_CASE("base64 padding: M -> TQ==, Ma -> TWE=", "[gemini_wav]")
{
    char out[8];
    size_t len;

    len = wav_base64_encode((const uint8_t *)"M", 1, out);
    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_STRING("TQ==", out);

    len = wav_base64_encode((const uint8_t *)"Ma", 2, out);
    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_STRING("TWE=", out);
}
