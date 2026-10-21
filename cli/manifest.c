#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *join_dir(const char *manifest_path, const char *rel)
{
    if (rel[0] == '/')
        return strdup(rel);
    const char *slash = strrchr(manifest_path, '/');
    const size_t dir = slash ? (size_t)(slash - manifest_path) + 1 : 0;
    char *out = malloc(dir + strlen(rel) + 1);
    if (!out)
        return NULL;
    memcpy(out, manifest_path, dir);
    strcpy(out + dir, rel);
    return out;
}

int manifest_read(const char *path, manifest *m)
{
    m->rows = NULL;
    m->n = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t cap = 0;
    char line[4096];
    int rc = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#' || !strncmp(line, "id\t", 3))
            continue;
        char *id_s = line;
        char *path_s = strchr(id_s, '\t');
        char *split_s = path_s ? strchr(path_s + 1, '\t') : NULL;
        if (!split_s) {
            rc = -1;
            break;
        }
        *path_s++ = '\0';
        *split_s++ = '\0';
        char *end;
        unsigned long id = strtoul(id_s, &end, 10);
        int holdout;
        if (*end || end == id_s)
            rc = -1;
        else if (!strcmp(split_s, "holdout"))
            holdout = 1;
        else if (!strcmp(split_s, "indexed"))
            holdout = 0;
        else
            rc = -1;
        if (rc)
            break;
        if (m->n == cap) {
            cap = cap ? cap * 2 : 64;
            manifest_row *nr = realloc(m->rows, cap * sizeof *nr);
            if (!nr) {
                rc = -1;
                break;
            }
            m->rows = nr;
        }
        char *full = join_dir(path, path_s);
        if (!full) {
            rc = -1;
            break;
        }
        m->rows[m->n++] = (manifest_row){(uint32_t)id, full, holdout};
    }
    fclose(f);
    if (rc)
        manifest_free(m);
    return rc;
}

void manifest_free(manifest *m)
{
    for (size_t i = 0; i < m->n; i++)
        free(m->rows[i].path);
    free(m->rows);
    m->rows = NULL;
    m->n = 0;
}
