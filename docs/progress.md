# Progress

## Phase 1 (in progress): steps 1.1–1.5 done, 1.6–1.7 remaining

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

### Open points for 1.6
- The default config gives about 83 peaks/s on the synthetic tracks, above the intended 10–60 range, so the threshold needs tuning.
- Holdout best-wrong scores reach about 750 on 10 s clips, so the `min_ratio` rule, not `min_score`, rejects them.
- Subjective listening check of the synthetic tracks (1.4 "Done when") is still to be done by ear.
