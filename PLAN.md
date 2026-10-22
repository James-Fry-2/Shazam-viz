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

### 1.8 Two-bin offset scoring and exact-bin survival metric (Opus 5.5, high effort)
- **Files:** `core/fp_config.h`, `core/fingerprint.c`, `core/match.c`, `cli/config_args.h`, `cli/fp-accuracy.c`, `cli/fp-selftest.c`, `bench/accuracy_phase1.sh`, docs.
- **`score_window`** (1 or 2, scoring-only, not in the index snapshot): 2 scores each track by its best sum of bins b and b+1, adjacent by value. Reports the bin with more votes, the lower on a tie. `runner_up` and `top[3]` use the windowed scores. `--score-window` flag.
- **Exact survival:** CSV columns `peak_survival_exact` and `pred_hash_survival_exact`, appended after the existing ones.
- **Results:** window 2 halves the wrong-offset rate and holds holdout FPs, but lowers recall under noise with `min_ratio` 2.0, so the default stays 1. The step 1.6 figure of 37 of 404 did not reproduce. Details in `docs/progress.md` and `docs/results-phase1.md`.
- **Done when:** the grid is re-run at both windows with every query identical on both backends, and the before and after tables are in `docs/results-phase1.md`.

### 1.9 Lessons from real recordings (Opus 5.5, high effort)
Source: the side project `~/Repos/Audio-Fingerprint-Test` (`fp.py`, Python, same hash layout). Its phone and mic recordings matched through the C matcher unchanged (`ThrowThemRec.wav`: 481 votes vs 1, same offset as `fp.py`), so the core works on real audio. These changes address the open flags in `docs/progress.md`. Each one changes an invariant in `CLAUDE.md`, so update `CLAUDE.md` and `docs/parameters.md` with it. Measure each change on its own with the step 1.6 grid, keeping `--impl both` passing.
- **Window the offset score.** Done in step 1.8 as a 2-bin window (`score_window`), left at 1 by default because it lowers recall under the current `min_ratio`; the 3-bin window and same-track runner-up exclusion proposed here were not built. The 37 of 404 figure did not reproduce at the final defaults. Choose the window and the decision rule together. Real recordings never share the reference's frame grid, so this matters more outside the simulator.
- **Suppress repeated hashes.** Keep only the first copy of a hash within `repeat_window` (default 4) frames of its last occurrence, updating the last-seen time even for dropped copies. A held tone produces the same hash every frame, and each copy votes for a different offset. This is the targeted fix for the sustained-note false positives (the `f1 == f2` cases), and it is local, so a clip and a full song agree. Try it before dropping `f1 == f2` pairs.
- **Target a peak density per file.** Bisect the threshold on the file's own peaks to hit `peaks/s`, with a minimum threshold. A fixed ratio finds almost nothing in a noisy room, and the grid already shows recall tracking density (speaker+echo 10 s: 0.98 at 60 peaks/s, 0.60 at 14). Compute band max and mean once per frame so the bisection is cheap. The index and the query then pick peaks by different thresholds, which is fine because hashes depend only on peak positions.
- **Normalise level before analysis.** Scale to a fixed RMS, measured on non-silent 100 ms blocks (blocks above 1e-3 of the loudest). The linear peak test is scale invariant but `min_magnitude_db` and the `log1p` display are not. An iPad voice memo at RMS 0.004 fingerprinted to zero peaks in `fp.py` before this.
- **Revisit the decision rule with real clips.** `fp.py` accepts at 10 votes and 3× the runner-up; the C defaults of 5 and 2× are the rule flagged for accepting noise matches of 6 vs 3. Add the real recordings to the sweep.
- **Leave whitening off.** `fp.py` implements per-bin median subtraction and it hurt matching, so do not add it.
- **Done when:** the grid is re-run, the CSV shows holdout FPs and wrong-offset wins no worse than before and recall no worse, and the real recordings still match under `--impl both`. Report the before and after for each change separately.
- **STOP** and wait for review before phase 2.

---

## Phase 2: dataset

Real music from FMA, plus a small set of real phone recordings. Synthetic channels share one frame grid with the clean clip by construction; only the real set shows the grid mismatch. Step 2.1 can run before 1.8 is committed; 2.2 and 2.3 need its final config.

