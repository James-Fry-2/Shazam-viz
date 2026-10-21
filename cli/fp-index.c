#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "config_args.h"
#include "fingerprint.h"
#include "hash_stats.h"
#include "index.h"
#include "manifest.h"
#include "wav.h"

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s <manifest.tsv> -o <index.bin> [config flags]\n" CONFIG_ARGS_USAGE, prog);
}

int main(int argc, char **argv)
{
    const char *manifest_path = NULL, *out_path = NULL;
    fp_config cfg = fp_config_default();
    for (int i = 1; i < argc;) {
        int r = config_arg(&cfg, argc, argv, &i);
        if (r < 0) {
            usage(argv[0]);
            return 2;
        }
        if (r)
            continue;
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_path = argv[i + 1];
            i += 2;
        } else if (argv[i][0] != '-' && !manifest_path) {
            manifest_path = argv[i++];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!manifest_path || !out_path || fp_config_validate(&cfg)) {
        usage(argv[0]);
        return 2;
    }

    manifest man;
    if (manifest_read(manifest_path, &man)) {
        fprintf(stderr, "cannot read manifest %s\n", manifest_path);
        return 1;
    }
    fp_fingerprinter *fpr = fp_fingerprinter_create(&cfg);
    fp_index *idx = fp_index_create(FP_IMPL_MODERN, &cfg);
    if (!fpr || !idx) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    size_t tracks = 0, peaks = 0, samples = 0;
    for (size_t r = 0; r < man.n; r++) {
        if (man.rows[r].holdout)
            continue;
        float *x;
        size_t n;
        if (wav_read_at_rate(man.rows[r].path, cfg.sample_rate, &x, &n)) {
            fprintf(stderr, "cannot read %s\n", man.rows[r].path);
            return 1;
        }
        fp_analysis a;
        fp_pair *pairs;
        size_t np;
        if (fp_analyze(fpr, x, n, 0, &a) || fp_pairs_build(&cfg, a.peaks, a.num_peaks, &pairs, &np) ||
            fp_index_add(idx, man.rows[r].id, (uint32_t)a.frames, pairs, np)) {
            fprintf(stderr, "failed to index %s\n", man.rows[r].path);
            return 1;
        }
        peaks += a.num_peaks;
        samples += n;
        tracks++;
        fp_pairs_free(pairs);
        fp_analysis_free(&a);
        free(x);
    }
    if (fp_index_finalize(idx) || fp_index_save(idx, out_path)) {
        fprintf(stderr, "cannot write %s\n", out_path);
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    struct stat st;
    stat(out_path, &st);
    const double seconds = (double)samples / cfg.sample_rate;
    printf("tracks:      %zu\n", tracks);
    printf("entries:     %zu\n", fp_index_num_entries(idx));
    printf("peaks/s:     %.1f\n", seconds > 0 ? (double)peaks / seconds : 0.0);
    printf("bytes:       %lld (file), %zu (in memory)\n", (long long)st.st_size, fp_index_bytes(idx));
    printf("time:        %.2f s\n", (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9);

    fp_hash_stats hs;
    if (fp_index_hash_stats(idx, &hs) == 0)
        hash_stats_print(stdout, &hs);

    fp_index_free(idx);
    fp_fingerprinter_free(fpr);
    manifest_free(&man);
    return 0;
}
