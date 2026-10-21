#ifndef FP_MATCH_H
#define FP_MATCH_H

#include <stdint.h>

#include "fp_config.h"
#include "index.h"

#define FP_NO_TRACK UINT32_MAX

typedef struct {
    uint32_t track;
    int32_t offset;
    uint32_t score;
} fp_candidate;

/* offset is bin * offset_bin, in frames. Empty slots have track FP_NO_TRACK and score 0. */
typedef struct {
    int matched;
    uint32_t track;
    int32_t offset;
    uint32_t score;
    uint32_t runner_up;
    fp_candidate top[3];
    uint32_t hashes_used;
    uint64_t entries_scanned;
} fp_match_result;

void fp_match_decide(const fp_config *cfg, const fp_votes *votes, fp_match_result *out);
int fp_match_equal(const fp_match_result *a, const fp_match_result *b);

#endif
