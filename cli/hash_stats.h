#ifndef HASH_STATS_H
#define HASH_STATS_H

#include <stdio.h>

#include "index.h"

static inline void hash_stats_print(FILE *out, const fp_hash_stats *s)
{
    static const struct {
        int lo, hi;
    } buckets[] = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 10}, {11, 20}, {21, 63}};
    const double n = s->entries ? (double)s->entries : 1.0;
    fprintf(out, "distinct hashes: %zu\n", s->distinct);
    fprintf(out, "posting list:    mean %.1f, entry-weighted %.1f, max %zu\n", s->mean_list, s->weighted_list,
            s->max_list);
    fprintf(out, "f1 == f2:        %.1f%%\n", 100.0 * (double)s->same_f / n);
    fprintf(out, "dt histogram:   ");
    size_t le3 = s->dt_hist[1] + s->dt_hist[2] + s->dt_hist[3];
    for (size_t b = 0; b < sizeof buckets / sizeof *buckets; b++) {
        size_t c = 0;
        for (int d = buckets[b].lo; d <= buckets[b].hi; d++)
            c += s->dt_hist[d];
        if (buckets[b].lo == buckets[b].hi)
            fprintf(out, " %d:%.1f%%", buckets[b].lo, 100.0 * (double)c / n);
        else
            fprintf(out, " %d-%d:%.1f%%", buckets[b].lo, buckets[b].hi, 100.0 * (double)c / n);
    }
    fprintf(out, "  (dt<=3: %.1f%%)\n", 100.0 * (double)le3 / n);
}

#endif
