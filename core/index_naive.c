#include <stdlib.h>
#include <string.h>

#include "index_internal.h"

#define BUCKETS (1u << 20)
#define VOTE_BUCKETS (1u << 14)

typedef struct node {
    uint32_t hash;
    uint32_t track;
    uint32_t time;
    struct node *next;
} node;

typedef struct vnode {
    uint32_t track;
    int32_t bin;
    uint32_t count;
    struct vnode *next;
} vnode;

typedef struct {
    node **buckets;
    size_t count;
} naive;

static void *naive_create(const fp_config *cfg)
{
    (void)cfg;
    naive *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->buckets = calloc(BUCKETS, sizeof *s->buckets);
    if (!s->buckets) {
        free(s);
        return NULL;
    }
    return s;
}

static void naive_destroy(void *self)
{
    naive *s = self;
    for (size_t b = 0; b < BUCKETS; b++)
        for (node *n = s->buckets[b]; n;) {
            node *next = n->next;
            free(n);
            n = next;
        }
    free(s->buckets);
    free(s);
}

static int naive_add_packed(void *self, const uint64_t *entries, size_t n)
{
    naive *s = self;
    for (size_t i = 0; i < n; i++) {
        node *nd = malloc(sizeof *nd);
        if (!nd)
            return -1;
        fp_entry_unpack(entries[i], &nd->hash, &nd->track, &nd->time);
        node **head = &s->buckets[nd->hash & (BUCKETS - 1)];
        nd->next = *head;
        *head = nd;
        s->count++;
    }
    return 0;
}

static int naive_finalize(void *self)
{
    (void)self;
    return 0;
}

static void vote_map_free(vnode **map)
{
    for (size_t b = 0; b < VOTE_BUCKETS; b++)
        for (vnode *v = map[b]; v;) {
            vnode *next = v->next;
            free(v);
            v = next;
        }
    free(map);
}

static int naive_query(const void *self, const fp_config *cfg, const fp_pair *q, size_t n, fp_votes *out)
{
    const naive *s = self;
    vnode **map = calloc(VOTE_BUCKETS, sizeof *map);
    if (!map)
        return -1;
    size_t keys = 0;

    for (size_t i = 0; i < n; i++) {
        const uint32_t h = q[i].hash;
        int used = 0;
        for (const node *nd = s->buckets[h & (BUCKETS - 1)]; nd; nd = nd->next) {
            if (nd->hash != h)
                continue;
            used = 1;
            out->entries_scanned++;
            const int32_t bin = fp_floor_div((int32_t)nd->time - q[i].t_anchor, cfg->offset_bin);
            const uint32_t slot = ((nd->track * 2654435761u) ^ ((uint32_t)bin * 40503u)) & (VOTE_BUCKETS - 1);
            vnode *v = map[slot];
            while (v && !(v->track == nd->track && v->bin == bin))
                v = v->next;
            if (!v) {
                v = malloc(sizeof *v);
                if (!v) {
                    vote_map_free(map);
                    return -1;
                }
                v->track = nd->track;
                v->bin = bin;
                v->count = 0;
                v->next = map[slot];
                map[slot] = v;
                keys++;
            }
            v->count++;
        }
        out->hashes_used += (uint32_t)used;
    }

    out->v = malloc((keys ? keys : 1) * sizeof *out->v);
    if (!out->v) {
        vote_map_free(map);
        return -1;
    }
    for (size_t b = 0; b < VOTE_BUCKETS; b++)
        for (const vnode *v = map[b]; v; v = v->next) {
            out->v[out->n].track = v->track;
            out->v[out->n].bin = v->bin;
            out->v[out->n].count = v->count;
            out->n++;
        }
    vote_map_free(map);
    qsort(out->v, out->n, sizeof *out->v, fp_cmp_vote);
    return 0;
}

static size_t naive_bytes(const void *self)
{
    const naive *s = self;
    return BUCKETS * sizeof(node *) + s->count * sizeof(node);
}

static size_t naive_num_entries(const void *self)
{
    return ((const naive *)self)->count;
}

static int naive_export_sorted(const void *self, uint64_t *out)
{
    const naive *s = self;
    size_t k = 0;
    for (size_t b = 0; b < BUCKETS; b++)
        for (const node *nd = s->buckets[b]; nd; nd = nd->next)
            out[k++] = fp_entry_pack(nd->hash, nd->track, nd->time);
    qsort(out, k, sizeof *out, fp_cmp_u64);
    return 0;
}

const fp_index_vt fp_vt_naive = {
    naive_create, naive_destroy, naive_add_packed, naive_finalize,
    naive_query,  naive_bytes,   naive_num_entries, naive_export_sorted,
};
