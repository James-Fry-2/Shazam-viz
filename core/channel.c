#include "channel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SPEAKER_HP_HZ 380.0
#define SPEAKER_LP_HZ 3300.0
#define SPEAKER_Q 0.7071067811865476
#define SPEAKER_DRIVE 2.0

#define ECHO_TAPS 12
#define ECHO_MAX_DELAY_S 0.5
#define ECHO_GAIN 0.6
#define ECHO_DECAY 3.0

typedef struct {
    double b0, b1, b2, a1, a2;
} biquad;

/* RBJ audio-EQ-cookbook coefficients, normalised by a0 */
static biquad biquad_design(int high_pass, double fc, double q, int sample_rate)
{
    const double w = 2.0 * M_PI * fc / sample_rate;
    const double alpha = sin(w) / (2.0 * q);
    const double c = cos(w);
    const double a0 = 1.0 + alpha;
    biquad b;
    if (high_pass) {
        b.b0 = (1.0 + c) / 2.0 / a0;
        b.b1 = -(1.0 + c) / a0;
        b.b2 = b.b0;
    } else {
        b.b0 = (1.0 - c) / 2.0 / a0;
        b.b1 = (1.0 - c) / a0;
        b.b2 = b.b0;
    }
    b.a1 = -2.0 * c / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

/* Transposed direct form II */
static void biquad_run(const biquad *b, float *x, size_t n)
{
    double z1 = 0.0, z2 = 0.0;
    for (size_t i = 0; i < n; i++) {
        const double in = x[i];
        const double out = b->b0 * in + z1;
        z1 = b->b1 * in - b->a1 * out + z2;
        z2 = b->b2 * in - b->a2 * out;
        x[i] = (float)out;
    }
}

void fp_channel_speaker_filter(float *x, size_t n, int sample_rate)
{
    const biquad hp = biquad_design(1, SPEAKER_HP_HZ, SPEAKER_Q, sample_rate);
    const biquad lp = biquad_design(0, SPEAKER_LP_HZ, SPEAKER_Q, sample_rate);
    biquad_run(&hp, x, n);
    biquad_run(&lp, x, n);
}

void fp_channel_speaker(float *x, size_t n, int sample_rate)
{
    fp_channel_speaker_filter(x, n, sample_rate);
    const double norm = 1.0 / tanh(SPEAKER_DRIVE);
    for (size_t i = 0; i < n; i++)
        x[i] = (float)(tanh(SPEAKER_DRIVE * x[i]) * norm);
}

int fp_channel_echo(float *x, size_t n, int sample_rate, fp_rng *rng)
{
    size_t delay[ECHO_TAPS];
    float gain[ECHO_TAPS];
    for (int k = 0; k < ECHO_TAPS; k++) {
        const double d = ECHO_MAX_DELAY_S * (0.02 + 0.98 * fp_rng_f32(rng));
        delay[k] = (size_t)(d * sample_rate);
        if (delay[k] == 0)
            delay[k] = 1;
        const double sign = (fp_rng_u64(rng) & 1) ? 1.0 : -1.0;
        gain[k] = (float)(sign * ECHO_GAIN * exp(-ECHO_DECAY * d / ECHO_MAX_DELAY_S));
    }
    float *out = malloc((n ? n : 1) * sizeof *out);
    if (!out)
        return -1;
    memcpy(out, x, n * sizeof *out);
    for (int k = 0; k < ECHO_TAPS; k++)
        for (size_t i = delay[k]; i < n; i++)
            out[i] += gain[k] * x[i - delay[k]];
    memcpy(x, out, n * sizeof *x);
    free(out);
    return 0;
}

double fp_rms(const float *x, size_t n)
{
    if (!n)
        return 0.0;
    double s = 0.0;
    for (size_t i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (double)n);
}

/* Paul Kellet's economy pink-noise filter over uniform white noise */
static void pink_fill(float *y, size_t n, fp_rng *rng)
{
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (size_t i = 0; i < n; i++) {
        const double w = 2.0 * (double)fp_rng_f32(rng) - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        y[i] = (float)(b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
        b6 = w * 0.115926;
    }
}

void fp_channel_noise(float *x, size_t n, double snr_db, fp_rng *rng)
{
    float *y = malloc((n ? n : 1) * sizeof *y);
    if (!y)
        return;
    pink_fill(y, n, rng);
    const double ry = fp_rms(y, n);
    if (ry > 0.0) {
        const double scale = fp_rms(x, n) / pow(10.0, snr_db / 20.0) / ry;
        for (size_t i = 0; i < n; i++)
            x[i] += (float)(scale * y[i]);
    }
    free(y);
}
