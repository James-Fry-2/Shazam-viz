#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "channel.h"
#include "config_args.h"
#include "fingerprint.h"
#include "hash_stats.h"
#include "index.h"
#include "manifest.h"
#include "match.h"
#include "rng.h"
#include "wav.h"

#define MAX_LIST 16
#define PRE_ROLL_S 0.5

enum { CH_CLEAN, CH_SPEAKER, CH_ECHO, CH_SPEAKER_ECHO, CH_COUNT };
static const char *const CHANNEL_NAMES[CH_COUNT] = {"clean", "speaker", "echo", "speaker+echo"};

typedef struct {
    float *x;
    size_t n;
    uint32_t id;
} track;

typedef struct {
    uint8_t holdout, matched, hit, track_hit;
    uint32_t clip_track, true_track, track;
    int32_t offset;
    double r;
    uint32_t score, runner_up, true_score, true_window_score, wrong_score;
    uint64_t entries;
    uint32_t pairs, query_peaks;
    uint32_t clean_peaks, surv_peaks, surv_peaks_exact, clean_hashes, surv_hashes, both_exact;
} record;

typedef struct {
    fp_config cfg;
    fp_index *naive, *modern;
    int use_naive;
    uint64_t seed;

    double lengths[MAX_LIST];
    int num_lengths;
    int channels[MAX_LIST];
    int num_channels;
    double snrs[MAX_LIST];
    int num_snrs;

    const track **clips;
    int num_clips;
    int num_indexed_clips;
    size_t *starts;

    record *rec;

    pthread_mutex_t lock;
    int next;
    int total;
} ctx;

static uint64_t mix(uint64_t a, uint64_t b)
{
    fp_rng r;
    fp_rng_seed(&r, a * 0x9E3779B97F4A7C15ULL ^ (b + 0x632BE59BD9B4E019ULL));
    return fp_rng_u64(&r);
}

static uint64_t seed_of(uint64_t seed, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    return mix(mix(mix(mix(seed, a), b), c), d);
}

static size_t rec_index(const ctx *c, int li, int clip, int ch, int si)
{
    return (((size_t)li * c->num_clips + clip) * c->num_channels + ch) * c->num_snrs + si;
}

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* Counts clean peaks with a degraded peak at the same t and |df| <= 1; exact[i] marks an identical (t, f), counted in *n_exact. */
static uint32_t peak_survivors(const fp_analysis *clean, const fp_analysis *deg, uint8_t *exact, uint32_t *n_exact)
{
    uint32_t s = 0;
    *n_exact = 0;
    size_t j = 0;
    for (size_t i = 0; i < clean->num_peaks; i++) {
        const fp_peak *p = &clean->peaks[i];
        int near = 0;
        exact[i] = 0;
        while (j < deg->num_peaks && deg->peaks[j].t < p->t)
            j++;
        for (size_t k = j; k < deg->num_peaks && deg->peaks[k].t == p->t; k++) {
            near |= abs(deg->peaks[k].f - p->f) <= 1;
            exact[i] |= deg->peaks[k].f == p->f;
        }
        s += (uint32_t)near;
        *n_exact += exact[i];
    }
    return s;
}

static uint64_t *pair_keys(const fp_pair *p, size_t n)
{
    uint64_t *k = malloc((n ? n : 1) * sizeof *k);
    if (!k)
        die("out of memory");
    for (size_t i = 0; i < n; i++)
        k[i] = (uint64_t)p[i].hash << 32 | (uint32_t)p[i].t_anchor;
    qsort(k, n, sizeof *k, cmp_u64);
    return k;
}

/* Multiset intersection of exact (hash, t_anchor) keys. */
static uint32_t hash_survivors(const uint64_t *a, size_t na, const uint64_t *b, size_t nb)
{
    uint32_t s = 0;
    size_t i = 0, j = 0;
    while (i < na && j < nb) {
        if (a[i] < b[j])
            i++;
        else if (a[i] > b[j])
            j++;
        else {
            s++;
            i++;
            j++;
        }
    }
    return s;
}

