#include <stdlib.h>
#include <string.h>

#include "index_internal.h"

#define JUMP_BITS 16
#define JUMP_SIZE ((1u << JUMP_BITS) + 1)

typedef struct {
    uint64_t *entries;
    size_t count;
    size_t cap;
    uint32_t *jump;
} modern;

static void *modern_create(const fp_config *cfg)
{
    (void)cfg;
    return calloc(1, sizeof(modern));
}

static void modern_destroy(void *self)
{
    modern *m = self;
    free(m->entries);
    free(m->jump);
    free(m);
}

static int modern_add_packed(void *self, const uint64_t *entries, size_t n)
{
    modern *m = self;
    if (m->count + n > m->cap) {
        size_t cap = m->cap ? m->cap : 1024;
        while (cap < m->count + n)
            cap *= 2;
        uint64_t *grown = realloc(m->entries, cap * sizeof *grown);
        if (!grown)
            return -1;
        m->entries = grown;
        m->cap = cap;
    }
    memcpy(m->entries + m->count, entries, n * sizeof *entries);
    m->count += n;
    free(m->jump);
    m->jump = NULL;
    return 0;
}

static int modern_finalize(void *self)
{
    modern *m = self;
    qsort(m->entries, m->count, sizeof *m->entries, fp_cmp_u64);
    free(m->jump);
    m->jump = calloc(JUMP_SIZE, sizeof *m->jump);
    if (!m->jump)
        return -1;

    /* jump[p] = first entry whose top 16 hash bits are >= p */
    for (size_t i = 0; i < m->count; i++)
        m->jump[(m->entries[i] >> 48) + 1]++;
    for (uint32_t p = 1; p < JUMP_SIZE; p++)
        m->jump[p] += m->jump[p - 1];
    return 0;
}

static int cmp_key(const void *a, const void *b)
{
    return fp_cmp_u64(a, b);
}

static int modern_query(const void *self, const fp_config *cfg, const fp_pair *q, size_t n, fp_votes *out)
{
    const modern *m = self;
    if (!m->jump && m->count)
        return -1;

    size_t cap = 1024;
    size_t nk = 0;
    uint64_t *keys = malloc(cap * sizeof *keys);
    if (!keys)
        return -1;

    for (size_t i = 0; i < n; i++) {
        const uint32_t h = q[i].hash;
        const uint32_t p = h >> 8;
        size_t lo = m->jump[p];
        size_t hi = m->jump[p + 1];
        const uint64_t target = (uint64_t)h << 40;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (m->entries[mid] < target)
                lo = mid + 1;
            else
                hi = mid;
        }
        int used = 0;
        for (size_t k = lo; k < m->count && (m->entries[k] >> 40) == h; k++) {
            uint32_t eh, track, time;
            fp_entry_unpack(m->entries[k], &eh, &track, &time);
            used = 1;
            out->entries_scanned++;
            if (nk == cap) {
                cap *= 2;
                uint64_t *grown = realloc(keys, cap * sizeof *keys);
                if (!grown) {
                    free(keys);
                    return -1;
                }
                keys = grown;
            }
            const int32_t bin = fp_floor_div((int32_t)time - q[i].t_anchor, cfg->offset_bin);
            keys[nk++] = ((uint64_t)track << 32) | (uint32_t)((int64_t)bin + 2147483648LL);
        }
        out->hashes_used += (uint32_t)used;
    }

    qsort(keys, nk, sizeof *keys, cmp_key);
    out->v = malloc((nk ? nk : 1) * sizeof *out->v);
    if (!out->v) {
        free(keys);
        return -1;
    }
    for (size_t i = 0; i < nk;) {
        size_t j = i;
        while (j < nk && keys[j] == keys[i])
            j++;
        out->v[out->n].track = (uint32_t)(keys[i] >> 32);
        out->v[out->n].bin = (int32_t)((int64_t)(uint32_t)keys[i] - 2147483648LL);
        out->v[out->n].count = (uint32_t)(j - i);
        out->n++;
        i = j;
    }
    free(keys);
    return 0;
}

static size_t modern_bytes(const void *self)
{
    const modern *m = self;
    return m->count * sizeof(uint64_t) + JUMP_SIZE * sizeof(uint32_t);
}

static size_t modern_num_entries(const void *self)
{
    return ((const modern *)self)->count;
}

static int modern_export_sorted(const void *self, uint64_t *out)
{
    const modern *m = self;
    memcpy(out, m->entries, m->count * sizeof *out);
    qsort(out, m->count, sizeof *out, fp_cmp_u64);
    return 0;
}

const fp_index_vt fp_vt_modern = {
    modern_create, modern_destroy, modern_add_packed, modern_finalize,
    modern_query,  modern_bytes,   modern_num_entries, modern_export_sorted,
};
