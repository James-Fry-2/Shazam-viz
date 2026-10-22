# Progress

## Phase 1 (complete): steps 1.1–1.8 done

### 1.1 Config, FFT, fingerprint
Radix-2 FFT, spectrogram, per-band peak picking, pair hashing, seeded RNG. Self-test covers the impulse spectrum, a 1 kHz sine peak at bin 128 and the hash round trip.

### 1.2 Index backends, file format, matching
`naive2003` (chained hash tables, one malloc per node) and `modern` (sorted array with a 16-bit prefix jump table) behind one vtable. `index.bin` stores a config snapshot and refuses a mismatched query config. `fp_match_decide` is shared, so backend equivalence reduces to equal vote counts.

### 1.3 Channel simulation, resampler, I/O
- Speaker filter: 100 Hz is attenuated by 23.3 dB, 1 kHz is within 0.1 dB of unity. The unity-gain check is on the linear filter stage; the tanh stage adds about +6.3 dB to small signals.
- Pink-noise SNR is exact to better than 0.001 dB at 30, 20, 10, 5 and 0 dB.
- Echo, Kaiser-windowed-sinc resampler, WAV read/write and TSV manifest are covered by the self-test.
- `build/libcli.a` holds the WAV and manifest helpers and is linked into every CLI tool.

### 1.4 Synthetic generator
`data/make_synthetic.sh` writes 250 tracks (200 indexed, 50 holdout) in about 3.2 s wall clock using parallel jobs. Track n uses seed n+1. Sections repeat (verse, chorus, verse, chorus).

### 1.5 CLI tools
- `fp-index` on the 200 indexed tracks: 2,475,723 entries, 19.8 MB, 0.88 s.
- `fp-query --impl both`: a 10 s excerpt of track 42 from 7.3 s matches track 42 at offset 114 frames (score 3842, runner-up 541). Holdout tracks report no match. The backends agree in every case.


### 1.6 Accuracy test and density tuning
`cli/fp-accuracy` runs the seeded grid in memory on both backends. It compares the full vote arrays and the match results on every query, and aborts on any difference. `bench/accuracy_phase1.sh [grid|detune|sweep]` regenerates everything in `docs/results/`. Output is identical across thread counts.

**Method.** Clip start `s` is drawn with `s % hop != 0`. The true offset is `r = s/hop`. A hit means the right track and a winning bin within ±1 of `floor(r)`. Hit offsets come out as `round(r)` with error inside ±0.5, with no bias. For survival, the clean reference is the clip itself, fingerprinted alone. The degraded clip runs speaker and echo over a 0.5 s pre-roll, which is then cut off, and adds noise afterwards, so both clips share the same samples and frame grid. The speaker channel's cross-correlation lag is 1 sample. Each clip's echo and noise sequence is fixed across SNRs, which are only rescaled.

**Defaults** (see `docs/parameters.md`): threshold 4 → 6, min_dt 1 → 5, fan_out 5, and `min_magnitude` 1e-3 → `min_magnitude_db` −80 dB re full-scale sine. Index format version 2.

**Hash diversity** (now printed by `fp-index`):

| | old (th 4, dt 1) | new (th 6, dt 5) |
|---|---|---|
| entries | 2,475,723 | 1,796,338 |
| distinct hashes | 43,667 | 50,294 |
| posting list mean / entry-weighted / max | 56.7 / 1,721 / 16,584 | 35.7 / 692 / 5,398 |
| f1 == f2 | 19.0% | 13.6% |
| dt ≤ 3 | 95.4% | 0% |
| entries scanned, 10 s clean query | 6.98M | 1.97M |
| pooled noisy recall (reduced grid) | 0.737 | 0.787 |

Root cause: peaks use only 60–88 effective f bins (half of them in 14–25 bins), and the next five peaks almost always sat in the next frame.

**Full grid** (`accuracy_phase1.csv`, 3 densities × {3, 5, 10 s} × 4 channels × 6 SNRs, 100 + 50 clips, 32,400 queries × 2 backends, all agree):
- Clean 10 s recall: 0.93 / 0.98 / 0.99 at 13.8 / 33.3 / 60.6 peaks/s. Track-only recall is 1.00 at all three.
- Holdout FP: 22 / 1 / 6 of 3,600 holdout queries (low / mid / high), max 0.04 in one cell.
- Entries scanned per 10 s clean query: 73k / 627k / 1.97M.
- Speaker+echo, 10 s, ∞ → 0 dB: high 0.98 → 0.82, mid 0.86 → 0.14, low 0.60 → 0.01.