FMA files stay apart from the synthetic set: downloads in `data/fma/`, audio in `data/audio-fma/` (both gitignored), manifests in `data/tracks-fma.{tsv,json}`. `data/tracks.tsv` stays synthetic.

### 2.1 FMA fetch and convert (Sonnet 5.5, medium effort)
- **Files:** `data/fetch_fma.sh`, `data/fma_select.c` (Makefile target `build/fma_select`), `data/README.md`, `.gitignore`.
- Download `fma_metadata.zip` and `fma_small.zip` (about 7.6 GB together; you run it) into `data/fma/`, using the URLs and SHA1s in the github.com/mdeff/fma README. Skip a download whose SHA1 already matches. Unzip each into its own fresh directory.
- `fma_select <tracks.csv> --seed n --indexed 200 --holdout 50` is C, not Python. It parses RFC 4180 (quoted fields with commas and newlines), skips the 3 header lines, keeps `set.subset == small`, and draws through `core/rng.h`, stratified by `track.genre_top`. It prints `fma_id, title, artist, licence, genre, split` as TSV, plus seeded spares.
- Convert in parallel with `ffmpeg -nostdin -v error -i X -ac 1 -ar 8000 -sample_fmt s16`. A few fma_small files fail to decode or are short, so reject anything that fails or is under 29 s and take the next spare in the same split.
- Write `data/tracks-fma.tsv` (`id<TAB>path<TAB>split`, readable by `cli/manifest.c` unchanged) and `data/tracks-fma.json` (title, artist, licence, fma_id, genre, split). `INDEXED`, `HOLDOUT` and `JOBS` override the defaults, as in `make_synthetic.sh`.
- **Done when:** a second run reproduces both manifests byte for byte, `git ls-files '*.wav' '*.mp3'` prints nothing, and `build/fp-index data/tracks-fma.tsv -o build/index-fma.bin` runs and prints peaks/s, entries, distinct hashes and the f1 == f2 share.

### 2.2 FMA accuracy grid (Opus 5.5, high effort). After 1.8 is committed
- Run the step 1.6 grid, with the final 1.8 config, on `data/tracks-fma.tsv` through a new `fma` mode in `bench/accuracy_phase1.sh`, writing `docs/results/accuracy_phase2_fma.{csv,txt}`.
- Compare with the synthetic grid: recall, holdout FP, wrong-offset rate, posting-list stats, f1 == f2 share and entries scanned.
- Explain every holdout FP. FMA has some duplicate audio under different ids. Report those as duplicates rather than filtering them out.
- **Done when:** the grid runs on real data without code changes, both backends agree, and `docs/progress.md` has the synthetic vs FMA table.

