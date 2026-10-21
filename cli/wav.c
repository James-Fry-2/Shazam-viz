#include "wav.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channel.h"

#define FMT_PCM 1
#define FMT_FLOAT 3
#define FMT_EXTENSIBLE 0xFFFE

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *buf = NULL;
    size_t cap = 0, n = 0;
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2 : 1 << 16;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0)
            break;
    }
    fclose(f);
    *len = n;
    return buf;
}

int wav_read(const char *path, float **samples, size_t *n, int *sample_rate)
{
    size_t len;
    uint8_t *buf = slurp(path, &len);
    if (!buf)
        return -1;
    if (len < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        free(buf);
        return -1;
    }

    int format = 0, channels = 0, bits = 0, rate = 0;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t *h = buf + pos;
        size_t sz = le32(h + 4);
        const uint8_t *body = h + 8;
        if (sz > len - pos - 8)
            sz = len - pos - 8;
        if (!memcmp(h, "fmt ", 4) && sz >= 16) {
            format = (int)le16(body);
            channels = (int)le16(body + 2);
            rate = (int)le32(body + 4);
            bits = (int)le16(body + 14);
            if (format == FMT_EXTENSIBLE && sz >= 26)
                format = (int)le16(body + 24);
        } else if (!memcmp(h, "data", 4)) {
            data = body;
            data_len = sz;
            break;
        }
        pos += 8 + sz + (sz & 1);
    }

    const int ok_pcm = format == FMT_PCM && bits == 16;
    const int ok_float = format == FMT_FLOAT && bits == 32;
    if (!data || channels < 1 || rate < 1 || !(ok_pcm || ok_float)) {
        free(buf);
        return -1;
    }

    const size_t bytes = (size_t)bits / 8;
    const size_t frames = data_len / (bytes * (size_t)channels);
    float *x = malloc((frames ? frames : 1) * sizeof *x);
    if (!x) {
        free(buf);
        return -1;
    }
    for (size_t i = 0; i < frames; i++) {
        double acc = 0.0;
        for (int c = 0; c < channels; c++) {
            const uint8_t *p = data + (i * (size_t)channels + (size_t)c) * bytes;
            if (ok_pcm) {
                acc += (int16_t)le16(p) / 32768.0;
            } else {
                uint32_t u = le32(p);
                float v;
                memcpy(&v, &u, sizeof v);
                acc += v;
            }
        }
        x[i] = (float)(acc / channels);
    }
    free(buf);
    *samples = x;
    *n = frames;
    *sample_rate = rate;
    return 0;
}

int wav_read_at_rate(const char *path, int target_rate, float **samples, size_t *n)
{
    float *x;
    size_t len;
    int rate;
    if (wav_read(path, &x, &len, &rate))
        return -1;
    if (rate == target_rate) {
        *samples = x;
        *n = len;
        return 0;
    }
    int rc = fp_resample(x, len, rate, target_rate, samples, n);
    free(x);
    return rc;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}

int wav_write(const char *path, const float *samples, size_t n, int sample_rate)
{
    const uint32_t data_len = (uint32_t)(n * 2);
    uint8_t *buf = malloc(44 + n * 2);
    if (!buf)
        return -1;
    memcpy(buf, "RIFF", 4);
    put32(buf + 4, 36 + data_len);
    memcpy(buf + 8, "WAVEfmt ", 8);
    put32(buf + 16, 16);
    put16(buf + 20, FMT_PCM);
    put16(buf + 22, 1);
    put32(buf + 24, (uint32_t)sample_rate);
    put32(buf + 28, (uint32_t)sample_rate * 2);
    put16(buf + 32, 2);
    put16(buf + 34, 16);
    memcpy(buf + 36, "data", 4);
    put32(buf + 40, data_len);
    for (size_t i = 0; i < n; i++) {
        double v = samples[i] * 32768.0;
        if (v > 32767.0)
            v = 32767.0;
        if (v < -32768.0)
            v = -32768.0;
        put16(buf + 44 + i * 2, (uint16_t)(int16_t)lrint(v));
    }
    FILE *f = fopen(path, "wb");
    int rc = -1;
    if (f) {
        rc = fwrite(buf, 1, 44 + n * 2, f) == 44 + n * 2 ? 0 : -1;
        if (fclose(f))
            rc = -1;
    }
    free(buf);
    return rc;
}
