# Progress

## Phase 1 (in progress): steps 1.1–1.6 done, 1.7 remaining

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
- *Wrong offset at a repeated section.* All wrong-offset wins are ±11–14 s away, at the repeated verse or chorus. Single-bin scoring splits the true votes when frac(s/hop) ≈ 0.5: 37 of 404 such clips lost to the repeat, against 1 of 396 otherwise. This makes up all of the gap between recall and track recall on clean clips. Candidate fix (not made, scoring invariant): score adjacent-bin pairs, or offset_bin 2.
- *Peak test vs noise and band width.* The ratio test loses peaks quickly under broadband noise: at threshold 16, 10 dB SNR keeps 10% of peaks. A band of N bins can never pass threshold ≥ N, so the 11-bin lowest band is silent at threshold ≥ 11.
- *Cross-track false positives come from sustained notes.* Two diagnosed cases: 46 of 48 (detuned set, threshold 16) and 33 of 34 (original set, threshold 8, min_dt 8, clean version of the clip) winning votes are f1 == f2 pairs from one held pitch present in both tracks. Candidate fix (not made): drop or down-weight f1 == f2 pairs.
- *Decision rule.* `min_score` 5 accepts noise matches of 6 vs 3 on clips with only a few dozen pairs. `min_ratio × runner_up` depends on the background vote level, so it shifts with posting-list length.
- *Hash survival vs p².* Under noise, measured ≥ p² (1.0–1.3× at the default density, 2.5× at low density at 10 dB), because survival is correlated across peaks and no pairs re-form. Under speaker/echo, measured is 5–18% below p²: 6–11% of pairs whose two endpoints survive still change hash (fan-out slots taken by added peaks), and peak survival allows |Δf| ≤ 1 while hashes need exact f.
- *Monotonicity.* 13 violations in 216 rows. Most are 0.01. The largest is 0.03, where track recall also rises (echo, 10 s, mid: 0.90 at ∞, 0.93 at 30 dB), so light noise genuinely helps a few clips. Mechanism unverified.
- *min_magnitude.* The self-test's 148 "leakage" peaks came from float phase error in the test sine. Real Hann leakage peaks at −92 dB for a full-scale off-bin sine. The floor removes 0.0 peaks/s of music under the speaker channel at −80 dB.
- *Detune proposal* (`DETUNE=1 data/make_synthetic.sh`, off by default, `accuracy_phase1_detune.csv`). Distinct hashes 50k → 117k, entries scanned at the default 1.97M → 0.61M, speaker+echo 10 s @ 5 dB recall 0.91 → 0.98, **but holdout FPs rise** (6 → 20, 1 → 74, 22 → 113 of 3,600). Shorter posting lists lower the runner-up, so sustained-note pile-ups pass the ratio test. Not adopted. For review.

### Open points
- Subjective listening check of the synthetic tracks (1.4 "Done when") is still to be done by ear.
