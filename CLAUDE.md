# Shazam-experiment

Educational recreation of Avery Wang's landmark audio fingerprinting algorithm
("An Industrial-Strength Audio Search Algorithm", ISMIR 2003): a minimal C99
matcher, a 2003-vs-modern benchmark, and a static browser app that visualises
the maths using the user's own 10 s mic recording.

The step-by-step build plan, with the recommended model for each step, is in
`PLAN.md`. Work one phase at a time and stop at each checkpoint it defines.

## Layout
- `core/`  C99 matcher library: fingerprint, index (two backends), match, channel sim. No dependencies.
- `cli/`   `fp-index`, `fp-query`, `fp-accuracy`, plus WAV and manifest helpers.
- `bench/` benchmark harness and report generator.
- `data/`  synthetic generator (`synth.c`) and FMA fetch/convert scripts. Audio lives in `data/audio/` (gitignored).
- `web/`   Vite + TypeScript static app; the matcher is compiled to WASM with Emscripten.
- `docs/`  `parameters.md`, results, `benchmark.md`, `progress.md` (source material for a later article).

## Commands
- `make`: builds `build/libfp.a` and all tools with `-Wall -Wextra -Werror`.
- `data/make_synthetic.sh`: writes 200 indexed + 50 holdout tracks plus `data/tracks.tsv` and `data/tracks.json`.
- `build/fp-index data/tracks.tsv -o build/index.bin`
- `build/fp-query build/index.bin clip.wav --impl both`: runs both backends and asserts identical results.
- `build/fp-accuracy data/tracks.tsv --out x.csv [--detail y.csv]`: seeded accuracy grid; both backends on every query, aborts if they differ.
- `bench/accuracy_phase1.sh [grid|detune|sweep]`: regenerates `docs/results/accuracy_phase1*.{csv,txt}` and `sweep_phase1.{csv,txt}`.

## Environment
- macOS, Apple M4 Pro, Apple clang 17, make, git.
- ffmpeg, node and emcc are not installed until the user runs the setup in `PLAN.md` phase 0. Check with `which` before relying on them. Never install system packages yourself; ask the user to.
- No Python dependency. Shell, C and TypeScript only.

## Algorithm invariants (do not change without updating docs/parameters.md)
- Every tunable lives in `fp_config` (`core/fp_config.h`). No magic numbers elsewhere.
- 8000 Hz mono, Hann 1024, hop 512. Bins 1..511 only, so `f` fits in 9 bits.
- The spectrogram is stored as `log1p(|X|)` for display. The peak test is in **linear** magnitude: `|X_peak| > threshold × mean|X|` over the band in that frame, plus an absolute floor `min_magnitude_db` (dB relative to a full-scale sine, |X| = window_size/4). 6 log-spaced bands, 100 Hz to 4000 Hz.
- Pairs: the next `fan_out` peaks with `min_dt ≤ dt ≤ max_dt` and `|df| ≤ max_df`, with peaks sorted by (t, f).
- Hash: `f1<<15 | f2<<6 | dt` (24 bits). Packed entry: `hash:24 | track:21 | time:19` as a uint64.
- Scoring: `offset = tDb − tQuery`, bin = `floor(offset / offset_bin)`. Per-track best bin, ties to the lowest offset. Rank by score desc, then track id asc. Runner-up = best score of a different track. Match iff `score ≥ min_score && score ≥ min_ratio × runner_up`.
- `naive2003` and `modern` must return identical results on the same input. Any change to either backend must keep `--impl both` passing.
- Tracing must be compiled in but disabled in benchmark runs.
- All randomness goes through the seeded RNG in `core/rng.h`, so runs are reproducible.

## Code style
- Minimal comments; the code should explain itself. Comment only a non-obvious *why* (a constraint, workaround or invariant). Never restate what the code does.
- No end-of-line comments. Put comments on the line above the relevant block.
- Don't use process verbs like "added", "fixed" or "changed" in comments or docs.
- C99, `snake_case`, `fp_` prefix on public core symbols, and no globals in `core/` (so it stays WASM- and thread-safe).

## Data and licensing
- Ship fingerprints and reference peaks only. Never commit or ship audio unless the track's licence allows it.
- Keep title, artist, licence and FMA id in `tracks.json`.

## Workflow
- At the end of each phase: update `docs/progress.md` with a short summary, then commit ("Phase N: ..."). Commit messages end with the Co-Authored-By line for the model used.
- Stop at every checkpoint marked **STOP** in `PLAN.md` and report results before continuing.
