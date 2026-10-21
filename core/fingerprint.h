#ifndef FP_FINGERPRINT_H
#define FP_FINGERPRINT_H

#include <stddef.h>
#include <stdint.h>

#include "fp_config.h"

/* Peak frequency bins are restricted to 1..511 so f fits in 9 bits. */
#define FP_MAX_F 511

typedef struct {
    int32_t t;
    int32_t f;
    float mag;
} fp_peak;

typedef struct {
    uint32_t hash;
    int32_t t_anchor;
    int32_t a_idx;
    int32_t b_idx;
} fp_pair;

typedef struct {
    /* frames * bins of log1p|X|; NULL unless requested */
    float *spec;
    int frames;
    int bins;
    fp_peak *peaks;
    size_t num_peaks;
} fp_analysis;

typedef struct fp_fingerprinter fp_fingerprinter;

int fp_config_validate(const fp_config *c);

fp_fingerprinter *fp_fingerprinter_create(const fp_config *c);
void fp_fingerprinter_free(fp_fingerprinter *fpr);

int fp_analyze(fp_fingerprinter *fpr, const float *x, size_t n, int keep_spec, fp_analysis *out);
void fp_analysis_free(fp_analysis *a);

int fp_pairs_build(const fp_config *c, const fp_peak *peaks, size_t n, fp_pair **out, size_t *num);
void fp_pairs_free(fp_pair *p);

/* hash = f1<<15 | f2<<6 | dt, 24 bits */
uint32_t fp_hash_pack(int f1, int f2, int dt);
void fp_hash_unpack(uint32_t h, int *f1, int *f2, int *dt);

/* entry = hash:24 | track:21 | time:19 */
uint64_t fp_entry_pack(uint32_t hash, uint32_t track, uint32_t time);
void fp_entry_unpack(uint64_t e, uint32_t *hash, uint32_t *track, uint32_t *time);

#endif
