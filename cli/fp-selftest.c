#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fft.h"
#include "fingerprint.h"
#include "rng.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

static void test_fft_impulse(void)
{
    printf("FFT of an impulse is flat\n");
    const int n = 1024;
    fp_fft_plan *p = fp_fft_create(n);
    float *re = calloc((size_t)n, sizeof *re);
    float *im = calloc((size_t)n, sizeof *im);
    re[0] = 1.0f;
    fp_fft_forward(p, re, im);
    float max_dev = 0.0f;
    for (int k = 0; k < n; k++) {
        float d = fabsf(hypotf(re[k], im[k]) - 1.0f);
        if (d > max_dev)
            max_dev = d;
    }
    printf("  max | |X[k]| - 1 | over %d bins = %g\n", n, max_dev);
    check(max_dev < 1e-6f, "impulse spectrum flat to 1e-6");

    memset(re, 0, (size_t)n * sizeof *re);
    memset(im, 0, (size_t)n * sizeof *im);
    for (int i = 0; i < n; i++)
        re[i] = cosf((float)(2.0 * M_PI * 100 * i / n));
    fp_fft_forward(p, re, im);
    check(fabsf(hypotf(re[100], im[100]) - n / 2.0f) < 1e-2f, "cosine at bin 100 has magnitude N/2");
    free(re);
    free(im);
    fp_fft_free(p);
}

static void test_sine_peak(void)
{
    printf("1 kHz sine lands at bin 128 +/- 1 in every frame\n");
    fp_config cfg = fp_config_default();
    fp_fingerprinter *fpr = fp_fingerprinter_create(&cfg);
    const size_t n = (size_t)cfg.sample_rate * 5;
    float *x = malloc(n * sizeof *x);
    for (size_t i = 0; i < n; i++)
        x[i] = 0.8f * sinf((float)(2.0 * M_PI * 1000.0 * (double)i / cfg.sample_rate));

    fp_analysis a;
    fp_analyze(fpr, x, n, 1, &a);
    printf("  frames=%d bins=%d peaks=%zu (%.2f per frame)\n", a.frames, a.bins, a.num_peaks,
           (double)a.num_peaks / a.frames);

    int frames_ok = 0;
    int off_target = 0;
    int min_bin = 9999;
    int max_bin = -1;
    size_t ip = 0;
    for (int t = 0; t < a.frames; t++) {
        int found = 0;
        for (; ip < a.num_peaks && a.peaks[ip].t == t; ip++) {
            int f = a.peaks[ip].f;
            if (abs(f - 128) <= 1) {
                found = 1;
                if (f < min_bin)
                    min_bin = f;
                if (f > max_bin)
                    max_bin = f;
            } else {
                off_target++;
            }
        }
        frames_ok += found;
    }
    printf("  frames with a peak in [127,129]: %d / %d, observed bins %d..%d\n", frames_ok, a.frames, min_bin,
           max_bin);
    printf("  other-band peaks (sidelobe leakage above min_magnitude): %d\n", off_target);
    check(frames_ok == a.frames, "every frame has a peak at bin 128 +/- 1");

    int col = 0;
    float best = 0.0f;
    for (int k = 0; k < a.bins; k++) {
        float v = a.spec[(size_t)10 * a.bins + k];
        if (v > best) {
            best = v;
            col = k;
        }
    }
    check(col == 128, "stored log1p spectrogram column also peaks at bin 128");

    fp_analysis_free(&a);
    free(x);
    fp_fingerprinter_free(fpr);
}