static int votes_equal(const fp_votes *a, const fp_votes *b)
{
    if (a->n != b->n || a->entries_scanned != b->entries_scanned || a->hashes_used != b->hashes_used)
        return 0;
    for (size_t i = 0; i < a->n; i++)
        if (a->v[i].track != b->v[i].track || a->v[i].bin != b->v[i].bin || a->v[i].count != b->v[i].count)
            return 0;
    return 1;
}

static void apply_channel(int ch, float *x, size_t n, int sr, uint64_t echo_seed)
{
    if (ch == CH_SPEAKER || ch == CH_SPEAKER_ECHO)
        fp_channel_speaker(x, n, sr);
    if (ch == CH_ECHO || ch == CH_SPEAKER_ECHO) {
        fp_rng r;
        fp_rng_seed(&r, echo_seed);
        if (fp_channel_echo(x, n, sr, &r))
            die("out of memory");
    }
}

static void run_unit(ctx *c, fp_fingerprinter *fpr, int li, int clip)
{
    const fp_config *cfg = &c->cfg;
    const track *tr = c->clips[clip];
    const int sr = cfg->sample_rate;
    const size_t len = (size_t)lround(c->lengths[li] * sr);
    const size_t s = c->starts[(size_t)li * c->num_clips + clip];
    const int holdout = clip >= c->num_indexed_clips;
    const double r = (double)s / cfg->hop_size;
    const int32_t true_bin = (int32_t)floor(r / cfg->offset_bin);

    fp_analysis clean;
    fp_pair *clean_pairs;
    size_t clean_np = 0;
    if (fp_analyze(fpr, tr->x + s, len, 0, &clean) ||
        fp_pairs_build(cfg, clean.peaks, clean.num_peaks, &clean_pairs, &clean_np))
        die("fingerprint failed");
    uint64_t *clean_keys = pair_keys(clean_pairs, clean_np);
    uint8_t *exact = malloc(clean.num_peaks ? clean.num_peaks : 1);
    if (!exact)
        die("out of memory");

    /* Pre-roll lets the echo tail and filter state settle; it is cut before analysis so framing matches the clean clip. */
    const size_t pre = s < (size_t)(PRE_ROLL_S * sr) ? s : (size_t)(PRE_ROLL_S * sr);
    float *base = malloc((pre + len) * sizeof *base);
    float *deg = malloc(len * sizeof *deg);
    if (!base || !deg)
        die("out of memory");

    for (int ci = 0; ci < c->num_channels; ci++) {
        const int ch = c->channels[ci];
        memcpy(base, tr->x + s - pre, (pre + len) * sizeof *base);
        apply_channel(ch, base, pre + len, sr, seed_of(c->seed, 2, tr->id, li, clip));
        const uint64_t noise_seed = seed_of(c->seed, 3, tr->id, li, (uint64_t)clip * CH_COUNT + ch);

        for (int si = 0; si < c->num_snrs; si++) {
            memcpy(deg, base + pre, len * sizeof *deg);
            if (isfinite(c->snrs[si])) {
                fp_rng nr;
                fp_rng_seed(&nr, noise_seed);
                fp_channel_noise(deg, len, c->snrs[si], &nr);
            }
            fp_analysis a;
            fp_pair *pairs;
            size_t np = 0;
            if (fp_analyze(fpr, deg, len, 0, &a) || fp_pairs_build(cfg, a.peaks, a.num_peaks, &pairs, &np))
                die("fingerprint failed");

            fp_votes vm, vn;
            fp_match_result rm, rn;
            if (fp_index_query(c->modern, pairs, np, &vm))
                die("query failed");
            fp_match_decide(cfg, &vm, &rm);
            if (c->use_naive) {
                if (fp_index_query(c->naive, pairs, np, &vn))
                    die("query failed");
                fp_match_decide(cfg, &vn, &rn);
                if (!votes_equal(&vm, &vn) || !fp_match_equal(&rm, &rn)) {
                    fprintf(stderr,
                            "BACKENDS DIFFER: track %u start %zu len %.1f s channel %s snr %g "
                            "(modern t%u o%d s%u, naive t%u o%d s%u)\n",
                            tr->id, s, c->lengths[li], CHANNEL_NAMES[ch], c->snrs[si], rm.track, rm.offset,
                            rm.score, rn.track, rn.offset, rn.score);
                    exit(1);
                }
                fp_votes_free(&vn);
            }

            record *o = &c->rec[rec_index(c, li, clip, ci, si)];
            memset(o, 0, sizeof *o);
            o->holdout = (uint8_t)holdout;
            o->clip_track = tr->id;
            o->true_track = holdout ? FP_NO_TRACK : tr->id;
            o->r = r;
            o->matched = (uint8_t)rm.matched;
            o->track = rm.track;
            o->offset = rm.offset;
            o->score = rm.score;
            o->runner_up = rm.runner_up;
            o->entries = rm.entries_scanned;
            o->pairs = (uint32_t)np;
            o->query_peaks = (uint32_t)a.num_peaks;
            if (!holdout) {
                const int32_t win_bin = rm.offset / cfg->offset_bin;
                o->track_hit = (uint8_t)(rm.matched && rm.track == tr->id);
                o->hit = (uint8_t)(o->track_hit && abs(win_bin - true_bin) <= 1);
                for (size_t k = 0; k < vm.n; k++) {
                    const fp_vote *v = &vm.v[k];
                    if (v->track == tr->id) {
                        if (abs(v->bin - true_bin) <= 1 && v->count > o->true_score)
                            o->true_score = v->count;
                        if (abs(v->bin - true_bin) <= 1) {
                            uint32_t w = v->count;
                            if (cfg->score_window == 2 && k + 1 < vm.n && vm.v[k + 1].track == tr->id &&
                                vm.v[k + 1].bin == v->bin + 1)
                                w += vm.v[k + 1].count;
                            if (w > o->true_window_score)
                                o->true_window_score = w;
                        }
                    } else if (v->count > o->wrong_score) {
                        o->wrong_score = v->count;
                    }
                }
            }
            o->clean_peaks = (uint32_t)clean.num_peaks;
            o->surv_peaks = peak_survivors(&clean, &a, exact, &o->surv_peaks_exact);
            for (size_t k = 0; k < clean_np; k++)
                o->both_exact += exact[clean_pairs[k].a_idx] && exact[clean_pairs[k].b_idx];
            uint64_t *keys = pair_keys(pairs, np);
            o->clean_hashes = (uint32_t)clean_np;
            o->surv_hashes = hash_survivors(clean_keys, clean_np, keys, np);

            free(keys);
            fp_votes_free(&vm);
            fp_pairs_free(pairs);
            fp_analysis_free(&a);
        }
    }
    free(base);
    free(deg);
    free(clean_keys);
    free(exact);
    fp_pairs_free(clean_pairs);
    fp_analysis_free(&clean);
}