**Flags for review**
- *Wrong offset at a repeated section.* **Resolved in step 1.8 (mechanism fixed by `score_window` 2, default stays 1; the 37/404 figure did not reproduce).** All wrong-offset wins are ±11–14 s away, at the repeated verse or chorus. Single-bin scoring splits the true votes when frac(s/hop) ≈ 0.5: 37 of 404 such clips lost to the repeat, against 1 of 396 otherwise. This makes up all of the gap between recall and track recall on clean clips.
- *Peak test vs noise and band width.* The ratio test loses peaks quickly under broadband noise: at threshold 16, 10 dB SNR keeps 10% of peaks. A band of N bins can never pass threshold ≥ N, so the 11-bin lowest band is silent at threshold ≥ 11.
- *Cross-track false positives come from sustained notes.* Two diagnosed cases: 46 of 48 (detuned set, threshold 16) and 33 of 34 (original set, threshold 8, min_dt 8, clean version of the clip) winning votes are f1 == f2 pairs from one held pitch present in both tracks. Candidate fix (not made): drop or down-weight f1 == f2 pairs.
- *Decision rule.* `min_score` 5 accepts noise matches of 6 vs 3 on clips with only a few dozen pairs. `min_ratio × runner_up` depends on the background vote level, so it shifts with posting-list length.
- *Hash survival vs p².* **Updated in step 1.8: the exact-bin prediction closes most of the speaker/echo gap (measured / exact² 0.96–0.99 at 20 dB and above).** Under noise, measured ≥ p² (1.0–1.3× at the default density, 2.5× at low density at 10 dB), because survival is correlated across peaks and no pairs re-form. Under speaker/echo, measured is 5–18% below p²: 6–11% of pairs whose two endpoints survive still change hash (fan-out slots taken by added peaks), and peak survival allows |Δf| ≤ 1 while hashes need exact f.
- *Monotonicity.* 13 violations in 216 rows. Most are 0.01. The largest is 0.03, where track recall also rises (echo, 10 s, mid: 0.90 at ∞, 0.93 at 30 dB), so light noise genuinely helps a few clips. Mechanism unverified.
- *min_magnitude.* The self-test's 148 "leakage" peaks came from float phase error in the test sine. Real Hann leakage peaks at −92 dB for a full-scale off-bin sine. The floor removes 0.0 peaks/s of music under the speaker channel at −80 dB.
- *Detune proposal* (`DETUNE=1 data/make_synthetic.sh`, off by default, `accuracy_phase1_detune.csv`). Distinct hashes 50k → 117k, entries scanned at the default 1.97M → 0.61M, speaker+echo 10 s @ 5 dB recall 0.91 → 0.98, **but holdout FPs rise** (6 → 20, 1 → 74, 22 → 113 of 3,600). Shorter posting lists lower the runner-up, so sustained-note pile-ups pass the ratio test. Not adopted. For review.

### Open points
- Subjective listening check of the synthetic tracks (1.4 "Done when") is still to be done by ear.

### 1.7 Docs
- `docs/parameters.md` now has an entry (default, unit, reason) for every `fp_config` field. `max_df`, the band edges, `max_dt`, `min_score` and `min_ratio` were not swept in step 1.6, and the file says so.
- `docs/results-phase1.md` holds six summary tables generated from `docs/results/*.csv` with the awk command shown above each table, plus the step 1.6 flags copied unchanged.
- Reproducibility: `make` is clean under `-Werror`, `build/fp-selftest` passes, and `sh bench/accuracy_phase1.sh grid` regenerates `docs/results/accuracy_phase1.csv` byte for byte (`git diff --exit-code` is clean). Only the wall-clock timing lines in the `.txt` log differ between runs.
- `.gitignore` covers `build/`, `data/audio/` and `data/audio-detune/`, and `git ls-files '*.wav'` prints nothing.

The decisions listed under "Flags for review" above, except the two marked resolved or updated in step 1.8, are unresolved and need an answer before phase 2.

## Step 1.8: two-bin offset scoring and exact-bin survival metric