### 2.3 Real-channel test set (you record, then Opus 5.5, high effort)
- **You:** play 10 to 20 indexed FMA tracks on a phone speaker and record 10 s each on the laptop mic (or the reverse), varying distance and room. Save to `data/real/*.wav` (gitignored), with `data/real/real.tsv` listing `wav, fma_id, approx_start_s, device, distance, room`.
- **Shared metrics:** move the survival metrics from `cli/fp-accuracy.c` into `core/metrics.{c,h}` so phase 5 can use them in WASM. `bench/accuracy_phase1.sh grid` must still reproduce the CSV byte for byte.
- **Ground truth:** a hand-noted start is only good to about ±0.5 s (8 frames). Find the true sample offset by normalised cross-correlation of the recording against the reference within ±2 s of the noted start, band-limited to 300–3000 Hz, and report the peak-to-second-peak ratio as a confidence. This doesn't use the fingerprint, so the check isn't circular.
- **`cli/fp-realset.c`:** for each recording, report:
  - The match: hit (true track, winner within ±1 bin of `floor(sample/hop)`), wrong offset, wrong track, or no match. Both backends must agree.
  - Peak survival, hash survival and p², measured two ways: (a) against the reference cut at the xcorr sample (shared grid, the simulator's metric) and (b) against the reference on the index's frame grid. (a) − (b) is the grid-mismatch loss.
  - A simulator check: `channel.c` speaker+echo on the (a) excerpt at the recording's measured SNR, with survival compared to (a).
- Write `docs/results/realset_phase2.{csv,txt}`.
- **Done when:** real-set recall is reported next to the simulated channels, the grid-mismatch loss is quantified, and the simulator-vs-real gap is written up in `docs/progress.md`.
- **STOP** and wait for review before phase 3.

---

## Phase 3: WASM + web record-and-match loop

### 3.1 Trace output (Sonnet 5.5, medium effort)
- **Files:** `core/trace.{c,h}`.
- A JSON writer into a growable buffer (no deps). Contents: config; spectrogram as uint8 (`255 × log1p / max`), base64; peaks; pairs; each lookup with its hits and offsets; vote histograms for the top 3 candidates; the result.
- Enabled by an `fp_trace *` argument; `NULL` means off. Off in any benchmark run.
- `fp-query --trace out.json`.

### 3.2 WASM build (Sonnet 5.5, medium effort)
- `web/wasm/build.sh` runs `emcc` with `-O3 -sMODULARIZE -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH` and exports `fp_load_index`, `fp_query_trace` and `fp_free`. Use the modern backend.
- Write `web/public/index.bin` and `web/public/tracks.json` with a cli step.

### 3.3 Web scaffold and record loop (Sonnet 5.5, medium effort)
- Vite + TS, no framework (or Preact if the views get heavy). Use CSS variables for light/dark, and make it mobile-first.
- Call `getUserMedia({audio:{echoCancellation:false, noiseSuppression:false, autoGainControl:false}})`. Record 10 s through an AudioWorklet, with a live waveform and a countdown.
- Resample with `OfflineAudioContext(1, 10*8000, 8000)`, pass the result to WASM, get the trace back, and show the match.
- With AGC off the input is quiet. Normalise the level before the spectrogram display (step 1.9), or the view is flat.
- The recording's frame grid never lines up with the reference's, so test with a clip whose start is not a multiple of the hop.
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
- Layout reference: `~/Repos/Audio-Fingerprint-Test/fp.py`, `plot_report`. Three panels: spectrogram with survived, lost and new peaks and band lines; every hash hit as query time against reference time with the winning diagonal in a second colour; the offset histogram with the winner marked.
- Pick one survival tolerance and use it for both metrics. `fp.py` allows ±1 frame and ±1 bin for peaks and ±1 frame for hashes. The C metric uses exact t, so the two give different numbers for the same recording.
- Real data to explain: a phone recording of a track kept 80% of peaks but only 35% of hashes, against 64% predicted by p². The frame grid mismatch rounds some Δt values differently, which the shared-grid simulator cannot show. Include a real-recording example in the explanation.
- Show per-band survival. Small phone speakers wipe out the lowest bands, and the view makes the reason visible.

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

---

## Phase 7 (optional): modern FFT and 2003-vs-modern benchmark
Do this only after the visualisation work is done.

### 7.1 Modern FFT and the equivalence question (Opus 5.5, high effort)
- **Files:** `core/fft_modern.c`.
- A real-input FFT (pack N real samples as an N/2 complex FFT, then post-process), radix-4 or split-radix, written so clang auto-vectorises it with NEON.
- **Decide and document in `docs/parameters.md`:** different floating-point summation can flip a peak that sits exactly at the threshold. Recommended approach: fingerprint correctness is *peak-set equality within tolerance* (report the mismatch rate), and match equivalence is tested on the *same hash list*, so the index and scoring comparison stays exact.

### 7.2 Benchmark harness and report (Opus 5.5, high effort)
- **Files:** `bench/bench.c`, `bench/report.sh`, `docs/benchmark.md`.
- Build two binaries: `bench-2003` (naive backend, scalar FFT, single thread, `-O2 -fno-tree-vectorize -fno-slp-vectorize`) and `bench-modern` (`-O3 -mcpu=native`, optional pthreads lookup).
- **Measure:** fingerprint time per 10 s query, lookup time, entries scanned per query, index bytes per track, and ns per scanned entry. Take the median of ≥ 20 runs after warm-up, using `clock_gettime(CLOCK_MONOTONIC)`. Tracing must be off.
- **Separate code gains from hardware gains:** compare naive vs modern on the same M4 for the code gain. Estimate hardware with a stated scaling assumption (e.g. a 2003 Pentium 4 at ~3 GHz, citing published SPECint ratios) and label it clearly as an estimate.
- **Extrapolate to 1.8M tracks:** entries scanned grows roughly linearly with index size for a fixed hash distribution. Show the formula and its assumptions, including the cache misses a 2003 machine would take.
- Commit and update `progress.md`.

