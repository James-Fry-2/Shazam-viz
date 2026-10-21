# Build plan

Each step lists: the goal, the files it touches, what "done" means, and the
recommended model and effort. `CLAUDE.md` holds the invariants that every step
must respect.

## How to run this plan with Claude models

### Which model for what
| Model | Use it for | Why |
|---|---|---|
| **Opus 5.5** | Algorithm core, scoring equivalence, accuracy and benchmark methodology, the signal-processing maths in the web views, debugging anything numerical | Where subtle mistakes (off-by-one framing, tie-breaks, biased metrics) silently ruin the results, the extra reasoning pays for itself |
| **Sonnet 5.5** | Well-specified implementation: CLI tools, WAV I/O, scripts, Makefile, Vite/TS scaffolding, UI views once the maths is settled | Fast and cheap. Handles clearly specified code very well |
| **Haiku 5.5** | Mechanical work: docs tidy-ups, renames, running the test suite and summarising output, small CSS tweaks | Cheapest. Fine when nothing needs designing |

Fable 5.1 is also available, but I don't have a reliable picture of how it
compares on this kind of work, so the plan doesn't depend on it. If you want to
try it, use it on a Sonnet-rated step and compare.

**Effort:** set it to *high* for Opus steps and *medium* for Sonnet steps. *Low* is enough for Haiku.

### Session hygiene
1. **One fresh session per step group** (e.g. "1.1–1.2"). Long sessions fill up the
   context with stale file contents. `CLAUDE.md` plus this file give a fresh session everything it needs.
2. **Opening prompt template:**
   > Read CLAUDE.md and PLAN.md. Execute steps X.Y–X.Z only. Use plan mode first,
   > then implement. Verify using the step's "Done when" criteria and stop.
3. **Plan mode for Opus steps.** Let the model propose a design, check it against
   this file, then approve. Sonnet and Haiku steps can usually go straight to implementation.
4. **Commit at every step boundary** (not only at phase ends), so a bad step is cheap to revert.
5. **Switch models between steps, not mid-step.** If a Sonnet step gets stuck on something
   numerical (wrong offsets, results that differ between backends), open an Opus session for just that bug.
6. **Make the model show evidence.** Each "Done when" is a command whose output proves the
   step works. Ask for that output instead of accepting "it works".

---

## Phase 0: Environment (you run these)
You only need these before the phases noted. Phase 1 needs nothing new.
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
```bash
brew install ffmpeg node emscripten
```
- ffmpeg: phase 2. node: phase 3. emscripten: phase 3.
- Check: `ffmpeg -version`, `node --version`, `emcc --version`.

---

## Phase 1: core + cli + synthetic data + accuracy test

### 1.1 Config, FFT, fingerprint (Opus 5.5, high effort)
- **Files:** `core/fp_config.h` (already written), `core/fft.{c,h}`, `core/fingerprint.{c,h}`, `core/rng.h`, `Makefile`.
- **FFT:** iterative in-place radix-2, scalar, with precomputed twiddles and a bit-reversal table, all held in a plan struct so there are no globals.
- **Spectrogram:** `frames = 1 + (n − window)/hop`. Hann-window each frame, take `|X|` for bins 0..512, and store `log1p(|X|)` as `float[frames][513]`.
- **Peaks:** compute the band edges once from `band_lo_hz`, `band_hi_hz` and `num_bands` (log-spaced, as bin indices clamped to 1..511). For each frame and band: find the max bin and the mean linear `|X|`. Keep it if `max > threshold × mean && max > min_magnitude`. Output `{t, f, mag}`, sorted by (t, f).
- **Pairs:** for anchor i, scan j > i. Skip dt < min_dt, stop at dt > max_dt, require |df| ≤ max_df, and take up to fan_out. Output `{hash, t_anchor, a_idx, b_idx}`. Keep the indices: the phase 3 trace needs them.
- **Makefile:** `fft.c` and `index_naive.c` get `-O2 -fno-tree-vectorize -fno-slp-vectorize`. `index_modern.c` gets `-O3 -mcpu=native`. Everything gets `-std=c99 -Wall -Wextra -Werror`.
- **Done when:** a tiny test program (`cli/fp-selftest.c`) shows that a 1 kHz sine produces a peak at bin 128 ± 1 in every frame, the FFT of an impulse is flat, and the hash pack/unpack round-trips.