**Part A, `score_window`** (`core/fp_config.h`, `core/match.c`, `--score-window 1|2`). 1 scores each track by its best bin, 2 by its best sum of bins b and b+1 (adjacent by value). The reported bin is the one with more votes, the lower on a tie; equal windows keep the lowest offset; `runner_up` and `top[3]` use the windowed scores. Scoring-only: the index snapshot and version are unchanged, and a self-test loads a window-1 index with a window-2 config. Self-tests cover split votes, non-adjacent bins, negative bins, tie-breaks under window 2, `offset_bin` 2, and windowed runner-up/top[3].

**Part B, exact survival.** `peak_survival_exact` (same frame, same f) and `pred_hash_survival_exact` (its square) are appended as CSV columns 25 and 26. Columns 1–24 of the regenerated grid are byte-identical to the committed step 1.6 CSV, so the run is deterministic and Part B changed nothing else. At the default density, 10 s, speaker+echo, SNR ∞: peak 0.618, p² 0.382, exact 0.568, exact² 0.323, measured hash survival 0.312. Measured / p² is 0.82 and measured / exact² is 0.97, so most of the speaker/echo shortfall flagged in 1.6 was the ±1 bin tolerance. Under noise, measured exceeds exact² by 1.02–1.46× from 10 dB down.

**Part C, results** (`accuracy_phase1_window1.csv`, `accuracy_phase1_window2.csv`, 32,400 queries each, both backends agree on every query; tables in `docs/results-phase1.md`):
- Clean 10 s recall, window 1 → 2: 0.93 → 0.99 (low), 0.98 → 1.00 (mid), 0.99 → 1.00 (high). Wrong-offset rate (mean over 72 cells) roughly halves at every density.
- Holdout FPs of 3,600: 22 → 13, 1 → 1, 6 → 6. They do not rise.
- **Recall under noise falls.** Mean track recall over 72 cells: 0.356 → 0.318 (low), 0.589 → 0.547 (mid), 0.790 → 0.745 (high). Clean track recall at low density: 0.88 → 0.83 (3 s), 0.96 → 0.91 (5 s). Speaker+echo 10 s at 5 dB: 0.91 → 0.88 (high), 0.61 → 0.46 (mid). Of the queries whose track hit changed, 1,005 were lost and 102 gained. Cause: for queries that match under both windows the runner-up grows about 1.85× from the second bin while the winner grows less, so the unchanged `min_ratio` 2.0 rejects more true matches (the lost ones were rejected, not outvoted).
- **Default stays `score_window` 1.** The adoption rule was fixed beforehand: no loss of recall, no noticeable rise in holdout FPs. FPs pass, recall fails.
- A diagnostic run (mid density, 5/10 s, clean and speaker+echo, SNR ∞/10/5/0, not committed) shows the coupling: mean recall / holdout FPs of 800 is 0.489 / 0 (window 1), 0.444 / 0 (window 2, ratio 2.0), 0.570 / 18 (ratio 1.5), 0.635 / 157 (ratio 1.2). Choosing window and `min_ratio` together is the decision-rule flag; not done here.
- Monotonicity violations: 13 → 8.

**Split-vote check.** Slice used for 404/396: 5 s and 10 s clips, SNR ∞, four channels, per density. Wrong-offset wins with frac(s/hop) in (0.25, 0.75) vs outside, window 1 → 2: low 48/404 vs 11/396 → 17/404 vs 7/396; mid 22/404 vs 11/396 → 7/404 vs 5/396; high 7/404 vs 8/396 → 2/404 vs 8/396. **The 37/404 vs 1/396 from 1.6 does not reproduce**, at any density, at window 1; the outside-the-range rate is 8–11 of 396, not 1. The earlier configuration behind those numbers was not recorded, so I cannot say why. At the default density there is no split-vote signature at window 1 (7 vs 8), so the fix helps mainly at low and mid density. Remaining wrong-offset wins (clean, SNR ∞, window 2: 14 of 900 clips) are 12–24 s from the truth; all outscore the true offset's windowed votes (mean 1.22×, 7 within 10%). They are not ties. Whether the generator's repeated sections are near-identical was not checked. Mid-zone and outside counts come from `--detail` runs in scratch (not committed); the detail CSV gained a final `true_window_score` column for this.

**Housekeeping.** `bench/accuracy_phase1.sh` has `window1` and `window2` modes; `detune` and `sweep` pin `--score-window 1` but were not re-run, so their CSVs still have 24 columns. `median_vote_ratio` uses raw single-bin counts and is the same at both windows. `PLAN.md`: the earlier "1.8 Lessons from real recordings" is now 1.9. `CLAUDE.md` scoring invariant updated.
