#include "fft.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

struct fp_fft_plan {
    int n;
    uint32_t *rev;
    float *cos_t;
    float *sin_t;
};

fp_fft_plan *fp_fft_create(int n)
{
    if (n < 2 || (n & (n - 1)) != 0)
        return NULL;

    fp_fft_plan *p = malloc(sizeof *p);
    if (!p)
        return NULL;
    p->n = n;
    p->rev = malloc((size_t)n * sizeof *p->rev);
    p->cos_t = malloc((size_t)(n / 2) * sizeof *p->cos_t);
    p->sin_t = malloc((size_t)(n / 2) * sizeof *p->sin_t);
    if (!p->rev || !p->cos_t || !p->sin_t) {
        fp_fft_free(p);
        return NULL;
    }

    int bits = 0;
    while ((1 << bits) < n)
        bits++;
    for (int i = 0; i < n; i++) {
        uint32_t r = 0;
        for (int b = 0; b < bits; b++)
            if (i & (1 << b))
                r |= 1u << (bits - 1 - b);
        p->rev[i] = r;
    }

    for (int k = 0; k < n / 2; k++) {
        double a = -2.0 * M_PI * k / n;
        p->cos_t[k] = (float)cos(a);
        p->sin_t[k] = (float)sin(a);
    }
    return p;
}

void fp_fft_free(fp_fft_plan *p)
{
    if (!p)
        return;
    free(p->rev);
    free(p->cos_t);
    free(p->sin_t);
    free(p);
}

void fp_fft_forward(const fp_fft_plan *p, float *re, float *im)
{
    const int n = p->n;

    for (int i = 0; i < n; i++) {
        int j = (int)p->rev[i];
        if (j > i) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1;
        const int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; k++) {
                const float wr = p->cos_t[k * step];
                const float wi = p->sin_t[k * step];
                const int a = i + k;
                const int b = a + half;
                const float xr = re[b] * wr - im[b] * wi;
                const float xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}