### 1.2 Index backends, file format, matching (Opus 5.5, high effort)
- **Files:** `core/index.h`, `core/index_naive.c`, `core/index_modern.c`, `core/index_file.c`, `core/match.{c,h}`.
- **Common API:** `fp_index_create(impl)`, `add(track, hashes)`, `finalize()`, `query(hashes) → votes`, `bytes()`, `free()`. Use a vtable struct, not `#ifdef`, so both backends coexist in one binary.
- **naive2003:** chained hash table (2^20 buckets, `hash & mask`) with one `malloc` per entry node. Votes go into a chained hash map keyed by (track, offset_bin), also one `malloc` per key. This is deliberately 2003-style.
- **modern:** collect packed uint64s, sort them once, and build a jump table on the top 16 hash bits (65537 uint32 offsets) with a binary search inside each prefix. For votes, collect `(track<<32 | (offset_bin + 2^31))` into an array, sort, and run-length count.
- **match.c:** takes the per-(track, bin) counts from either backend and applies the ranking and decision rules from `CLAUDE.md`. Returns the winner, offset, score, runner_up, top[3], hashes_used and entries_scanned. Because this one function decides for both backends, equivalence reduces to "same counts".
- **index.bin:** magic `WFP1`, version, a snapshot of the fingerprint fields of `fp_config`, num_tracks, per-track frame counts, num_entries, then the sorted packed entries. Both backends load from it. A query must refuse an index whose config snapshot doesn't match its own.
- **Done when:** a self-test that indexes 3 synthetic sine-sweep tracks and queries an excerpt gets the right track and offset from both backends, and the results compare equal field by field.

### 1.3 Channel simulation, resampler, I/O (Sonnet 5.5, medium effort)
- **Files:** `core/channel.{c,h}`, `core/resample.c`, `cli/wav.{c,h}`, `cli/manifest.{c,h}`.
- **Speaker:** RBJ biquad high-pass at 380 Hz, Q 0.707, then a low-pass at 3300 Hz, then `tanh(drive·x)/tanh(drive)` with drive 2.
- **Echo:** direct path plus about 12 taps at seeded random delays within 0.5 s, gain `0.6·exp(−3·delay/0.5)`, with random signs, applied as a sparse convolution.
- **Pink noise:** Paul Kellet filter on seeded white noise, scaled to `rms(signal) / 10^(snr/20)`.
- **Resampler:** windowed-sinc (Kaiser or Hann, 16 taps per side) used only when a WAV isn't at 8 kHz.
- **WAV:** read 16-bit PCM and 32-bit float, downmix to mono; write 16-bit mono.
- **Manifest:** TSV `id<TAB>path<TAB>split` where split is `indexed` or `holdout`.
- **Done when:** the self-test shows the speaker filter attenuates 100 Hz by more than 12 dB and passes 1 kHz within 1 dB, and the measured SNR of the noise stage is within ±0.2 dB of the target.

### 1.4 Synthetic track generator (Sonnet 5.5, medium effort)
- **Files:** `data/synth.c`, `data/make_synthetic.sh`, `data/README.md`.
- **Per seed:** tempo 80–160 BPM, a random key, a major or minor scale, and a 4-chord progression. Structure is verse-chorus-verse-chorus, so sections repeat (realistic offset ambiguity within a track). Voices: 2–3 harmonic instruments (sum of partials with an ADSR envelope), a bass line, and kick/snare/hat noise bursts. 30 s at 8 kHz.
- **Script output:** 200 indexed + 50 holdout tracks in `data/audio/`, `data/tracks.tsv`, and `data/tracks.json` (title, artist "Synthetic", licence "CC0 (generated)", seed, split).
- `data/README.md` documents the real FMA step, to be implemented in 2.1.
- **Done when:** the script runs in under 30 s, and listening to two tracks confirms they sound distinct and music-like (you check this by ear).

### 1.5 CLI tools (Sonnet 5.5, medium effort)
- **Files:** `cli/fp-index.c`, `cli/fp-query.c`.
- `fp-index <manifest> -o <index.bin> [--threshold x] [--fan-out n] ...` indexes only the `indexed` rows and prints tracks, entries, peaks/s and bytes.
- `fp-query <index.bin> <wav> [--start s] [--len s] [--impl naive|modern|both]` prints the result. `both` exits non-zero if the two results differ.
- **Done when:** a 10 s excerpt of an indexed track matches with the correct offset under `--impl both`, and a holdout track reports no match.

