#include <string.h>

#include "match.h"

static void insert_top(fp_candidate top[3], fp_candidate c)
{
    int pos = 3;
    while (pos > 0 && top[pos - 1].score < c.score)
        pos--;
    if (pos == 3)
        return;
    for (int k = 2; k > pos; k--)
        top[k] = top[k - 1];
    top[pos] = c;
}

void fp_match_decide(const fp_config *cfg, const fp_votes *votes, fp_match_result *out)
{
    memset(out, 0, sizeof *out);
    for (int k = 0; k < 3; k++) {
        out->top[k].track = FP_NO_TRACK;
        out->top[k].offset = 0;
        out->top[k].score = 0;
    }
    out->track = FP_NO_TRACK;
    out->hashes_used = votes->hashes_used;
    out->entries_scanned = votes->entries_scanned;

    /* Votes arrive sorted by (track, bin), so adjacent bins are neighbours in the array when both are present.
     * A window starting at an absent bin never beats the one starting at its present neighbour. Windows are
     * visited in ascending order and only a strict > replaces the best, so ties keep the lowest offset; the
     * reported bin is non-decreasing in the window start. Tracks are visited in ascending id, which makes
     * insert_top break score ties by track id. */
    size_t i = 0;
    while (i < votes->n) {
        const uint32_t track = votes->v[i].track;
        fp_candidate best = {track, 0, 0};
        for (; i < votes->n && votes->v[i].track == track; i++) {
            const fp_vote *v = &votes->v[i];
            uint32_t score = v->count;
            int32_t bin = v->bin;
            if (cfg->score_window == 2 && i + 1 < votes->n && votes->v[i + 1].track == track &&
                (int64_t)votes->v[i + 1].bin - v->bin == 1) {
                score += votes->v[i + 1].count;
                if (votes->v[i + 1].count > v->count)
                    bin = votes->v[i + 1].bin;
            }
            if (score > best.score) {
                best.score = score;
                best.offset = bin * cfg->offset_bin;
            }
        }
        insert_top(out->top, best);
    }

    if (out->top[0].track == FP_NO_TRACK)
        return;
    out->track = out->top[0].track;
    out->offset = out->top[0].offset;
    out->score = out->top[0].score;
    out->runner_up = out->top[1].score;
    out->matched = out->score >= (uint32_t)cfg->min_score &&
                   (float)out->score >= cfg->min_ratio * (float)out->runner_up;
}

int fp_match_equal(const fp_match_result *a, const fp_match_result *b)
{
    if (a->matched != b->matched || a->track != b->track || a->offset != b->offset || a->score != b->score ||
        a->runner_up != b->runner_up || a->hashes_used != b->hashes_used ||
        a->entries_scanned != b->entries_scanned)
        return 0;
    for (int k = 0; k < 3; k++)
        if (a->top[k].track != b->top[k].track || a->top[k].offset != b->top[k].offset ||
            a->top[k].score != b->top[k].score)
            return 0;
    return 1;
}
