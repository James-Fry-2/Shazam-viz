#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channel.h"
#include "fft.h"
#include "fingerprint.h"
#include "index.h"
#include "match.h"
#include "manifest.h"
#include "rng.h"
#include "wav.h"

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
        x[i] = (float)(0.8 * sin(2.0 * M_PI * 1000.0 * (double)i / cfg.sample_rate));

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
    printf("  other-band peaks (sidelobe leakage above min_magnitude_db): %d\n", off_target);
    check(frames_ok == a.frames, "every frame has a peak at bin 128 +/- 1");
    check(off_target == 0, "no peaks outside bin 128 +/- 1");

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

/* Phase is computed in double: float phase error at large arguments alone puts broadband noise near -72 dB. */
static void test_offbin_leakage(void)
{
    printf("off-bin 1003.7 Hz sine: Hann sidelobes stay below min_magnitude_db\n");
    fp_config cfg = fp_config_default();
    fp_fingerprinter *fpr = fp_fingerprinter_create(&cfg);
    const size_t n = (size_t)cfg.sample_rate * 5;
    float *x = malloc(n * sizeof *x);
    for (size_t i = 0; i < n; i++)
        x[i] = (float)(0.8 * sin(2.0 * M_PI * 1003.7 * (double)i / cfg.sample_rate));
    fp_analysis a;
    fp_analyze(fpr, x, n, 0, &a);
    int off_target = 0;
    for (size_t k = 0; k < a.num_peaks; k++)
        off_target += abs(a.peaks[k].f - 128) > 1;
    printf("  min_magnitude_db %.0f: %d off-target peaks in %d frames\n", cfg.min_magnitude_db, off_target, a.frames);
    check(off_target == 0, "no sidelobe peaks in other bands");
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

#define SWEEP_SECONDS 15
#define NUM_SWEEP_TRACKS 3

/* Three voices whose frequency moves linearly towards a new random target every ~0.3 s. */
static float *make_sweep_track(uint64_t seed, const fp_config *cfg, size_t *len)
{
    const size_t n = (size_t)cfg->sample_rate * SWEEP_SECONDS;
    const size_t seg = (size_t)(0.3 * cfg->sample_rate);
    float *x = calloc(n, sizeof *x);
    fp_rng rng;
    fp_rng_seed(&rng, seed);
    for (int v = 0; v < 3; v++) {
        double f = 200.0 + 3400.0 * fp_rng_f32(&rng);
        double phase = 0.0;
        for (size_t s = 0; s < n; s += seg) {
            double target = 200.0 + 3400.0 * fp_rng_f32(&rng);
            for (size_t i = s; i < s + seg && i < n; i++) {
                double fi = f + (target - f) * (double)(i - s) / (double)seg;
                phase += 2.0 * M_PI * fi / cfg->sample_rate;
                x[i] += 0.3f * sinf((float)fmod(phase, 2.0 * M_PI));
            }
            f = target;
        }
    }
    *len = n;
    return x;
}

typedef struct {
    fp_pair *pairs;
    size_t np;
    int frames;
} fingerprinted;

static fingerprinted fingerprint(fp_fingerprinter *fpr, const fp_config *cfg, const float *x, size_t n)
{
    fingerprinted r;
    fp_analysis a;
    fp_analyze(fpr, x, n, 0, &a);
    fp_pairs_build(cfg, a.peaks, a.num_peaks, &r.pairs, &r.np);
    r.frames = a.frames;
    fp_analysis_free(&a);
    return r;
}

static int votes_equal(const fp_votes *a, const fp_votes *b)
{
    return a->n == b->n && a->entries_scanned == b->entries_scanned && a->hashes_used == b->hashes_used &&
           memcmp(a->v, b->v, a->n * sizeof *a->v) == 0;
}

static void print_result(const char *name, const fp_match_result *r)
{
    printf("  %-8s matched=%d track=%u offset=%d score=%u runner_up=%u hashes_used=%u scanned=%llu\n", name,
           r->matched, r->track, r->offset, r->score, r->runner_up, r->hashes_used,
           (unsigned long long)r->entries_scanned);
    printf("           top = [t%u o%d s%u] [t%u o%d s%u] [t%u o%d s%u]\n", r->top[0].track, r->top[0].offset,
           r->top[0].score, r->top[1].track, r->top[1].offset, r->top[1].score, r->top[2].track,
           r->top[2].offset, r->top[2].score);
}

static void test_index_match(void)
{
    printf("index 3 sine-sweep tracks, query a non-aligned 5 s excerpt\n");
    fp_config cfg = fp_config_default();
    fp_fingerprinter *fpr = fp_fingerprinter_create(&cfg);
    fp_index *idx[2] = {fp_index_create(FP_IMPL_NAIVE2003, &cfg), fp_index_create(FP_IMPL_MODERN, &cfg)};
    float *tracks[NUM_SWEEP_TRACKS + 1];
    size_t len = 0;

    for (int t = 0; t < NUM_SWEEP_TRACKS + 1; t++) {
        tracks[t] = make_sweep_track(1000 + (uint64_t)t, &cfg, &len);
        if (t == NUM_SWEEP_TRACKS)
            break;
        fingerprinted f = fingerprint(fpr, &cfg, tracks[t], len);
        printf("  track %d: frames=%d pairs=%zu\n", t, f.frames, f.np);
        for (int k = 0; k < 2; k++)
            fp_index_add(idx[k], (uint32_t)t, (uint32_t)f.frames, f.pairs, f.np);
        fp_pairs_free(f.pairs);
    }
    for (int k = 0; k < 2; k++)
        fp_index_finalize(idx[k]);
    printf("  entries: naive=%zu modern=%zu; bytes: naive=%zu modern=%zu\n", fp_index_num_entries(idx[0]),
           fp_index_num_entries(idx[1]), fp_index_bytes(idx[0]), fp_index_bytes(idx[1]));

    const int true_track = 1;
    const size_t start = (size_t)(3.3 * cfg.sample_rate) + 123;
    const size_t clip_len = (size_t)5 * cfg.sample_rate;
    fingerprinted q = fingerprint(fpr, &cfg, tracks[true_track] + start, clip_len);
    const double true_offset = (double)start / cfg.hop_size;
    printf("  query: track %d, start sample %zu, true offset %.2f frames, %zu query pairs\n", true_track, start,
           true_offset, q.np);

    fp_votes votes[2];
    fp_match_result res[2];
    for (int k = 0; k < 2; k++) {
        fp_index_query(idx[k], q.pairs, q.np, &votes[k]);
        fp_match_decide(&cfg, &votes[k], &res[k]);
    }
    print_result("naive", &res[0]);
    print_result("modern", &res[1]);

    for (int k = 0; k < 2; k++) {
        check(res[k].matched && (int)res[k].track == true_track, k ? "modern picks the right track" : "naive picks the right track");
        check(fabs((double)res[k].offset - true_offset) <= 1.5, k ? "modern offset within 1.5 frames of truth" : "naive offset within 1.5 frames of truth");
    }
    check(votes_equal(&votes[0], &votes[1]), "raw vote arrays identical (n, counts, scanned, hashes_used)");
    check(fp_match_equal(&res[0], &res[1]), "match results identical field by field");

    fingerprinted other = fingerprint(fpr, &cfg, tracks[NUM_SWEEP_TRACKS] + start, clip_len);
    fp_votes ov[2];
    fp_match_result orr[2];
    for (int k = 0; k < 2; k++) {
        fp_index_query(idx[k], other.pairs, other.np, &ov[k]);
        fp_match_decide(&cfg, &ov[k], &orr[k]);
    }
    printf("  holdout query (track not in index): naive score=%u matched=%d, modern score=%u matched=%d\n",
           orr[0].score, orr[0].matched, orr[1].score, orr[1].matched);
    check(!orr[0].matched && !orr[1].matched, "unindexed track reports no match");
    check(fp_match_equal(&orr[0], &orr[1]), "holdout results identical across backends");

    const char *path = "build/selftest_index.bin";
    check(fp_index_save(idx[0], path) == 0, "save index.bin from naive backend");
    for (int k = 0; k < 2; k++) {
        fp_index *loaded = NULL;
        int rc = fp_index_load(&loaded, k ? FP_IMPL_MODERN : FP_IMPL_NAIVE2003, &cfg, path);
        fp_votes lv = {0};
        fp_match_result lr = {0};
        if (rc == 0) {
            fp_index_query(loaded, q.pairs, q.np, &lv);
            fp_match_decide(&cfg, &lv, &lr);
        }
        check(rc == 0 && fp_match_equal(&lr, &res[0]), k ? "load into modern reproduces result" : "load into naive reproduces result");
        fp_votes_free(&lv);
        fp_index_free(loaded);
    }
    fp_config other_cfg = cfg;
    other_cfg.threshold = 3.0f;
    fp_index *refused = NULL;
    check(fp_index_load(&refused, FP_IMPL_MODERN, &other_cfg, path) == -2 && !refused, "load refuses a config mismatch");
    other_cfg = cfg;
    other_cfg.min_score = 99;
    check(fp_index_load(&refused, FP_IMPL_MODERN, &other_cfg, path) == 0, "scoring-only config change is accepted");
    fp_index_free(refused);
    other_cfg = cfg;
    other_cfg.score_window = 3 - cfg.score_window;
    check(fp_index_load(&refused, FP_IMPL_MODERN, &other_cfg, path) == 0, "score_window change is accepted without a re-index");
    fp_index_free(refused);

    for (int k = 0; k < 2; k++) {
        fp_votes_free(&votes[k]);
        fp_votes_free(&ov[k]);
        fp_index_free(idx[k]);
    }
    fp_pairs_free(q.pairs);
    fp_pairs_free(other.pairs);
    for (int t = 0; t < NUM_SWEEP_TRACKS + 1; t++)
        free(tracks[t]);
    fp_fingerprinter_free(fpr);
}

static fp_pair mkpair(uint32_t hash, int t)
{
    fp_pair p = {hash, t, 0, 0};
    return p;
}

static void test_edge_cases(void)
{
    printf("negative offsets, bucket collisions and ties on hand-built pairs\n");
    fp_config cfg = fp_config_default();
    cfg.offset_bin = 2;
    cfg.min_score = 1;
    fp_index *idx[2] = {fp_index_create(FP_IMPL_NAIVE2003, &cfg), fp_index_create(FP_IMPL_MODERN, &cfg)};

    /* H and H + 2^20 share a naive bucket but are different hashes. */
    const uint32_t h = 0x012345;
    const uint32_t collide = h + (1u << 20);
    const fp_pair t7[] = {mkpair(h, 5), mkpair(h, 9)};
    const fp_pair t3[] = {mkpair(collide, 5), mkpair(h, 5)};
    for (int k = 0; k < 2; k++) {
        fp_index_add(idx[k], 7, 100, t7, 2);
        fp_index_add(idx[k], 3, 100, t3, 2);
        fp_index_finalize(idx[k]);
    }

    /* Query at t=8: offsets 5-8=-3 -> bin -2; 9-8=1 -> bin 0. Floor, not truncation. */
    const fp_pair q[] = {mkpair(h, 8)};
    fp_votes v[2];
    fp_match_result r[2];
    for (int k = 0; k < 2; k++) {
        fp_index_query(idx[k], q, 1, &v[k]);
        fp_match_decide(&cfg, &v[k], &r[k]);
    }
    printf("  votes (track,bin,count):");
    for (size_t i = 0; i < v[0].n; i++)
        printf(" (%u,%d,%u)", v[0].v[i].track, v[0].v[i].bin, v[0].v[i].count);
    printf("  scanned=%llu\n", (unsigned long long)v[0].entries_scanned);
    check(v[0].n == 3 && v[0].v[0].track == 3 && v[0].v[0].bin == -2 && v[0].v[1].track == 7 &&
              v[0].v[1].bin == -2 && v[0].v[2].track == 7 && v[0].v[2].bin == 0,
          "floor division places offset -3 in bin -2 and keeps tracks separate");
    check(v[0].entries_scanned == 3, "colliding bucket entry with a different hash is not counted");
    check(votes_equal(&v[0], &v[1]), "backends agree on these votes");
    check(r[0].track == 3 && r[0].offset == -4 && r[0].score == 1, "tie on score resolves to the lowest track id");
    check(r[0].top[1].track == 7 && r[0].runner_up == 1 && !r[0].matched,
          "runner-up counted, and score 1 vs runner-up 1 fails the ratio test");
    check(fp_match_equal(&r[0], &r[1]), "match results identical");

    fp_votes empty;
    fp_match_result er;
    fp_index_query(idx[1], NULL, 0, &empty);
    fp_match_decide(&cfg, &empty, &er);
    check(!er.matched && er.track == FP_NO_TRACK && er.top[0].track == FP_NO_TRACK, "empty query gives no match");

    fp_votes_free(&empty);
    for (int k = 0; k < 2; k++) {
        fp_votes_free(&v[k]);
        fp_index_free(idx[k]);
    }
}

static fp_match_result decide_votes(int window, int offset_bin, const fp_vote *v, size_t n)
{
    fp_config cfg = fp_config_default();
    cfg.score_window = window;
    cfg.offset_bin = offset_bin;
    cfg.min_score = 1;
    fp_votes votes = {(fp_vote *)v, n, 0, 0};
    fp_match_result r;
    fp_match_decide(&cfg, &votes, &r);
    return r;
}

static void test_score_window(void)
{
    printf("two-bin offset scoring on hand-built votes\n");
    fp_match_result r;

    const fp_vote split[] = {{0, 10, 3}, {0, 11, 3}, {0, 40, 5}};
    r = decide_votes(1, 1, split, 3);
    check(r.score == 5 && r.offset == 40, "window 1: a single 5-vote bin beats 3 + 3 split across adjacent bins");
    r = decide_votes(2, 1, split, 3);
    check(r.score == 6 && r.offset == 10, "window 2: 3 + 3 in adjacent bins beats 5 in one bin, equal halves report the lower bin");

    const fp_vote larger[] = {{0, 7, 2}, {0, 8, 4}};
    r = decide_votes(2, 1, larger, 2);
    check(r.score == 6 && r.offset == 8, "reported bin is the one with more votes");
    r = decide_votes(2, 3, larger, 2);
    check(r.offset == 24, "reported offset scales with offset_bin");

    const fp_vote apart[] = {{0, 10, 3}, {0, 12, 3}};
    r = decide_votes(2, 1, apart, 2);
    check(r.score == 3 && r.offset == 10, "bins two apart do not sum, and the lower wins the tie");
    const fp_vote gap[] = {{0, 4, 2}, {0, 6, 2}, {0, 9, 2}};
    r = decide_votes(2, 1, gap, 3);
    check(r.score == 2, "neighbours in the array but not in bin value do not sum");

    const fp_vote neg[] = {{0, -3, 1}, {0, -1, 3}, {0, 0, 2}};
    r = decide_votes(2, 1, neg, 3);
    check(r.score == 5 && r.offset == -1, "adjacent bins straddling zero sum and report the larger bin (-1)");
    const fp_vote neg2[] = {{0, -3, 4}, {0, -2, 4}};
    r = decide_votes(2, 1, neg2, 2);
    check(r.score == 8 && r.offset == -3, "negative bins -3 and -2 sum, tie reports -3");

    const fp_vote run[] = {{0, 0, 3}, {0, 1, 3}, {0, 2, 3}};
    r = decide_votes(2, 1, run, 3);
    check(r.score == 6 && r.offset == 0, "equal windows in one track keep the lowest offset");

    const fp_vote two_tracks[] = {{2, 5, 3}, {2, 6, 3}, {5, 1, 6}, {9, 0, 6}};
    r = decide_votes(2, 1, two_tracks, 4);
    check(r.track == 2 && r.top[1].track == 5 && r.top[2].track == 9 && r.top[0].score == 6 &&
              r.top[1].score == 6 && r.top[2].score == 6,
          "equal windowed scores rank by track id and fill top[3]");

    const fp_vote ru[] = {{1, 0, 4}, {1, 1, 3}, {2, 8, 6}};
    r = decide_votes(1, 1, ru, 3);
    check(r.track == 2 && r.score == 6 && r.runner_up == 4, "window 1: runner-up is the best single bin of another track");
    r = decide_votes(2, 1, ru, 3);
    check(r.track == 1 && r.score == 7 && r.runner_up == 6 && r.top[1].track == 2,
          "window 2: runner-up and top[3] use windowed scores");

    const fp_vote same[] = {{3, 5, 4}, {3, 6, 1}, {3, 20, 5}};
    r = decide_votes(2, 1, same, 3);
    check(r.score == 5 && r.offset == 5 && r.runner_up == 0, "a track's own other windows are never the runner-up (5 at bin 5 ties 5 at bin 20, lower wins)");

    fp_match_result a = decide_votes(1, 1, split, 3), b = decide_votes(1, 1, split, 3);
    check(fp_match_equal(&a, &b), "window 1 is deterministic");
}

static double tone_gain_db(double hz, float amp, int sat)
{
    const int sr = 8000;
    const size_t n = sr * 2;
    float *x = malloc(n * sizeof *x);
    for (size_t i = 0; i < n; i++)
        x[i] = amp * sinf((float)(2.0 * M_PI * hz * (double)i / sr));
    if (sat)
        fp_channel_speaker(x, n, sr);
    else
        fp_channel_speaker_filter(x, n, sr);
    /* second half only, so the filter transient has died away */
    const double g = fp_rms(x + n / 2, n / 2) / ((double)amp / sqrt(2.0));
    free(x);
    return 20.0 * log10(g);
}

static void test_speaker(void)
{
    printf("speaker filter (380 Hz high-pass, 3300 Hz low-pass, Q 0.707)\n");
    const double g100 = tone_gain_db(100.0, 0.1f, 0);
    const double g1k = tone_gain_db(1000.0, 0.1f, 0);
    printf("  filter stage gain at 100 Hz = %+.2f dB, at 1 kHz = %+.2f dB\n", g100, g1k);
    check(g100 < -12.0, "100 Hz attenuated by more than 12 dB");
    check(fabs(g1k) < 1.0, "1 kHz within 1 dB of unity");
    printf("  (with tanh stage at amplitude 0.1: 100 Hz %+.2f dB, 1 kHz %+.2f dB; small signals gain\n"
           "   drive/tanh(drive) = %.2f dB, so the unity-gain check is on the linear stage)\n",
           tone_gain_db(100.0, 0.1f, 1), tone_gain_db(1000.0, 0.1f, 1), 20.0 * log10(2.0 / tanh(2.0)));
}

static void test_noise_snr(void)
{
    printf("pink noise SNR\n");
    fp_config cfg = fp_config_default();
    size_t n;
    float *clean = make_sweep_track(7, &cfg, &n);
    const double targets[] = {30.0, 20.0, 10.0, 5.0, 0.0};
    int ok = 1;
    for (size_t k = 0; k < sizeof targets / sizeof *targets; k++) {
        float *y = malloc(n * sizeof *y);
        memcpy(y, clean, n * sizeof *y);
        fp_rng rng;
        fp_rng_seed(&rng, 100 + k);
        fp_channel_noise(y, n, targets[k], &rng);
        for (size_t i = 0; i < n; i++)
            y[i] -= clean[i];
        const double measured = 20.0 * log10(fp_rms(clean, n) / fp_rms(y, n));
        printf("  target %5.1f dB  measured %7.3f dB  error %+.4f dB\n", targets[k], measured, measured - targets[k]);
        if (fabs(measured - targets[k]) > 0.2)
            ok = 0;
        free(y);
    }
    check(ok, "measured SNR within +/-0.2 dB of target");
    free(clean);
}

static void test_echo(void)
{
    printf("echo\n");
    const int sr = 8000;
    const size_t n = sr;
    float *x = calloc(n, sizeof *x);
    x[0] = 1.0f;
    fp_rng rng;
    fp_rng_seed(&rng, 5);
    fp_channel_echo(x, n, sr, &rng);
    int taps = 0;
    size_t last = 0;
    for (size_t i = 1; i < n; i++)
        if (x[i] != 0.0f) {
            taps++;
            last = i;
        }
    printf("  direct = %g, %d taps, last at %.3f s\n", x[0], taps, (double)last / sr);
    check(x[0] == 1.0f, "direct path has delay 0 and gain 1");
    check(taps >= 10 && taps <= 12 && last <= sr / 2, "about 12 taps within 0.5 s");
    free(x);
}

static void test_resample(void)
{
    printf("resampler\n");
    const int sr_in = 44100;
    const size_t n = sr_in * 2;
    float *x = malloc(n * sizeof *x);
    for (size_t i = 0; i < n; i++)
        x[i] = 0.5f * sinf((float)(2.0 * M_PI * 1000.0 * (double)i / sr_in));
    float *y;
    size_t m;
    fp_resample(x, n, sr_in, 8000, &y, &m);
    double max_err = 0.0;
    for (size_t i = 100; i + 100 < m; i++) {
        double e = fabs(y[i] - 0.5 * sin(2.0 * M_PI * 1000.0 * (double)i / 8000));
        if (e > max_err)
            max_err = e;
    }
    printf("  44100 -> 8000 Hz, %zu -> %zu samples, max error vs ideal 1 kHz tone = %g\n", n, m, max_err);
    check(m == 16000 && max_err < 2e-3, "1 kHz tone survives 44.1k -> 8k");
    free(y);
    fp_resample(x, n, sr_in, sr_in, &y, &m);
    check(m == n && memcmp(x, y, n * sizeof *x) == 0, "same-rate resample is the identity");
    free(y);
    free(x);
}

static void test_io(void)
{
    printf("WAV and manifest I/O\n");
    const size_t n = 4000;
    float *x = malloc(n * sizeof *x);
    for (size_t i = 0; i < n; i++)
        x[i] = 0.9f * sinf((float)i * 0.05f);
    const char *wav = "build/selftest_io.wav";
    float *y;
    size_t m;
    int sr;
    check(wav_write(wav, x, n, 8000) == 0 && wav_read(wav, &y, &m, &sr) == 0, "16-bit mono write then read");
    double max_err = 0.0;
    for (size_t i = 0; i < n && i < m; i++)
        max_err = fmax(max_err, fabs(x[i] - y[i]));
    check(m == n && sr == 8000 && max_err <= 1.0 / 32768.0, "round trip within one 16-bit step");
    free(y);
    free(x);

    /* two frames of stereo float: (0.5, -0.1) and (1.0, 0.0) */
    const uint8_t hdr[] = {'R', 'I', 'F', 'F', 60, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 3, 0, 2, 0,
                           0x40, 0x1F, 0, 0, 0, 0x7D, 0, 0, 8, 0, 32, 0, 'd', 'a', 't', 'a', 16, 0, 0, 0};
    const float pcm[] = {0.5f, -0.1f, 1.0f, 0.0f};
    FILE *f = fopen("build/selftest_f32.wav", "wb");
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(pcm, sizeof pcm[0], 4, f);
    fclose(f);
    check(wav_read("build/selftest_f32.wav", &y, &m, &sr) == 0 && m == 2 && fabsf(y[0] - 0.2f) < 1e-6f &&
              fabsf(y[1] - 0.5f) < 1e-6f,
          "32-bit float stereo downmixed to mono");
    free(y);
    float *z;
    check(wav_read_at_rate("build/selftest_f32.wav", 4000, &z, &m) == 0 && m == 1, "wav_read_at_rate resamples 8 kHz to 4 kHz");
    free(z);

    f = fopen("build/selftest_manifest.tsv", "w");
    fputs("id\tpath\tsplit\n3\taudio/a.wav\tindexed\n9\t/abs/b.wav\tholdout\n", f);
    fclose(f);
    manifest man;
    check(manifest_read("build/selftest_manifest.tsv", &man) == 0 && man.n == 2, "manifest parses two rows");
    check(man.n == 2 && man.rows[0].id == 3 && !man.rows[0].holdout && man.rows[1].holdout &&
              !strcmp(man.rows[0].path, "build/audio/a.wav") && !strcmp(man.rows[1].path, "/abs/b.wav"),
          "ids, splits and path resolution");
    manifest_free(&man);
}

int main(void)
{
    printf("== step 1.1: FFT and fingerprint ==\n");
    test_fft_impulse();
    test_sine_peak();
    test_offbin_leakage();
    test_hash_roundtrip();
    test_pairs();

    printf("\n== step 1.2: index backends, file format, matching ==\n");
    test_index_match();
    test_edge_cases();
    test_score_window();

    printf("\n== step 1.3: channel simulation, resampler, I/O ==\n");
    test_speaker();
    test_noise_snr();
    test_echo();
    test_resample();
    test_io();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