static void *worker(void *arg)
{
    ctx *c = arg;
    fp_fingerprinter *fpr = fp_fingerprinter_create(&c->cfg);
    if (!fpr)
        die("out of memory");
    for (;;) {
        pthread_mutex_lock(&c->lock);
        const int u = c->next++;
        pthread_mutex_unlock(&c->lock);
        if (u >= c->total)
            break;
        run_unit(c, fpr, u / c->num_clips, u % c->num_clips);
    }
    fp_fingerprinter_free(fpr);
    return NULL;
}

typedef struct {
    const ctx *c;
    const track *tracks;
    size_t num_tracks;
    fp_pair **pairs;
    size_t *num_pairs;
    int *frames;
    size_t *peaks;
    pthread_mutex_t lock;
    size_t next;
} build_ctx;

static void *build_worker(void *arg)
{
    build_ctx *b = arg;
    fp_fingerprinter *fpr = fp_fingerprinter_create(&b->c->cfg);
    if (!fpr)
        die("out of memory");
    for (;;) {
        pthread_mutex_lock(&b->lock);
        const size_t i = b->next++;
        pthread_mutex_unlock(&b->lock);
        if (i >= b->num_tracks)
            break;
        fp_analysis a;
        if (fp_analyze(fpr, b->tracks[i].x, b->tracks[i].n, 0, &a) ||
            fp_pairs_build(&b->c->cfg, a.peaks, a.num_peaks, &b->pairs[i], &b->num_pairs[i]))
            die("fingerprint failed");
        b->frames[i] = a.frames;
        b->peaks[i] = a.num_peaks;
        fp_analysis_free(&a);
    }
    fp_fingerprinter_free(fpr);
    return NULL;
}

