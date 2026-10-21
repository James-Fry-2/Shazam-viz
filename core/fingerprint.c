#include "fingerprint.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fft.h"

struct fp_fingerprinter {
    fp_config cfg;
    fp_fft_plan *plan;
    float *window;
    float *re;
    float *im;
    float *mag;
    float min_mag;
    int band_lo[FP_MAX_BANDS];
    int band_hi[FP_MAX_BANDS];
};

static int is_pow2(int v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

int fp_config_validate(const fp_config *c)
{
    if (c->sample_rate <= 0)
        return -1;
    if (!is_pow2(c->window_size) || c->window_size < 16 || c->window_size / 2 - 1 > FP_MAX_F)
        return -1;
    if (c->hop_size <= 0 || c->hop_size > c->window_size)
        return -1;
    if (c->num_bands < 1 || c->num_bands > FP_MAX_BANDS)
        return -1;
    if (!(c->band_lo_hz > 0.0f) || !(c->band_hi_hz > c->band_lo_hz))
        return -1;
    if (c->fan_out < 1 || c->min_dt < 0 || c->max_dt < c->min_dt || c->max_dt > 63)
        return -1;
    if (c->max_df < 0 || c->max_df > FP_MAX_F)
        return -1;
    if (c->offset_bin < 1)
        return -1;
    return 0;
}

static void compute_bands(fp_fingerprinter *f)
{
    const fp_config *c = &f->cfg;
    const int max_bin = c->window_size / 2 - 1;
    const double bin_hz = (double)c->sample_rate / c->window_size;
    const double lo = c->band_lo_hz / bin_hz;
    const double hi = c->band_hi_hz / bin_hz;
    int edge[FP_MAX_BANDS + 1];

    for (int k = 0; k <= c->num_bands; k++) {
        double e = lo * pow(hi / lo, (double)k / c->num_bands);
        int v = (int)lround(e);
        if (v < 1)
            v = 1;
        if (v > max_bin)
            v = max_bin;
        edge[k] = v;
    }

    /* Bands are [edge[k], edge[k+1]) except the last, which includes its upper edge; each keeps at least one bin. */
    for (int k = 0; k < c->num_bands; k++) {
        f->band_lo[k] = edge[k];
        int h = (k == c->num_bands - 1) ? edge[k + 1] : edge[k + 1] - 1;
        if (h < f->band_lo[k])
            h = f->band_lo[k];
        f->band_hi[k] = h;
    }
}

fp_fingerprinter *fp_fingerprinter_create(const fp_config *c)
{
    if (fp_config_validate(c) != 0)
        return NULL;

    fp_fingerprinter *f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->cfg = *c;
    const int n = c->window_size;
    f->plan = fp_fft_create(n);
    f->window = malloc((size_t)n * sizeof *f->window);
    f->re = malloc((size_t)n * sizeof *f->re);
    f->im = malloc((size_t)n * sizeof *f->im);
    f->mag = malloc((size_t)(n / 2 + 1) * sizeof *f->mag);
    if (!f->plan || !f->window || !f->re || !f->im || !f->mag) {
        fp_fingerprinter_free(f);
        return NULL;
    }

    /* Periodic Hann: exact overlap-add at 50% hop. */
    for (int i = 0; i < n; i++)
        f->window[i] = (float)(0.5 - 0.5 * cos(2.0 * M_PI * i / n));
    f->min_mag = (float)(n / 4.0 * pow(10.0, c->min_magnitude_db / 20.0));
    compute_bands(f);
    return f;
}

void fp_fingerprinter_free(fp_fingerprinter *f)
{
    if (!f)
        return;
    fp_fft_free(f->plan);
    free(f->window);
    free(f->re);
    free(f->im);
    free(f->mag);
    free(f);
}

int fp_analyze(fp_fingerprinter *f, const float *x, size_t n, int keep_spec, fp_analysis *out)
{
    const fp_config *c = &f->cfg;
    const int win = c->window_size;
    const int bins = win / 2 + 1;

    memset(out, 0, sizeof *out);
    out->bins = bins;
    if (n < (size_t)win)
        return 0;

    const int frames = 1 + (int)((n - (size_t)win) / (size_t)c->hop_size);
    out->frames = frames;
    if (keep_spec) {
        out->spec = malloc((size_t)frames * bins * sizeof *out->spec);
        if (!out->spec)
            return -1;
    }

    size_t cap = 256;
    fp_peak *peaks = malloc(cap * sizeof *peaks);
    if (!peaks) {
        free(out->spec);
        out->spec = NULL;
        return -1;
    }
    size_t np = 0;

    for (int t = 0; t < frames; t++) {
        const float *src = x + (size_t)t * c->hop_size;
        for (int i = 0; i < win; i++) {
            f->re[i] = src[i] * f->window[i];
            f->im[i] = 0.0f;
        }
        fp_fft_forward(f->plan, f->re, f->im);

        for (int k = 0; k < bins; k++)
            f->mag[k] = sqrtf(f->re[k] * f->re[k] + f->im[k] * f->im[k]);

        if (keep_spec) {
            float *row = out->spec + (size_t)t * bins;
            for (int k = 0; k < bins; k++)
                row[k] = log1pf(f->mag[k]);
        }

        for (int b = 0; b < c->num_bands; b++) {
            const int lo = f->band_lo[b];
            const int hi = f->band_hi[b];
            int best = lo;
            double sum = 0.0;
            for (int k = lo; k <= hi; k++) {
                sum += f->mag[k];
                if (f->mag[k] > f->mag[best])
                    best = k;
            }
            const float mean = (float)(sum / (hi - lo + 1));
            const float m = f->mag[best];
            if (!(m > c->threshold * mean && m > f->min_mag))
                continue;

            if (np == cap) {
                cap *= 2;
                fp_peak *grown = realloc(peaks, cap * sizeof *peaks);
                if (!grown) {
                    free(peaks);
                    free(out->spec);
                    out->spec = NULL;
                    return -1;
                }
                peaks = grown;
            }
            peaks[np].t = t;
            peaks[np].f = best;
            peaks[np].mag = m;
            np++;
        }
    }

    /* Bands are disjoint and ascending, so peaks within a frame are already in f order. */
    out->peaks = peaks;
    out->num_peaks = np;
    return 0;
}

void fp_analysis_free(fp_analysis *a)
{
    free(a->spec);
    free(a->peaks);
    memset(a, 0, sizeof *a);
}

int fp_pairs_build(const fp_config *c, const fp_peak *peaks, size_t n, fp_pair **out, size_t *num)
{
    size_t cap = n * (size_t)c->fan_out;
    fp_pair *pairs = malloc((cap ? cap : 1) * sizeof *pairs);
    if (!pairs)
        return -1;

    size_t np = 0;
    for (size_t i = 0; i < n; i++) {
        int taken = 0;
        for (size_t j = i + 1; j < n && taken < c->fan_out; j++) {
            const int dt = peaks[j].t - peaks[i].t;
            if (dt < c->min_dt)
                continue;
            if (dt > c->max_dt)
                break;
            int df = peaks[j].f - peaks[i].f;
            if (df < 0)
                df = -df;
            if (df > c->max_df)
                continue;
            pairs[np].hash = fp_hash_pack(peaks[i].f, peaks[j].f, dt);
            pairs[np].t_anchor = peaks[i].t;
            pairs[np].a_idx = (int32_t)i;
            pairs[np].b_idx = (int32_t)j;
            np++;
            taken++;
        }
    }
    *out = pairs;
    *num = np;
    return 0;
}

void fp_pairs_free(fp_pair *p)
{
    free(p);
}

uint32_t fp_hash_pack(int f1, int f2, int dt)
{
    return ((uint32_t)f1 << 15) | ((uint32_t)f2 << 6) | (uint32_t)dt;
}

void fp_hash_unpack(uint32_t h, int *f1, int *f2, int *dt)
{
    *f1 = (int)((h >> 15) & 0x1FF);
    *f2 = (int)((h >> 6) & 0x1FF);
    *dt = (int)(h & 0x3F);
}

uint64_t fp_entry_pack(uint32_t hash, uint32_t track, uint32_t time)
{
    return ((uint64_t)(hash & 0xFFFFFF) << 40) | ((uint64_t)(track & 0x1FFFFF) << 19) | (uint64_t)(time & 0x7FFFF);
}

void fp_entry_unpack(uint64_t e, uint32_t *hash, uint32_t *track, uint32_t *time)
{
    *hash = (uint32_t)(e >> 40);
    *track = (uint32_t)((e >> 19) & 0x1FFFFF);
    *time = (uint32_t)(e & 0x7FFFF);
}
