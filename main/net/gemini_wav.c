#include "gemini_wav.h"

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

size_t wav_write_header(uint8_t *hdr, uint32_t sample_rate, uint32_t samples)
{
    uint32_t data_len = samples * 2;            // 16-bit mono
    uint32_t byte_rate = sample_rate * 2;       // mono * 2 bytes
    hdr[0] = 'R';
    hdr[1] = 'I';
    hdr[2] = 'F';
    hdr[3] = 'F';
    put_u32(hdr + 4, 36 + data_len);
    hdr[8]  = 'W';
    hdr[9]  = 'A';
    hdr[10] = 'V';
    hdr[11] = 'E';
    hdr[12] = 'f';
    hdr[13] = 'm';
    hdr[14] = 't';
    hdr[15] = ' ';
    put_u32(hdr + 16, 16);                      // fmt chunk size
    put_u16(hdr + 20, 1);                       // PCM
    put_u16(hdr + 22, 1);                       // mono
    put_u32(hdr + 24, sample_rate);
    put_u32(hdr + 28, byte_rate);
    put_u16(hdr + 32, 2);                       // block align (mono*2)
    put_u16(hdr + 34, 16);                      // bits per sample
    hdr[36] = 'd';
    hdr[37] = 'a';
    hdr[38] = 't';
    hdr[39] = 'a';
    put_u32(hdr + 40, data_len);
    return 44;
}

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t wav_base64_encode(const uint8_t *in, size_t in_len, char *out)
{
    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= in_len) {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8) | in[i+2];
        out[o++] = B64[(n >> 18) & 0x3f];
        out[o++] = B64[(n >> 12) & 0x3f];
        out[o++] = B64[(n >> 6) & 0x3f];
        out[o++] = B64[n & 0x3f];
        i += 3;
    }
    size_t rem = in_len - i;
    if (rem == 1) {
        uint32_t n = (uint32_t)in[i] << 16;
        out[o++] = B64[(n >> 18) & 0x3f];
        out[o++] = B64[(n >> 12) & 0x3f];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t n = ((uint32_t)in[i] << 16) | ((uint32_t)in[i+1] << 8);
        out[o++] = B64[(n >> 18) & 0x3f];
        out[o++] = B64[(n >> 12) & 0x3f];
        out[o++] = B64[(n >> 6) & 0x3f];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}