static int parse_list(const char *s, double *out, int max)
{
    int n = 0;
    while (*s && n < max) {
        char *end;
        if (!strncmp(s, "inf", 3)) {
            out[n++] = INFINITY;
            end = (char *)s + 3;
        } else {
            out[n++] = strtod(s, &end);
            if (end == s)
                return -1;
        }
        s = *end == ',' ? end + 1 : end;
        if (*end && *end != ',')
            return -1;
    }
    return n;
}

static int parse_channels(const char *s, int *out, int max)
{
    int n = 0;
    while (*s && n < max) {
        size_t len = strcspn(s, ",");
        int found = -1;
        for (int k = 0; k < CH_COUNT; k++)
            if (strlen(CHANNEL_NAMES[k]) == len && !strncmp(s, CHANNEL_NAMES[k], len))
                found = k;
        if (found < 0)
            return -1;
        out[n++] = found;
        s += len;
        if (*s == ',')
            s++;
    }
    return n;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, size_t n)
{
    if (!n)
        return NAN;
    qsort(v, n, sizeof *v, cmp_double);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <manifest.tsv> [--out csv] [--append] [--detail csv] [--label name]\n"
            "          [--lengths 3,5,10] [--channels clean,speaker,echo,speaker+echo] [--snrs inf,30,20,10,5,0]\n"
            "          [--n-indexed 100] [--n-holdout 50] [--seed n] [--threads n] [--impl both|modern]\n" CONFIG_ARGS_USAGE,
            prog);
}

