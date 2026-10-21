#ifndef FP_CONFIG_ARGS_H
#define FP_CONFIG_ARGS_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fp_config.h"

/*
 * Consumes a fingerprint config flag and its value at argv[*i], advancing *i past the value.
 * Returns 1 if consumed, 0 if the flag is not a config flag, -1 if the value is missing.
 */
static inline int config_arg(fp_config *c, int argc, char **argv, int *i)
{
    static const struct {
        const char *flag;
        int is_float;
        size_t off;
    } opts[] = {
        {"--bands", 0, offsetof(fp_config, num_bands)},
        {"--threshold", 1, offsetof(fp_config, threshold)},
        {"--min-magnitude", 1, offsetof(fp_config, min_magnitude)},
        {"--fan-out", 0, offsetof(fp_config, fan_out)},
        {"--min-dt", 0, offsetof(fp_config, min_dt)},
        {"--max-dt", 0, offsetof(fp_config, max_dt)},
        {"--max-df", 0, offsetof(fp_config, max_df)},
        {"--min-score", 0, offsetof(fp_config, min_score)},
        {"--min-ratio", 1, offsetof(fp_config, min_ratio)},
    };
    for (size_t k = 0; k < sizeof opts / sizeof *opts; k++) {
        if (strcmp(argv[*i], opts[k].flag))
            continue;
        if (*i + 1 >= argc)
            return -1;
        void *dst = (char *)c + opts[k].off;
        if (opts[k].is_float)
            *(float *)dst = strtof(argv[*i + 1], NULL);
        else
            *(int *)dst = atoi(argv[*i + 1]);
        *i += 2;
        return 1;
    }
    return 0;
}

#define CONFIG_ARGS_USAGE \
    "  config: --bands n --threshold x --min-magnitude x --fan-out n --min-dt n --max-dt n --max-df n\n" \
    "          --min-score n --min-ratio x   (an index and its queries must share the fingerprint fields)\n"

#endif
