# Parameters

Every tunable lives in `fp_config` (`core/fp_config.h`). This file covers the
fields that step 1.6 tuned. Step 1.7 completes it for the remaining fields.

The evidence comes from `docs/results/sweep_phase1.csv` (reduced grid: 5 s and
10 s clips, clean and speaker+echo, SNR ∞/10/5/0, 100 indexed + 50 holdout clips)
and `docs/results/accuracy_phase1.csv` (full grid). `bench/accuracy_phase1.sh`
regenerates both.

## Peak picking

| Field | Default | Was |
|---|---|---|
| `threshold` | 6.0 | 4.0 |
| `min_magnitude_db` | −80 dB | `min_magnitude` = 1e-3 linear (≈ −108 dB) |

### threshold
The peak test is a ratio: the band maximum must exceed `threshold × mean|X|`
over the band in that frame. Two consequences drive the choice:

- **Noise robustness falls fast as threshold rises.** Broadband noise lifts the
  band mean, so peaks stop clearing the ratio long before they are buried.
  Clean 10 s clips at 10 dB pink-noise SNR keep 67% of their peaks at
  threshold 6 but only 10% at threshold 16. Lowering the threshold is the
  strongest lever on noisy recall that we found. It costs more peaks and
  so more entries scanned.
- **A band of N bins can never pass a threshold ≥ N**, because max/mean ≤ N.
  The 6 log-spaced bands from 100 Hz to 4 kHz are 11, 20, 37, 69, 127 and 235
  bins wide. At threshold 11 or more the lowest band (100–185 Hz) produces
  nothing, and band 1 (ratio cap 20) yields only 0.2 peaks/s at threshold 12. Threshold therefore changes which
  bands contribute, as well as how many peaks there are.

6.0 gives 60.6 peaks/s on the synthetic set. That is the top of the 10–60
peaks/s range in PLAN.md, and the cheapest setting that matched or beat the old
default on noisy recall (see "Pairing" below).

### min_magnitude_db
An absolute floor on the peak's linear magnitude, in dB relative to a full-scale
sine. With the periodic Hann window of length N, a sine of amplitude 1 on a bin
centre gives |X| = N/4 (256 for N = 1024). The floor is
`(window_size / 4) · 10^(min_magnitude_db / 20)`, computed once per fingerprinter.

Measured:

- An off-bin (1003.7 Hz) sine at −1.9 dBFS leaks Hann sidelobe peaks into every
  other band at up to −92 dB. With the old floor (≈ −108 dB) that produces an
  off-target peak in every frame. At −80 dB there are none, a 12 dB margin.
- The "148 off-target peaks" in the earlier self-test came mostly from the
  test signal: `sinf` of a float phase that reaches ~31,000 rad adds broadband
  phase noise up to −72 dB. With the phase in double precision, an on-bin
  sine has 0 off-target peaks at any floor. The self-test now uses double phase
  and adds the off-bin case.
- On the synthetic music the floor has no effect anywhere from −108 to −50 dB:
  peaks removed at −80 dB, threshold 6, speaker channel, are 0.0 peaks/s (clean,
  30 dB and 10 dB SNR alike). Only at −40 dB does it start removing music peaks
  (−4.3 of 61.1 peaks/s with no channel). The floor values swept on the reduced grid (−108, −80, −60, −50 dB) give pooled noisy recall 0.787, 0.787, 0.785 and 0.775.
- Headroom: tracks peak at −1.9 dBFS. A floor at −80 dB keeps 30 dB of headroom
  for quiet input (for example a mic in phase 3 with AGC off) before it behaves
  like −50 dB at full level, which removes 0.5 of 61.1 peaks/s. The value suits
  inputs whose level is within ~30 dB of full scale.

## Pairing

| Field | Default | Was |
|---|---|---|
| `min_dt` | 5 frames (320 ms) | 1 |
| `fan_out` | 5 | 5 |
| `max_dt` | 63 | 63 |

**Departure from the spec's dt range.** The hash still encodes dt in 6 bits
(1..63), but pairs with dt < 5 are skipped. The reason is measured, not assumed:

- At min_dt 1, 95% of pairs had dt ≤ 3 and 66% had dt = 1. Peaks are sorted by
  (t, f) and there are up to 6 per frame, so the "next 5 peaks" are almost
  always in the next frame. dt then carries almost no information, and the
  hash space reduces to (f1, f2).
- f is itself low-entropy on this data: 60–88 effective bins, with half of all
  peaks in 14–25 bins. Distinct hashes stayed at 44–57k at every threshold.
- Holding density fixed and moving min_dt from 1 to 5 roughly halves entries
  scanned per query **and** raises noisy recall at both thresholds tested:

| reduced grid, fan_out 5 | entries/query (10 s, clean) | pooled noisy recall |
|---|---|---|
| threshold 12, min_dt 1 | 653k | 0.310 |
| threshold 12, min_dt 5 | 315k | 0.357 |
| threshold 8, min_dt 1 | 2.14M | 0.595 |
| threshold 8, min_dt 5 | 1.10M | 0.677 |
| threshold 6, min_dt 1 | 3.55M | 0.680 |
| threshold 6, min_dt 5 | 1.97M | 0.787 |

"Pooled noisy recall" is the mean of four speaker+echo cells (5 s and 10 s clips
at 10 dB and 5 dB SNR, n = 400). The single cells differ by 1–3 clips between
neighbouring configs, which is within sampling error (±5 pp at n = 100), so the
selection rule (cheapest config within 2 pp of the best at the same density) was
applied to the pooled value, not to the single 5 s cells as first planned.
At 60.6 peaks/s the candidates were fan_out 5 (1.97M, 0.787) and fan_out 8
(3.16M, 0.798), so fan_out 5 wins. At threshold 12 and threshold 8, min_dt 5 is
also the best min_dt for every fan_out. min_dt 2 and 3 were not run at threshold 6.

**fan_out.** At a fixed peak budget, a lower threshold with fan_out 5 beat a
higher threshold with fan_out 8. Threshold 6 / min_dt 5 / fan_out 5 scans
1.97M entries per 10 s query (pooled 0.787). Threshold 8 / min_dt 5 / fan_out 8
scans 1.76M (pooled 0.703). The old default, threshold 4 / min_dt 1 /
fan_out 5, scans 6.98M (pooled 0.737). The new default scans 3.5× fewer
entries and has 5 pp better noisy recall.

**min_dt above 5.** min_dt 8 (threshold 8, fan_out 8) was cheaper still (1.52M)
with similar pooled recall (0.717) but 2 holdout FPs in one cell. It was not explored further.

Index statistics at the defaults: 1.80M entries, 50,294 distinct hashes, mean
posting list 35.7, entry-weighted list 692 (measured 681 entries scanned per
query hash), max 5,398, f1 == f2 on 13.6% of entries.

## Unchanged, but flagged by step 1.6
- `offset_bin` = 1 and single-bin scoring: when the clip's start is near half a
  frame (frac(s/hop) ≈ 0.5), the true votes split between two adjacent bins. An
  exactly repeated section elsewhere in the same track can then win. 37 of 404
  clips with frac in (0.25, 0.75) won at the wrong repeat, against 1 of 396
  outside that range.
- `min_score` = 5 and `min_ratio` = 2.0 are calibrated against the background
  vote level. That level changes with posting-list length. See
  `docs/progress.md` for the false positives this causes.