static void test_hash_roundtrip(void)
{
    printf("hash and entry pack/unpack round-trip\n");
    int ok = 1;
    const int fs[] = {1, 2, 127, 128, 255, 256, 510, 511};
    const int dts[] = {0, 1, 31, 62, 63};
    for (size_t i = 0; i < sizeof fs / sizeof *fs; i++)
        for (size_t j = 0; j < sizeof fs / sizeof *fs; j++)
            for (size_t k = 0; k < sizeof dts / sizeof *dts; k++) {
                uint32_t h = fp_hash_pack(fs[i], fs[j], dts[k]);
                int a, b, d;
                fp_hash_unpack(h, &a, &b, &d);
                if (h >> 24 || a != fs[i] || b != fs[j] || d != dts[k])
                    ok = 0;
            }
    check(ok, "hash f1<<15|f2<<6|dt is 24-bit and round-trips");

    ok = 1;
    const uint32_t hashes[] = {0, 1, 0xABCDEF, 0xFFFFFF};
    const uint32_t tracks[] = {0, 1, 12345, 0x1FFFFF};
    const uint32_t times[] = {0, 1, 99999, 0x7FFFF};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++) {
                uint64_t e = fp_entry_pack(hashes[i], tracks[j], times[k]);
                uint32_t h, tr, ti;
                fp_entry_unpack(e, &h, &tr, &ti);
                if (h != hashes[i] || tr != tracks[j] || ti != times[k])
                    ok = 0;
            }
    check(ok, "entry hash:24|track:21|time:19 round-trips at bounds");

    uint64_t lo = fp_entry_pack(5, 0x1FFFFF, 0x7FFFF);
    uint64_t hi = fp_entry_pack(6, 0, 0);
    check(lo < hi, "entries sort by hash first");
}

static void test_pairs(void)
{
    printf("peak ordering and pair constraints on random tone bursts\n");
    fp_config cfg = fp_config_default();
    fp_fingerprinter *fpr = fp_fingerprinter_create(&cfg);
    fp_rng rng;
    fp_rng_seed(&rng, 42);
    const size_t n = (size_t)cfg.sample_rate * 10;
    float *x = malloc(n * sizeof *x);
    memset(x, 0, n * sizeof *x);
    for (size_t seg = 0; seg < n; seg += 1500) {
        for (int v = 0; v < 3; v++) {
            double hz = 150.0 + 3500.0 * fp_rng_f32(&rng);
            for (size_t i = seg; i < seg + 1500 && i < n; i++)
                x[i] += 0.25f * sinf((float)(2.0 * M_PI * hz * (double)i / cfg.sample_rate));
        }
    }
    for (size_t i = 0; i < n; i++)
        x[i] += 0.02f * (fp_rng_f32(&rng) - 0.5f);

    fp_analysis a;
    fp_analyze(fpr, x, n, 0, &a);
    int sorted = 1;
    int range = 1;
    for (size_t i = 0; i < a.num_peaks; i++) {
        if (i && (a.peaks[i].t < a.peaks[i - 1].t || (a.peaks[i].t == a.peaks[i - 1].t && a.peaks[i].f <= a.peaks[i - 1].f)))
            sorted = 0;
        if (a.peaks[i].f < 1 || a.peaks[i].f > FP_MAX_F)
            range = 0;
    }
    check(sorted, "peaks strictly sorted by (t, f)");
    check(range, "peak bins within 1..511");

    fp_pair *pairs;
    size_t np;
    fp_pairs_build(&cfg, a.peaks, a.num_peaks, &pairs, &np);
    printf("  peaks=%zu (%.1f/s) pairs=%zu\n", a.num_peaks, a.num_peaks / 10.0, np);
    int ok = np > 0;
    for (size_t i = 0; i < np; i++) {
        const fp_peak *pa = &a.peaks[pairs[i].a_idx];
        const fp_peak *pb = &a.peaks[pairs[i].b_idx];
        int dt = pb->t - pa->t;
        int df = abs(pb->f - pa->f);
        int h1, h2, hd;
        fp_hash_unpack(pairs[i].hash, &h1, &h2, &hd);
        if (dt < cfg.min_dt || dt > cfg.max_dt || df > cfg.max_df || h1 != pa->f || h2 != pb->f || hd != dt ||
            pairs[i].t_anchor != pa->t)
            ok = 0;
    }
    check(ok, "every pair satisfies min_dt/max_dt/max_df and its hash decodes to its peaks");
    fp_pairs_free(pairs);
    fp_analysis_free(&a);
    free(x);
    fp_fingerprinter_free(fpr);
}

int main(void)
{
    printf("== step 1.1: FFT and fingerprint ==\n");
    test_fft_impulse();
    test_sine_peak();
    test_hash_roundtrip();
    test_pairs();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
