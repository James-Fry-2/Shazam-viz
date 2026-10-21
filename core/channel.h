#ifndef FP_CHANNEL_H
#define FP_CHANNEL_H

#include <stddef.h>

#include "rng.h"

/* High-pass 380 Hz and low-pass 3300 Hz biquads (Q 0.707), without the saturation stage. */
void fp_channel_speaker_filter(float *x, size_t n, int sample_rate);

/* Speaker filter followed by tanh(drive*x)/tanh(drive) with drive 2. */
void fp_channel_speaker(float *x, size_t n, int sample_rate);

/*
 * Direct path at delay 0 with gain 1, plus about 12 taps at delays within 0.5 s.
 * Returns -1 on allocation failure.
 */
int fp_channel_echo(float *x, size_t n, int sample_rate, fp_rng *rng);

/* Adds pink noise whose rms is rms(x) / 10^(snr_db/20). */
void fp_channel_noise(float *x, size_t n, double snr_db, fp_rng *rng);

double fp_rms(const float *x, size_t n);

/* Windowed-sinc, 16 taps per side. Output length is n * sr_out / sr_in, rounded down. */
int fp_resample(const float *in, size_t n, int sr_in, int sr_out, float **out, size_t *n_out);

#endif
