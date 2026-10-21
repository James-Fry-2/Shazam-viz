#ifndef FP_CONFIG_H
#define FP_CONFIG_H

#include <stdint.h>

#define FP_MAX_BANDS 16

typedef struct {
    int sample_rate;
    int window_size;
    int hop_size;

    int num_bands;
    float band_lo_hz;
    float band_hi_hz;
    float threshold;
    float min_magnitude;

    int fan_out;
    int min_dt;
    int max_dt;
    int max_df;

    int offset_bin;
    int min_score;
    float min_ratio;
} fp_config;

static inline fp_config fp_config_default(void)
{
    fp_config c;
    c.sample_rate = 8000;
    c.window_size = 1024;
    c.hop_size = 512;

    c.num_bands = 6;
    c.band_lo_hz = 100.0f;
    c.band_hi_hz = 4000.0f;
    c.threshold = 4.0f;
    c.min_magnitude = 1e-3f;

    c.fan_out = 5;
    c.min_dt = 1;
    c.max_dt = 63;
    c.max_df = 128;

    c.offset_bin = 1;
    c.min_score = 5;
    c.min_ratio = 2.0f;
    return c;
}

#endif
