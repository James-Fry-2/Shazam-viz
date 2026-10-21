#ifndef FP_MANIFEST_H
#define FP_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    char *path;
    int holdout;
} manifest_row;

typedef struct {
    manifest_row *rows;
    size_t n;
} manifest;

/*
 * Reads a TSV of id<TAB>path<TAB>split, split being "indexed" or "holdout". A leading
 * "id" header line is skipped. Relative paths are resolved against the manifest's directory.
 * Returns 0 on success, -1 on I/O or format error.
 */
int manifest_read(const char *path, manifest *m);
void manifest_free(manifest *m);

#endif
