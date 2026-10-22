# Shazam-experiment

An educational recreation of Avery Wang's audio fingerprinting algorithm from
["An Industrial-Strength Audio Search Algorithm"](https://www.ee.columbia.edu/~dpwe/papers/Wang03-shazam.pdf)
(ISMIR 2003).

The project has two main parts, plus an optional third:

1. **A minimal C99 matcher.** Spectrogram, peak picking, pair hashing, an inverted index and offset-histogram scoring, with no dependencies.
2. **A browser app.** The matcher compiled to WASM, visualising each stage of the maths on your own 10 s microphone recording.
3. **Optional: a 2003-vs-modern benchmark.** The same algorithm run with a period-style implementation (chained hash tables, scalar FFT) and a modern one (sorted array, vectorised FFT), to separate code gains from hardware gains. Planned for the end, after the visualisations.

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | Environment setup | You run this, see [PLAN.md](PLAN.md) |
| 1 | Core, CLI, synthetic data, accuracy test | Complete |
| 2 | FMA dataset | Not started |
| 3 | WASM and web record-and-match loop | Not started |
| 4–5 | Visualisation views, channel report, simulator | Not started |
| 6 | Video exporter scaffold | Not started |
| 7 | Optional: modern FFT and benchmark | Not started |

Progress notes and results are in [docs/progress.md](docs/progress.md) and
[docs/results-phase1.md](docs/results-phase1.md).

## How it works

1. Resample to 8 kHz mono and take a 1024-sample Hann-windowed FFT every 512 samples.
2. In each of 6 log-spaced bands (100 Hz to 4 kHz), keep the spectral peak if it is well above the band mean.
3. Pair each peak with the next few peaks in a target zone after it. Each pair becomes a 24-bit hash: `f1<<15 | f2<<6 | dt`.
4. Look every query hash up in the index. Each hit votes for `offset = tDb − tQuery` for that track.
5. The track with a tall spike in its offset histogram wins, provided it clears a minimum score and beats the runner-up by a ratio.

Every tunable lives in `fp_config` ([core/fp_config.h](core/fp_config.h)) and is
documented in [docs/parameters.md](docs/parameters.md).

## Layout

| Path | Contents |
|---|---|
| `core/` | C99 matcher library: fingerprint, index (two backends), match, channel simulation |
| `cli/` | `fp-index`, `fp-query`, `fp-accuracy`, `fp-selftest`, plus WAV and manifest helpers |
| `bench/` | Benchmark and accuracy scripts |
| `data/` | Synthetic track generator and manifests (audio is gitignored) |
| `web/` | Vite + TypeScript app (planned, phase 3) |
| `docs/` | Parameters, results, progress notes |
| `PLAN.md` | Step-by-step build plan |

## Requirements

- macOS with Apple clang and `make`. Phase 1 needs nothing else.
- Later phases need `ffmpeg`, `node` and `emscripten`: `brew install ffmpeg node emscripten`.
- No Python dependency.

## Quick start

```bash
make
build/fp-selftest
```

Generate 200 indexed and 50 holdout synthetic tracks (CC0, generated):

```bash
data/make_synthetic.sh
```

Build an index:

```bash
build/fp-index data/tracks.tsv -o build/index.bin
```

Query it with a WAV clip. `--impl both` runs both backends and asserts identical results:

```bash
build/fp-query build/index.bin clip.wav --impl both
```

Run the seeded accuracy grid (both backends on every query, aborting if they differ):

```bash
build/fp-accuracy data/tracks.tsv --out accuracy.csv
```

Regenerate the checked-in results in `docs/results/`:

```bash
bench/accuracy_phase1.sh grid
```

## Phase 1 results

On the synthetic set, with the default density:

- Index of 200 tracks: about 1.8M entries, built in under a second.
- Clean 10 s clips: 0.98 recall, 1.00 track-only recall.
- Speaker plus echo at 10 s: recall falls from 0.86 with no noise to 0.14 at 0 dB SNR.
- Both backends return identical results on all 32,400 grid queries.

Open questions, including scoring ambiguity at repeated sections and
sustained-note false positives, are listed under "Flags for review" in
[docs/progress.md](docs/progress.md).

## Data and licensing

Only fingerprints and reference peaks are shipped. Audio is never committed
unless the track's licence allows it. Track metadata (title, artist, licence,
FMA id) is kept in `tracks.json`. The synthetic tracks are CC0. Real music for
phase 2 comes from the [Free Music Archive](https://github.com/mdeff/fma) small
subset.

## Contributing notes

See [CLAUDE.md](CLAUDE.md) for the algorithm invariants and code style. The
two index backends must always return identical results.