### 1.6 Accuracy test and density tuning (Opus 5.5, high effort)
- **Files:** `cli/fp-accuracy.c`, `docs/results/accuracy_phase1.csv`.
- **Grid:** densities (thresholds, tuned so peaks/s span roughly 10–60) × clip length {3, 5, 10 s} × channel {clean, speaker, echo, speaker+echo} × SNR {∞, 30, 20, 10, 5, 0}. Use 100 indexed clips + 50 holdout clips per length, with seeded random start samples that are deliberately not frame-aligned.
- **True offset:** the clip starts at sample s, so the expected offset is `s/hop` in frames. Count a hit when the winner is the true track and the offset is within ±1 bin of the truth.
- **Metrics per row:** recall, holdout false-positive rate, wrong-track rate, peak survival (clean-clip peaks found again in the degraded clip at the same t with |Δf| ≤ 1), hash survival (exact (hash, t)), predicted hash survival = peak_survival², median true/best-wrong vote ratio, and peaks/s.
- Run every query on both backends and abort if any result differs.
- **Watch for:** the clean clip and the degraded clip must use identical framing for the survival metrics. Echo adds latency only through its direct path, so keep the direct path at delay 0.
- **Done when:** the CSV is written, clean 10 s recall is about 100%, holdout FPs are about 0%, and recall falls off monotonically with SNR.

### 1.7 Docs and commit (Sonnet 5.5 or Haiku 5.5)
- **Files:** `docs/parameters.md` (every config field, its default and why), `docs/results-phase1.md` (summary tables), `docs/progress.md`, `.gitignore`.
- `git init`, then commit "Phase 1: core matcher, CLI, synthetic data, accuracy test".
- **STOP.** Report the parameter choices, the density vs recall/FP table, and measured vs predicted hash survival. Wait for review before phase 2.

---

## Phase 2: dataset + benchmark

### 2.1 FMA dataset script (Sonnet 5.5, medium effort)
- **Files:** `data/fetch_fma.sh`.
- Download `fma_metadata.zip` and `fma_small.zip` from the URLs listed in the github.com/mdeff/fma README. Check the published SHA1s. Unzip each into its own fresh directory.
- Select N tracks (default 200, seeded) and parse `tracks.csv` for title, artist, licence and track id. Use `awk` if it copes with the CSV quoting; otherwise use the existing `python3 -I` with just the stdlib `csv` module.
- Convert with `ffmpeg -ac 1 -ar 8000 -sample_fmt s16`. Write `tracks.tsv` and `tracks.json`, and mark 50 of the tracks as holdout.
- **Done when:** the indexing and accuracy runs work on real data unchanged.

### 2.2 Modern FFT and the equivalence question (Opus 5.5, high effort)
- **Files:** `core/fft_modern.c`.
- A real-input FFT (pack N real samples as an N/2 complex FFT, then post-process), radix-4 or split-radix, written so clang auto-vectorises it with NEON.
- **Decide and document in `docs/parameters.md`:** different floating-point summation can flip a peak that sits exactly at the threshold. Recommended approach: fingerprint correctness is *peak-set equality within tolerance* (report the mismatch rate), and match equivalence is tested on the *same hash list*, so the index and scoring comparison stays exact.

### 2.3 Benchmark harness and report (Opus 5.5, high effort)
- **Files:** `bench/bench.c`, `bench/report.sh`, `docs/benchmark.md`.
- Build two binaries: `bench-2003` (naive backend, scalar FFT, single thread, `-O2 -fno-tree-vectorize -fno-slp-vectorize`) and `bench-modern` (`-O3 -mcpu=native`, optional pthreads lookup).
- **Measure:** fingerprint time per 10 s query, lookup time, entries scanned per query, index bytes per track, and ns per scanned entry. Take the median of ≥ 20 runs after warm-up, using `clock_gettime(CLOCK_MONOTONIC)`. Tracing must be off.
- **Separate code gains from hardware gains:** compare naive vs modern on the same M4 for the code gain. Estimate hardware with a stated scaling assumption (e.g. a 2003 Pentium 4 at ~3 GHz, citing published SPECint ratios) and label it clearly as an estimate.
- **Extrapolate to 1.8M tracks:** entries scanned grows roughly linearly with index size for a fixed hash distribution. Show the formula and its assumptions, including the cache misses a 2003 machine would take.
- Commit and update `progress.md`.

---

## Phase 3: WASM + web record-and-match loop

