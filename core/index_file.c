#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index_internal.h"

#define MAGIC "WFP1"
#define VERSION 1u
#define CFG_FIELDS 12

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

static uint32_t f32_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* Only fields that change which hashes a clip produces; scoring fields act at query time. */
static void config_snapshot(const fp_config *c, uint32_t out[CFG_FIELDS])
{
    out[0] = (uint32_t)c->sample_rate;
    out[1] = (uint32_t)c->window_size;
    out[2] = (uint32_t)c->hop_size;
    out[3] = (uint32_t)c->num_bands;
    out[4] = f32_bits(c->band_lo_hz);
    out[5] = f32_bits(c->band_hi_hz);
    out[6] = f32_bits(c->threshold);
    out[7] = f32_bits(c->min_magnitude);
    out[8] = (uint32_t)c->fan_out;
    out[9] = (uint32_t)c->min_dt;
    out[10] = (uint32_t)c->max_dt;
    out[11] = (uint32_t)c->max_df;
}

static int write_all(FILE *f, const uint8_t *buf, size_t n)
{
    return fwrite(buf, 1, n, f) == n ? 0 : -1;
}

int fp_index_save(const fp_index *idx, const char *path)
{
    const size_t n = idx->vt->num_entries(idx->self);
    uint64_t *entries = malloc((n ? n : 1) * sizeof *entries);
    if (!entries)
        return -1;
    idx->vt->export_sorted(idx->self, entries);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(entries);
        return -1;
    }

    int rc = 0;
    uint8_t buf[8192];
    uint32_t snap[CFG_FIELDS];
    config_snapshot(&idx->cfg, snap);

    memcpy(buf, MAGIC, 4);
    put32(buf + 4, VERSION);
    for (int i = 0; i < CFG_FIELDS; i++)
        put32(buf + 8 + 4 * i, snap[i]);
    put32(buf + 8 + 4 * CFG_FIELDS, idx->num_tracks);
    rc |= write_all(f, buf, 12 + 4 * CFG_FIELDS);

    for (uint32_t t = 0; t < idx->num_tracks; t++) {
        put32(buf, idx->track_frames[t]);
        rc |= write_all(f, buf, 4);
    }

    put64(buf, (uint64_t)n);
    rc |= write_all(f, buf, 8);
    for (size_t i = 0; i < n;) {
        size_t chunk = n - i < 1024 ? n - i : 1024;
        for (size_t k = 0; k < chunk; k++)
            put64(buf + 8 * k, entries[i + k]);
        rc |= write_all(f, buf, chunk * 8);
        i += chunk;
    }

    free(entries);
    if (fclose(f) != 0)
        rc = -1;
    return rc ? -1 : 0;
}

static int read_all(FILE *f, uint8_t *buf, size_t n)
{
    return fread(buf, 1, n, f) == n ? 0 : -1;
}

int fp_index_load(fp_index **out, fp_impl impl, const fp_config *expect, const char *path)
{
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    int rc = -1;
    fp_index *idx = NULL;
    uint8_t buf[8192];
    uint32_t want[CFG_FIELDS];
    config_snapshot(expect, want);

    if (read_all(f, buf, 12 + 4 * CFG_FIELDS) != 0 || memcmp(buf, MAGIC, 4) != 0 || get32(buf + 4) != VERSION)
        goto done;
    for (int i = 0; i < CFG_FIELDS; i++)
        if (get32(buf + 8 + 4 * i) != want[i]) {
            rc = -2;
            goto done;
        }
    const uint32_t num_tracks = get32(buf + 8 + 4 * CFG_FIELDS);
    if (num_tracks > FP_MAX_TRACKS)
        goto done;

    idx = fp_index_create(impl, expect);
    if (!idx)
        goto done;
    for (uint32_t t = 0; t < num_tracks; t++) {
        if (read_all(f, buf, 4) != 0 || fp_index_set_track(idx, t, get32(buf)) != 0)
            goto done;
    }

    if (read_all(f, buf, 8) != 0)
        goto done;
    const uint64_t n = get64(buf);
    uint64_t chunk_entries[1024];
    for (uint64_t i = 0; i < n;) {
        size_t chunk = n - i < 1024 ? (size_t)(n - i) : 1024;
        if (read_all(f, buf, chunk * 8) != 0)
            goto done;
        for (size_t k = 0; k < chunk; k++)
            chunk_entries[k] = get64(buf + 8 * k);
        if (fp_index_add_packed(idx, chunk_entries, chunk) != 0)
            goto done;
        i += chunk;
    }
    if (fp_index_finalize(idx) != 0)
        goto done;
    *out = idx;
    idx = NULL;
    rc = 0;

done:
    fp_index_free(idx);
    fclose(f);
    return rc;
}
