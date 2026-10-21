#ifndef FP_INDEX_H
#define FP_INDEX_H

#include <stddef.h>
#include <stdint.h>

#include "fingerprint.h"

typedef enum {
    FP_IMPL_NAIVE2003,
    FP_IMPL_MODERN
} fp_impl;

typedef struct {
    uint32_t track;
    int32_t bin;
    uint32_t count;
} fp_vote;

/*
 * Votes are sorted by (track, bin) ascending in both backends.
 * entries_scanned counts index entries whose hash equals a query hash;
 * hashes_used counts query pairs with at least one such entry.
 */
typedef struct {
    fp_vote *v;
    size_t n;
    uint64_t entries_scanned;
    uint32_t hashes_used;
} fp_votes;

typedef struct fp_index fp_index;

/* weighted_list = sum(n_h^2) / sum(n_h): the expected entries scanned per query hash drawn from the index distribution. */
typedef struct {
    size_t entries;
    size_t distinct;
    size_t max_list;
    double mean_list;
    double weighted_list;
    size_t same_f;
    size_t dt_hist[64];
} fp_hash_stats;

fp_index *fp_index_create(fp_impl impl, const fp_config *cfg);
int fp_index_add(fp_index *idx, uint32_t track, uint32_t num_frames, const fp_pair *pairs, size_t n);
int fp_index_finalize(fp_index *idx);
int fp_index_query(const fp_index *idx, const fp_pair *q, size_t n, fp_votes *out);
size_t fp_index_bytes(const fp_index *idx);
size_t fp_index_num_entries(const fp_index *idx);
uint32_t fp_index_num_tracks(const fp_index *idx);
int fp_index_hash_stats(const fp_index *idx, fp_hash_stats *out);
void fp_votes_free(fp_votes *v);
void fp_index_free(fp_index *idx);

/* Returns 0 on success, -1 on I/O or format error, -2 if the file's config differs from expect. */
int fp_index_save(const fp_index *idx, const char *path);
int fp_index_load(fp_index **out, fp_impl impl, const fp_config *expect, const char *path);

#endif
