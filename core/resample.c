#include <math.h>
#include <stdlib.h>

#include "channel.h"

#define HALF_TAPS 16
#define KAISER_BETA 8.0

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 50; k++) {
        term *= q / ((double)k * k);
        sum += term;
    }
    return sum;
}

static double sinc(double x)
{
    if (fabs(x) < 1e-12)
        return 1.0;
    return sin(M_PI * x) / (M_PI * x);
}

int fp_resample(const float *in, size_t n, int sr_in, int sr_out, float **out, size_t *n_out)
{
    const size_t m = (size_t)((double)n * sr_out / sr_in);
    float *y = malloc((m ? m : 1) * sizeof *y);
    if (!y)
        return -1;
    if (sr_in == sr_out) {
        for (size_t i = 0; i < m; i++)
            y[i] = in[i];
        *out = y;
        *n_out = m;
        return 0;
    }

    /* When decimating, the sinc cutoff drops with the output rate and the kernel widens to keep 16 taps per side. */
    const double ratio = (double)sr_in / sr_out;
    const double cutoff = ratio > 1.0 ? 1.0 / ratio : 1.0;
    const double half_width = HALF_TAPS * (ratio > 1.0 ? ratio : 1.0);
    const double inv_i0 = 1.0 / bessel_i0(KAISER_BETA);

    for (size_t i = 0; i < m; i++) {
        const double centre = (double)i * ratio;
        long lo = (long)ceil(centre - half_width);
        long hi = (long)floor(centre + half_width);
        double acc = 0.0, wsum = 0.0;
        for (long j = lo; j <= hi; j++) {
            const double d = (double)j - centre;
            const double u = d / half_width;
            const double win = bessel_i0(KAISER_BETA * sqrt(1.0 - u * u)) * inv_i0;
            const double k = cutoff * sinc(d * cutoff) * win;
            wsum += k;
            if (j >= 0 && (size_t)j < n)
                acc += k * in[j];
        }
        y[i] = (float)(wsum != 0.0 ? acc / wsum : 0.0);
    }
    *out = y;
    *n_out = m;
    return 0;
}
