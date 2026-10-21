#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_args.h"
#include "fingerprint.h"
#include "index.h"
#include "match.h"
#include "wav.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <index.bin> <query.wav> [--start s] [--len s] [--impl naive|modern|both] [config flags]\n"
            CONFIG_ARGS_USAGE,
            prog);
}

static void print_result(const fp_config *cfg, const char *name, const fp_match_result *r)
{
    const double sec_per_frame = (double)cfg->hop_size / cfg->sample_rate;
    printf("[%s] ", name);
    if (r->matched)
        printf("MATCH track %u at offset %d frames (%.2f s into the track), score %u, runner-up %u\n", r->track,
               r->offset, r->offset * sec_per_frame, r->score, r->runner_up);
    else
        printf("NO MATCH (best: track %u, score %u, runner-up %u)\n", r->top[0].track, r->score, r->runner_up);
    printf("        hashes used %u, entries scanned %llu, top3 = [t%u o%d s%u] [t%u o%d s%u] [t%u o%d s%u]\n",
           r->hashes_used, (unsigned long long)r->entries_scanned, r->top[0].track, r->top[0].offset,
           r->top[0].score, r->top[1].track, r->top[1].offset, r->top[1].score, r->top[2].track,
           r->top[2].offset, r->top[2].score);
}

static int run_impl(const fp_config *cfg, fp_impl impl, const char *index_path, const fp_pair *pairs, size_t np,
                    fp_match_result *out)
{
    fp_index *idx = NULL;
    int rc = fp_index_load(&idx, impl, cfg, index_path);
    if (rc == -2) {
        fprintf(stderr, "index was built with different fingerprint settings; pass the same config flags\n");
        return -1;
    }
    if (rc) {
        fprintf(stderr, "cannot load index %s\n", index_path);
        return -1;
    }
    fp_votes votes;
    if (fp_index_query(idx, pairs, np, &votes)) {
        fprintf(stderr, "query failed\n");
        fp_index_free(idx);
        return -1;
    }
    fp_match_decide(cfg, &votes, out);
    fp_votes_free(&votes);
    fp_index_free(idx);
    return 0;
}

int main(int argc, char **argv)
{
    const char *index_path = NULL, *wav_path = NULL;
    const char *impl_s = "both";
    double start_s = 0.0, len_s = 0.0;
    fp_config cfg = fp_config_default();
    for (int i = 1; i < argc;) {
        int r = config_arg(&cfg, argc, argv, &i);
        if (r < 0) {
            usage(argv[0]);
            return 2;
        }
        if (r)
            continue;
        if (!strcmp(argv[i], "--start") && i + 1 < argc) {
            start_s = atof(argv[i + 1]);
            i += 2;
        } else if (!strcmp(argv[i], "--len") && i + 1 < argc) {
            len_s = atof(argv[i + 1]);
            i += 2;
        } else if (!strcmp(argv[i], "--impl") && i + 1 < argc) {
            impl_s = argv[i + 1];
            i += 2;
        } else if (argv[i][0] != '-' && !index_path) {
            index_path = argv[i++];
        } else if (argv[i][0] != '-' && !wav_path) {
            wav_path = argv[i++];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    const int naive = !strcmp(impl_s, "naive") || !strcmp(impl_s, "both");
    const int modern = !strcmp(impl_s, "modern") || !strcmp(impl_s, "both");
    if (!index_path || !wav_path || !(naive || modern) || start_s < 0 || len_s < 0 || fp_config_validate(&cfg)) {
        usage(argv[0]);
        return 2;
    }

    float *x;
    size_t n;
    if (wav_read_at_rate(wav_path, cfg.sample_rate, &x, &n)) {
        fprintf(stderr, "cannot read %s\n", wav_path);
        return 1;
    }
    size_t start = (size_t)(start_s * cfg.sample_rate);
    if (start >= n) {
        fprintf(stderr, "--start %.2f s is past the end of the %.2f s file\n", start_s, (double)n / cfg.sample_rate);
        return 1;
    }
    size_t len = len_s > 0 ? (size_t)(len_s * cfg.sample_rate) : n - start;
    if (len > n - start)
        len = n - start;

    fp_fingerprinter *fpr = fp_fingerprinter_create(&cfg);
    fp_analysis a;
    fp_pair *pairs;
    size_t np;
    if (!fpr || fp_analyze(fpr, x + start, len, 0, &a) || fp_pairs_build(&cfg, a.peaks, a.num_peaks, &pairs, &np)) {
        fprintf(stderr, "clip too short to fingerprint (%.2f s)\n", (double)len / cfg.sample_rate);
        return 1;
    }
    printf("clip: %s, %.2f s from %.2f s, %zu peaks (%.1f/s), %zu pairs\n", wav_path, (double)len / cfg.sample_rate,
           (double)start / cfg.sample_rate, a.num_peaks, a.num_peaks / ((double)len / cfg.sample_rate), np);
    printf("expected offset if the clip is an excerpt of the track at the same start: %.1f frames\n",
           (double)start / cfg.hop_size);

    fp_match_result res[2];
    int rc = 0;
    if (naive && run_impl(&cfg, FP_IMPL_NAIVE2003, index_path, pairs, np, &res[0]))
        rc = 1;
    if (!rc && modern && run_impl(&cfg, FP_IMPL_MODERN, index_path, pairs, np, &res[1]))
        rc = 1;
    if (!rc) {
        if (naive)
            print_result(&cfg, "naive2003", &res[0]);
        if (modern)
            print_result(&cfg, "modern", &res[1]);
        if (naive && modern) {
            const int same = fp_match_equal(&res[0], &res[1]);
            printf("backends %s\n", same ? "agree" : "DIFFER");
            if (!same)
                rc = 1;
        }
    }

    fp_pairs_free(pairs);
    fp_analysis_free(&a);
    fp_fingerprinter_free(fpr);
    free(x);
    return rc;
}
