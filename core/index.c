#include <stdlib.h>
#include <string.h>

#include "index_internal.h"

fp_index *fp_index_create(fp_impl impl, const fp_config *cfg)
{
    if (fp_config_validate(cfg) != 0)
        return NULL;
    fp_index *idx = calloc(1, sizeof *idx);
    if (!idx)
        return NULL;
    idx->vt = impl == FP_IMPL_NAIVE2003 ? &fp_vt_naive : &fp_vt_modern;
    idx->cfg = *cfg;
    idx->self = idx->vt->create(cfg);
    if (!idx->self) {
        free(idx);
        return NULL;
    }
    return idx;
}

void fp_index_free(fp_index *idx)
{
    if (!idx)
        return;
    idx->vt->destroy(idx->self);
    free(idx->track_frames);
    free(idx);
}

int fp_index_set_track(fp_index *idx, uint32_t track, uint32_t num_frames)
{
    if (track >= FP_MAX_TRACKS || num_frames > FP_MAX_TIME)
        return -1;
    if (track >= idx->tracks_cap) {
        uint32_t cap = idx->tracks_cap ? idx->tracks_cap : 16;
        while (cap <= track)
            cap *= 2;
        uint32_t *grown = realloc(idx->track_frames, (size_t)cap * sizeof *grown);
        if (!grown)
            return -1;
        memset(grown + idx->tracks_cap, 0, (size_t)(cap - idx->tracks_cap) * sizeof *grown);
        idx->track_frames = grown;
        idx->tracks_cap = cap;
    }
    idx->track_frames[track] = num_frames;
    if (track >= idx->num_tracks)
        idx->num_tracks = track + 1;
    return 0;
}

int fp_index_add_packed(fp_index *idx, const uint64_t *entries, size_t n)
{
    return idx->vt->add_packed(idx->self, entries, n);
}

int fp_index_add(fp_index *idx, uint32_t track, uint32_t num_frames, const fp_pair *pairs, size_t n)
{
    if (fp_index_set_track(idx, track, num_frames) != 0)
        return -1;
    uint64_t *packed = malloc((n ? n : 1) * sizeof *packed);
    if (!packed)
        return -1;
    for (size_t i = 0; i < n; i++) {
        if ((uint32_t)pairs[i].t_anchor >= FP_MAX_TIME) {
            free(packed);
            return -1;
        }
        packed[i] = fp_entry_pack(pairs[i].hash, track, (uint32_t)pairs[i].t_anchor);
    }
    int rc = idx->vt->add_packed(idx->self, packed, n);
    free(packed);
    return rc;
}

int fp_index_finalize(fp_index *idx)
{
    return idx->vt->finalize(idx->self);
}

int fp_index_query(const fp_index *idx, const fp_pair *q, size_t n, fp_votes *out)
{
    memset(out, 0, sizeof *out);
    return idx->vt->query(idx->self, &idx->cfg, q, n, out);
}

size_t fp_index_bytes(const fp_index *idx)
{
    return idx->vt->bytes(idx->self) + (size_t)idx->tracks_cap * sizeof *idx->track_frames;
}

size_t fp_index_num_entries(const fp_index *idx)
{
    return idx->vt->num_entries(idx->self);
}

uint32_t fp_index_num_tracks(const fp_index *idx)
{
    return idx->num_tracks;
}

void fp_votes_free(fp_votes *v)
{
    free(v->v);
    memset(v, 0, sizeof *v);
}

int fp_index_hash_stats(const fp_index *idx, fp_hash_stats *out)
{
    memset(out, 0, sizeof *out);
    const size_t n = fp_index_num_entries(idx);
    uint64_t *e = malloc((n ? n : 1) * sizeof *e);
    if (!e || idx->vt->export_sorted(idx->self, e) != 0) {
        free(e);
        return -1;
    }
    double sum_sq = 0.0;
    size_t i = 0;
    while (i < n) {
        const uint64_t h = e[i] >> 40;
        size_t j = i + 1;
        while (j < n && e[j] >> 40 == h)
            j++;
        const size_t len = j - i;
        int f1, f2, dt;
        fp_hash_unpack((uint32_t)h, &f1, &f2, &dt);
        out->distinct++;
        if (len > out->max_list)
            out->max_list = len;
        sum_sq += (double)len * (double)len;
        out->dt_hist[dt] += len;
        if (f1 == f2)
            out->same_f += len;
        i = j;
    }
    out->entries = n;
    out->mean_list = out->distinct ? (double)n / (double)out->distinct : 0.0;
    out->weighted_list = n ? sum_sq / (double)n : 0.0;
    free(e);
    return 0;
}
