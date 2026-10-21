#ifndef FP_INDEX_INTERNAL_H
#define FP_INDEX_INTERNAL_H

#include "index.h"

#define FP_MAX_TRACKS (1u << 21)
#define FP_MAX_TIME (1u << 19)

typedef struct {
    void *(*create)(const fp_config *cfg);
    void (*destroy)(void *self);
    int (*add_packed)(void *self, const uint64_t *entries, size_t n);
    int (*finalize)(void *self);
    int (*query)(const void *self, const fp_config *cfg, const fp_pair *q, size_t n, fp_votes *out);
    size_t (*bytes)(const void *self);
    size_t (*num_entries)(const void *self);
    /* Writes num_entries packed entries in ascending order. */
    int (*export_sorted)(const void *self, uint64_t *out);
} fp_index_vt;

extern const fp_index_vt fp_vt_naive;
extern const fp_index_vt fp_vt_modern;

struct fp_index {
    const fp_index_vt *vt;
    void *self;
    fp_config cfg;
    uint32_t *track_frames;
    uint32_t num_tracks;
    uint32_t tracks_cap;
};

int fp_index_set_track(fp_index *idx, uint32_t track, uint32_t num_frames);
int fp_index_add_packed(fp_index *idx, const uint64_t *entries, size_t n);

static inline int32_t fp_floor_div(int32_t a, int32_t b)
{
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static inline int fp_cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static inline int fp_cmp_vote(const void *a, const void *b)
{
    const fp_vote *x = a;
    const fp_vote *y = b;
    if (x->track != y->track)
        return x->track < y->track ? -1 : 1;
    return (x->bin > y->bin) - (x->bin < y->bin);
}

#endif