int main(int argc, char **argv)
{
    ctx c;
    memset(&c, 0, sizeof c);
    c.cfg = fp_config_default();
    c.seed = 1;
    c.use_naive = 1;
    const char *manifest_path = NULL, *out_path = NULL, *detail_path = NULL, *label = "default";
    const char *lengths_s = "3,5,10", *channels_s = "clean,speaker,echo,speaker+echo", *snrs_s = "inf,30,20,10,5,0";
    int n_indexed = 100, n_holdout = 50, append = 0;
    long threads = sysconf(_SC_NPROCESSORS_ONLN);

    for (int i = 1; i < argc;) {
        int rc = config_arg(&c.cfg, argc, argv, &i);
        if (rc < 0) {
            usage(argv[0]);
            return 2;
        }
        if (rc)
            continue;
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--append")) {
            append = 1;
            i++;
            continue;
        }
        if (a[0] != '-') {
            if (manifest_path) {
                usage(argv[0]);
                return 2;
            }
            manifest_path = a;
            i++;
            continue;
        }
        if (!v) {
            usage(argv[0]);
            return 2;
        }
        if (!strcmp(a, "--out"))
            out_path = v;
        else if (!strcmp(a, "--detail"))
            detail_path = v;
        else if (!strcmp(a, "--label"))
            label = v;
        else if (!strcmp(a, "--lengths"))
            lengths_s = v;
        else if (!strcmp(a, "--channels"))
            channels_s = v;
        else if (!strcmp(a, "--snrs"))
            snrs_s = v;
        else if (!strcmp(a, "--n-indexed"))
            n_indexed = atoi(v);
        else if (!strcmp(a, "--n-holdout"))
            n_holdout = atoi(v);
        else if (!strcmp(a, "--seed"))
            c.seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--threads"))
            threads = atol(v);
        else if (!strcmp(a, "--impl") && (!strcmp(v, "both") || !strcmp(v, "modern")))
            c.use_naive = !strcmp(v, "both");
        else {
            usage(argv[0]);
            return 2;
        }
        i += 2;
    }
    c.num_lengths = parse_list(lengths_s, c.lengths, MAX_LIST);
    c.num_snrs = parse_list(snrs_s, c.snrs, MAX_LIST);
    c.num_channels = parse_channels(channels_s, c.channels, MAX_LIST);
    if (!manifest_path || fp_config_validate(&c.cfg) || c.num_lengths <= 0 || c.num_snrs <= 0 ||
        c.num_channels <= 0 || threads < 1 || n_indexed < 0 || n_holdout < 0) {
        usage(argv[0]);
        return 2;
    }
    const fp_config *cfg = &c.cfg;
    const int sr = cfg->sample_rate;

    manifest man;
    if (manifest_read(manifest_path, &man))
        die("cannot read manifest");
    track *idx_tracks = calloc(man.n, sizeof *idx_tracks);
    track *hold_tracks = calloc(man.n, sizeof *hold_tracks);
    size_t n_idx = 0, n_hold = 0;
    for (size_t r = 0; r < man.n; r++) {
        track t;
        t.id = man.rows[r].id;
        if (wav_read_at_rate(man.rows[r].path, sr, &t.x, &t.n)) {
            fprintf(stderr, "cannot read %s\n", man.rows[r].path);
            return 1;
        }
        if (man.rows[r].holdout)
            hold_tracks[n_hold++] = t;
        else
            idx_tracks[n_idx++] = t;
    }
    if ((size_t)n_indexed > n_idx || (size_t)n_holdout > n_hold)
        die("not enough tracks in the manifest for --n-indexed / --n-holdout");

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    build_ctx b;
    memset(&b, 0, sizeof b);
    b.c = &c;
    b.tracks = idx_tracks;
    b.num_tracks = n_idx;
    b.pairs = calloc(n_idx, sizeof *b.pairs);
    b.num_pairs = calloc(n_idx, sizeof *b.num_pairs);
    b.frames = calloc(n_idx, sizeof *b.frames);
    b.peaks = calloc(n_idx, sizeof *b.peaks);
    pthread_mutex_init(&b.lock, NULL);
    pthread_t *th = malloc((size_t)threads * sizeof *th);
    for (long k = 0; k < threads; k++)
        pthread_create(&th[k], NULL, build_worker, &b);
    for (long k = 0; k < threads; k++)
        pthread_join(th[k], NULL);

    c.modern = fp_index_create(FP_IMPL_MODERN, cfg);
    c.naive = c.use_naive ? fp_index_create(FP_IMPL_NAIVE2003, cfg) : NULL;
    if (!c.modern || (c.use_naive && !c.naive))
        die("out of memory");
    size_t db_peaks = 0, db_samples = 0;
    for (size_t i = 0; i < n_idx; i++) {
        if (fp_index_add(c.modern, idx_tracks[i].id, (uint32_t)b.frames[i], b.pairs[i], b.num_pairs[i]) ||
            (c.naive && fp_index_add(c.naive, idx_tracks[i].id, (uint32_t)b.frames[i], b.pairs[i], b.num_pairs[i])))
            die("index add failed");
        db_peaks += b.peaks[i];
        db_samples += idx_tracks[i].n;
        fp_pairs_free(b.pairs[i]);
    }
    if (fp_index_finalize(c.modern) || (c.naive && fp_index_finalize(c.naive)))
        die("finalize failed");
    const double db_pps = (double)db_peaks / ((double)db_samples / sr);

    printf("config: threshold %g, min_magnitude_db %g, fan_out %d, min_dt %d, max_dt %d, max_df %d, score_window %d\n",
           cfg->threshold, cfg->min_magnitude_db, cfg->fan_out, cfg->min_dt, cfg->max_dt, cfg->max_df, cfg->score_window);
    printf("index: %zu tracks, %zu entries, %.1f peaks/s\n", n_idx, fp_index_num_entries(c.modern), db_pps);
    fp_hash_stats hs;
    if (fp_index_hash_stats(c.modern, &hs) == 0)
        hash_stats_print(stdout, &hs);

    /* Seeded shuffles pick distinct tracks; clip choice and starts are independent of the config. */
    fp_rng pick;
    fp_rng_seed(&pick, mix(c.seed, 1));
    for (size_t i = n_idx; i > 1; i--) {
        size_t j = (size_t)(fp_rng_u64(&pick) % i);
        track t = idx_tracks[i - 1];
        idx_tracks[i - 1] = idx_tracks[j];
        idx_tracks[j] = t;
    }
    for (size_t i = n_hold; i > 1; i--) {
        size_t j = (size_t)(fp_rng_u64(&pick) % i);
        track t = hold_tracks[i - 1];
        hold_tracks[i - 1] = hold_tracks[j];
        hold_tracks[j] = t;
    }
    c.num_indexed_clips = n_indexed;
    c.num_clips = n_indexed + n_holdout;
    c.clips = malloc((size_t)c.num_clips * sizeof *c.clips);
    for (int k = 0; k < n_indexed; k++)
        c.clips[k] = &idx_tracks[k];
    for (int k = 0; k < n_holdout; k++)
        c.clips[n_indexed + k] = &hold_tracks[k];

    c.starts = malloc((size_t)c.num_lengths * c.num_clips * sizeof *c.starts);
    for (int li = 0; li < c.num_lengths; li++) {
        const size_t len = (size_t)lround(c.lengths[li] * sr);
        for (int k = 0; k < c.num_clips; k++) {
            const track *t = c.clips[k];
            if (t->n < len + 1)
                die("track shorter than clip length");
            fp_rng r;
            fp_rng_seed(&r, seed_of(c.seed, 4, t->id, (uint64_t)lround(c.lengths[li] * 1000), 0));
            size_t s;
            do
                s = (size_t)(fp_rng_u64(&r) % (t->n - len + 1));
            while (s % (size_t)cfg->hop_size == 0);
            c.starts[(size_t)li * c.num_clips + k] = s;
        }
    }

    const size_t nrec = (size_t)c.num_lengths * c.num_clips * c.num_channels * c.num_snrs;
    c.rec = calloc(nrec, sizeof *c.rec);
    c.total = c.num_lengths * c.num_clips;
    pthread_mutex_init(&c.lock, NULL);
    for (long k = 0; k < threads; k++)
        pthread_create(&th[k], NULL, worker, &c);
    for (long k = 0; k < threads; k++)
        pthread_join(th[k], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("%zu queries%s in %.1f s on %ld threads\n", nrec, c.use_naive ? " x 2 backends (all agree)" : "",
           (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9, threads);

    FILE *out = NULL;
    if (out_path) {
        out = fopen(out_path, append ? "a" : "w");
        if (!out)
            die("cannot open --out");
        if (!append || ftell(out) == 0)
            fprintf(out, "density,threshold,min_mag_db,fan_out,min_dt,db_peaks_per_s,clip_len,channel,snr,"
                         "n_indexed,n_holdout,recall,track_recall,wrong_offset_rate,wrong_track_rate,"
                         "holdout_fp_rate,peak_survival,hash_survival,pred_hash_survival,pair_endpoint_survival,median_vote_ratio,"
                         "query_peaks_per_s,hashes_per_query,entries_scanned_per_query,"
                         "peak_survival_exact,pred_hash_survival_exact\n");
    }
    FILE *detail = NULL;
    if (detail_path) {
        detail = fopen(detail_path, append ? "a" : "w");
        if (!detail)
            die("cannot open --detail");
        if (!append || ftell(detail) == 0)
            fprintf(detail, "density,clip_len,channel,snr,holdout,clip_track,start,r,matched,track,offset,score,runner_up,"
                            "true_score,wrong_score,hit,track_hit,entries,pairs,true_window_score\n");
    }

    printf("\n%-5s %-13s %5s | %6s %6s %6s %6s | %6s %6s %6s %6s %7s | %6s %7s %10s %10s\n", "len", "channel", "snr",
           "recall", "track", "wrongT", "holdFP", "pSurv", "hSurv", "pSurv2", "both", "vratio", "pk/s", "hashes",
           "scan/q", "scan/q50");
    double *tmp = malloc((size_t)c.num_clips * sizeof *tmp);
    double *rec_by_snr = malloc((size_t)c.num_snrs * sizeof *rec_by_snr);
    int violations = 0;
    for (int li = 0; li < c.num_lengths; li++)
        for (int ci = 0; ci < c.num_channels; ci++) {
            for (int si = 0; si < c.num_snrs; si++) {
                int hits = 0, thits = 0, wrongt = 0, wrongo = 0, fps = 0;
                double cp = 0, sp = 0, spe = 0, ch = 0, sh = 0, be = 0, ent = 0, prs = 0, qp = 0;
                size_t nr = 0;
                for (int k = 0; k < c.num_clips; k++) {
                    const record *o = &c.rec[rec_index(&c, li, k, ci, si)];
                    cp += o->clean_peaks;
                    sp += o->surv_peaks;
                    spe += o->surv_peaks_exact;
                    ch += o->clean_hashes;
                    sh += o->surv_hashes;
                    be += o->both_exact;
                    ent += (double)o->entries;
                    prs += o->pairs;
                    qp += o->query_peaks;
                    if (o->holdout) {
                        fps += o->matched;
                    } else {
                        hits += o->hit;
                        thits += o->track_hit;
                        wrongt += o->matched && o->track != o->true_track;
                        wrongo += o->track_hit && !o->hit;
                        tmp[nr++] = (double)o->true_score / (o->wrong_score ? o->wrong_score : 1);
                    }
                    if (detail)
                        fprintf(detail, "%s,%g,%s,%g,%d,%u,%zu,%.4f,%d,%d,%d,%u,%u,%u,%u,%d,%d,%llu,%u,%u\n", label,
                                c.lengths[li], CHANNEL_NAMES[c.channels[ci]], c.snrs[si],
                                o->holdout, o->clip_track,
                                c.starts[(size_t)li * c.num_clips + k], o->r, o->matched,
                                o->track == FP_NO_TRACK ? -1 : (int)o->track, o->offset, o->score, o->runner_up,
                                o->true_score, o->wrong_score, o->hit, o->track_hit,
                                (unsigned long long)o->entries, o->pairs, o->true_window_score);
                }
                const double vr = median(tmp, nr);
                for (int k = 0; k < c.num_clips; k++)
                    tmp[k] = (double)c.rec[rec_index(&c, li, k, ci, si)].entries;
                const double ent_med = median(tmp, (size_t)c.num_clips);
                const double ni = n_indexed ? n_indexed : 1, nh = n_holdout ? n_holdout : 1;
                const double recall = hits / ni;
                const double psurv = cp ? sp / cp : NAN, hsurv = ch ? sh / ch : NAN;
                const double pexact = cp ? spe / cp : NAN;
                const double pps = qp / (c.num_clips * c.lengths[li]);
                rec_by_snr[si] = recall;
                printf("%-5g %-13s %5g | %6.3f %6.3f %6.3f %6.3f | %6.3f %6.3f %6.3f %6.3f %7.2f | %6.1f %7.0f %10.0f %10.0f\n",
                       c.lengths[li], CHANNEL_NAMES[c.channels[ci]], c.snrs[si], recall, thits / ni, wrongt / ni,
                       fps / nh, psurv, hsurv, psurv * psurv, ch ? be / ch : NAN, vr, pps, prs / c.num_clips,
                       ent / c.num_clips, ent_med);
                if (out)
                    fprintf(out,
                            "%s,%g,%g,%d,%d,%.2f,%g,%s,%g,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.2f,%.1f,%.1f,%.4f,%.4f\n",
                            label, cfg->threshold, cfg->min_magnitude_db, cfg->fan_out, cfg->min_dt, db_pps,
                            c.lengths[li], CHANNEL_NAMES[c.channels[ci]], c.snrs[si], n_indexed, n_holdout, recall,
                            thits / ni, wrongo / ni, wrongt / ni, fps / nh, psurv, hsurv, psurv * psurv, ch ? be / ch : NAN, vr, pps,
                            prs / c.num_clips, ent / c.num_clips, pexact, pexact * pexact);
            }
            /* SNRs are listed from cleanest to noisiest, so recall must not rise along the list. */
            for (int si = 1; si < c.num_snrs; si++)
                if (c.snrs[si] < c.snrs[si - 1] && rec_by_snr[si] > rec_by_snr[si - 1]) {
                    printf("NON-MONOTONIC: len %g %s: recall %.3f at %g dB > %.3f at %g dB\n", c.lengths[li],
                           CHANNEL_NAMES[c.channels[ci]], rec_by_snr[si], c.snrs[si], rec_by_snr[si - 1],
                           c.snrs[si - 1]);
                    violations++;
                }
        }
    printf("monotonicity violations: %d\n", violations);

    if (out)
        fclose(out);
    if (detail)
        fclose(detail);
    return 0;
}