### 3.1 Trace output (Sonnet 5.5, medium effort)
- **Files:** `core/trace.{c,h}`.
- A JSON writer into a growable buffer (no deps). Contents: config; spectrogram as uint8 (`255 × log1p / max`), base64; peaks; pairs; each lookup with its hits and offsets; vote histograms for the top 3 candidates; the result.
- Enabled by an `fp_trace *` argument; `NULL` means off. Off in `bench/`.
- `fp-query --trace out.json`.

### 3.2 WASM build (Sonnet 5.5, medium effort)
- `web/wasm/build.sh` runs `emcc` with `-O3 -sMODULARIZE -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH` and exports `fp_load_index`, `fp_query_trace` and `fp_free`. Use the modern backend.
- Write `web/public/index.bin` and `web/public/tracks.json` with a cli step.

### 3.3 Web scaffold and record loop (Sonnet 5.5, medium effort)
- Vite + TS, no framework (or Preact if the views get heavy). Use CSS variables for light/dark, and make it mobile-first.
- Call `getUserMedia({audio:{echoCancellation:false, noiseSuppression:false, autoGainControl:false}})`. Record 10 s through an AudioWorklet, with a live waveform and a countdown.
- Resample with `OfflineAudioContext(1, 10*8000, 8000)`, pass the result to WASM, get the trace back, and show the match.
- **Done when:** on your phone, playing a synthetic track from a laptop speaker matches.

Commit and update `progress.md`.

---

## Phase 4: visualisation views 2–5

### 4.1 Mapping explorer (Opus 5.5 for the audio maths, then Sonnet 5.5 for the UI)
- Waveform and spectrogram on a shared time axis. The slice panel shows the spectrogram column as a sideways bar chart, aligned to the frequency axis.
- Drag a box to select time and frequency. Dashed guides project the box onto the waveform and the slice.
- **Opus part:** the band-filtered waveform inside the box (offline FFT → zero bins outside the box → overlap-add ISTFT, which is exact with Hann at 50% overlap). Also the "peaks only" resynthesis: a sum of sinusoids at each peak frequency, with amplitude from `mag`, Hann-enveloped per frame for click-free joins. Hover shows time, Hz, and the nearest note name (`12·log2(f/440)+69`).
- **Sonnet part:** canvas rendering, interaction and playback through `AudioBufferSourceNode`.

### 4.2 Index lookup (Sonnet 5.5)
- Step through the query hashes. Show the decoded f1/f2/dt, the index row with one chip per entry (track, tDb), and the arithmetic `tDb − tQ = offset`.

### 4.3 Alignment (Sonnet 5.5)
- Scatter of tQuery against tDb for the top 3 candidates. Animate the projection onto the offset axis to form a histogram, and highlight the winning diagonal.

### 4.4 Lock-in (Sonnet 5.5)
- Needs the winning track's reference peaks. Ship a per-track peaks file, `peaks.bin` (t:19, f:9 packed). The query constellation slides over the reference and locks at the offset.

Commit and update `progress.md`.

---

## Phase 5: channel report and simulator (views 6, 7)

### 5.1 Channel report (Opus 5.5 for the metrics, then Sonnet 5.5 for the UI)
- Align the query peaks to the reference peaks using the match offset. Classify each peak as survived (same t, |Δf| ≤ 1), lost, or added.
- Show survival per band. Show measured hash survival against the peak_survival² prediction, and explain why they can differ (fan-out pairs aren't independent, and the target zone can shift).
- Reuse the phase 1 metric code so the CLI and web numbers agree.

### 5.2 Simulator (Sonnet 5.5)
- A bundled demo track: one of the CC0 synthetic tracks, or an FMA track whose licence allows redistribution.
- Run `channel.c` through WASM with sliders for speaker, echo, SNR and threshold, and live metrics and match result.

Commit and update `progress.md`.

---

## Phase 6: video exporter scaffold (Sonnet 5.5)
- **Files:** `video/` (Node + `node-canvas`, or a headless page with frame capture).
- Read a trace JSON and render 1920×1080 PNG frames per scene: waveform → windowing into spectrogram → peak picking → hashing → lookup and votes → alignment. Pipe them to `ffmpeg -framerate 30 -i %05d.png -c:v libx264 -pix_fmt yuv420p`.
- Scaffold only: scene timing in one config, one working scene end to end, and the others stubbed.

Commit and update `progress.md`.
