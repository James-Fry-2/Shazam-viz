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
    /* dB relative to the |X| of a full-scale sine under the Hann window (window_size / 4) */
    float min_magnitude_db;

    int fan_out;
    int min_dt;
    int max_dt;
    int max_df;

    int offset_bin;
    /* Scoring only: 1 scores each bin alone, 2 scores each track by its best sum of bins b and b+1. */
    int score_window;
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
    c.threshold = 6.0f;
    c.min_magnitude_db = -80.0f;

    c.fan_out = 5;
    c.min_dt = 5;
    c.max_dt = 63;
    c.max_df = 128;

    c.offset_bin = 1;
    c.score_window = 1;
    c.min_score = 5;
    c.min_ratio = 2.0f;
    return c;
}

#endif
