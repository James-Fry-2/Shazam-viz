# Phase 1 results

Every table below is generated from `docs/results/*.csv` with the awk command in the comment above it. Run each command from the repository root. Nothing is re-run and no number is typed by hand.

Default density is the `high` row (threshold 6, 60.6 peaks/s). `low` is threshold 16 (13.8 peaks/s) and `mid` is threshold 10 (33.3 peaks/s). All grid rows use min_dt 5, fan_out 5, min_magnitude_db −80. The grid is 3 densities × {3, 5, 10 s} × 4 channels × 6 SNRs with 100 indexed and 50 holdout clips per cell.

## Clean recall and right-track recall

Each cell is `recall / track recall`. Track recall counts a hit when the right track wins, whatever the offset.

<!-- awk -F, 'NR>1 && $8=="clean" && $9=="inf" {r[$1,$7]=sprintf("%.2f / %.2f",$12,$13); p[$1]=$6} END{print "| density | peaks/s | 3 s | 5 s | 10 s |"; print "|---|---|---|---|---|"; n=split("low mid high",d," "); for(i=1;i<=n;i++) printf "| %s | %.1f | %s | %s | %s |\n", d[i], p[d[i]], r[d[i],3], r[d[i],5], r[d[i],10]}' docs/results/accuracy_phase1.csv -->
| density | peaks/s | 3 s | 5 s | 10 s |
|---|---|---|---|---|
| low | 13.8 | 0.75 / 0.88 | 0.87 / 0.96 | 0.93 / 1.00 |
| mid | 33.3 | 0.88 / 0.97 | 0.96 / 1.00 | 0.98 / 1.00 |
| high | 60.6 | 0.95 / 1.00 | 0.98 / 1.00 | 0.99 / 1.00 |

## Holdout false positives

Holdout clips come from the 50 unindexed tracks. Count = holdout_fp_rate × n_holdout summed over all 24 channel × SNR cells for that length. Each length has 1,200 holdout queries per density, 3,600 in total.

<!-- awk -F, 'NR>1 {c[$1,$7]+=int($16*$11+0.5); t[$1]+=int($16*$11+0.5); if($16>m[$1]) m[$1]=$16} END{print "| density | 3 s (of 1200) | 5 s (of 1200) | 10 s (of 1200) | total (of 3600) | worst cell rate |"; print "|---|---|---|---|---|---|"; n=split("low mid high",d," "); for(i=1;i<=n;i++) printf "| %s | %d | %d | %d | %d | %.2f |\n", d[i], c[d[i],3], c[d[i],5], c[d[i],10], t[d[i]], m[d[i]]}' docs/results/accuracy_phase1.csv -->
| density | 3 s (of 1200) | 5 s (of 1200) | 10 s (of 1200) | total (of 3600) | worst cell rate |
|---|---|---|---|---|---|
| low | 7 | 7 | 8 | 22 | 0.04 |
| mid | 0 | 0 | 1 | 1 | 0.02 |
| high | 1 | 5 | 0 | 6 | 0.02 |

## Speaker + echo recall against SNR

Recall (right track and offset within one bin) for the speaker+echo channel.

<!-- awk -F, 'NR>1 && $8=="speaker+echo" {r[$1,$7,$9]=$12} END{print "| density | length | inf | 30 dB | 20 dB | 10 dB | 5 dB | 0 dB |"; print "|---|---|---|---|---|---|---|---|"; nd=split("low mid high",d," "); nl=split("3 5 10",l," "); split("inf 30 20 10 5 0",s," "); for(i=1;i<=nd;i++) for(j=1;j<=nl;j++){printf "| %s | %s s", d[i], l[j]; for(k=1;k<=6;k++) printf " | %.2f", r[d[i],l[j],s[k]]; print " |"}}' docs/results/accuracy_phase1.csv -->
| density | length | inf | 30 dB | 20 dB | 10 dB | 5 dB | 0 dB |
|---|---|---|---|---|---|---|---|
| low | 3 s | 0.18 | 0.17 | 0.17 | 0.06 | 0.00 | 0.00 |
| low | 5 s | 0.33 | 0.31 | 0.26 | 0.11 | 0.00 | 0.00 |
| low | 10 s | 0.60 | 0.58 | 0.57 | 0.26 | 0.05 | 0.01 |
| mid | 3 s | 0.30 | 0.31 | 0.34 | 0.19 | 0.12 | 0.00 |
| mid | 5 s | 0.74 | 0.73 | 0.65 | 0.47 | 0.27 | 0.02 |
| mid | 10 s | 0.86 | 0.85 | 0.87 | 0.82 | 0.61 | 0.14 |
| high | 3 s | 0.41 | 0.39 | 0.34 | 0.33 | 0.29 | 0.15 |
| high | 5 s | 0.80 | 0.76 | 0.73 | 0.70 | 0.60 | 0.38 |
| high | 10 s | 0.98 | 0.97 | 0.96 | 0.92 | 0.91 | 0.82 |

## Measured against predicted hash survival

Default density, 10 s clips. `peak` is peak survival p. `pred` is p², the hash survival expected if the two peaks of a pair survived independently. `measured` is the fraction of clean-clip hashes found again at the same time. `pair endpoints` is the fraction of pairs whose two endpoints both survived as peaks.

<!-- awk -F, 'NR>1 && $1=="high" && $7==10 {k=$8 SUBSEP $9; p[k]=$17; h[k]=$18; q[k]=$19; e[k]=$20} END{print "| channel | SNR | peak | pred (p squared) | measured | measured / pred | pair endpoints |"; print "|---|---|---|---|---|---|---|"; nc=split("clean speaker echo speaker+echo",c," "); split("inf 30 20 10 5 0",s," "); for(i=1;i<=nc;i++) for(j=1;j<=6;j++){k=c[i] SUBSEP s[j]; printf "| %s | %s | %.3f | %.3f | %.3f | %.2f | %.3f |\n", c[i], s[j], p[k], q[k], h[k], (q[k]>0?h[k]/q[k]:0), e[k]}}' docs/results/accuracy_phase1.csv -->
| channel | SNR | peak | pred (p squared) | measured | measured / pred | pair endpoints |
|---|---|---|---|---|---|---|
| clean | inf | 1.000 | 1.000 | 1.000 | 1.00 | 1.000 |
| clean | 30 | 0.972 | 0.944 | 0.938 | 0.99 | 0.940 |
| clean | 20 | 0.909 | 0.827 | 0.814 | 0.98 | 0.817 |
| clean | 10 | 0.674 | 0.454 | 0.452 | 1.00 | 0.454 |
| clean | 5 | 0.434 | 0.189 | 0.203 | 1.08 | 0.204 |
| clean | 0 | 0.169 | 0.028 | 0.038 | 1.32 | 0.038 |
| speaker | inf | 0.921 | 0.848 | 0.802 | 0.95 | 0.849 |
| speaker | 30 | 0.902 | 0.814 | 0.780 | 0.96 | 0.822 |
| speaker | 20 | 0.857 | 0.734 | 0.716 | 0.97 | 0.747 |
| speaker | 10 | 0.751 | 0.565 | 0.559 | 0.99 | 0.573 |
| speaker | 5 | 0.618 | 0.382 | 0.388 | 1.01 | 0.393 |
| speaker | 0 | 0.370 | 0.137 | 0.149 | 1.09 | 0.150 |
| echo | inf | 0.612 | 0.375 | 0.312 | 0.83 | 0.335 |
| echo | 30 | 0.608 | 0.369 | 0.309 | 0.84 | 0.330 |
| echo | 20 | 0.586 | 0.343 | 0.290 | 0.84 | 0.307 |
| echo | 10 | 0.481 | 0.231 | 0.205 | 0.89 | 0.211 |
| echo | 5 | 0.344 | 0.119 | 0.116 | 0.97 | 0.117 |
| echo | 0 | 0.162 | 0.026 | 0.031 | 1.17 | 0.031 |
| speaker+echo | inf | 0.618 | 0.382 | 0.312 | 0.82 | 0.340 |
| speaker+echo | 30 | 0.610 | 0.372 | 0.306 | 0.82 | 0.333 |
| speaker+echo | 20 | 0.583 | 0.340 | 0.284 | 0.84 | 0.306 |
| speaker+echo | 10 | 0.523 | 0.274 | 0.235 | 0.86 | 0.248 |
| speaker+echo | 5 | 0.451 | 0.203 | 0.181 | 0.89 | 0.188 |
| speaker+echo | 0 | 0.300 | 0.090 | 0.088 | 0.98 | 0.089 |

## Entries scanned per query

Clean clips, mean index entries whose hash matched a query hash.

<!-- awk -F, 'NR>1 && $8=="clean" && $9=="inf" {r[$1,$7]=$24} END{print "| density | 3 s | 5 s | 10 s |"; print "|---|---|---|---|"; n=split("low mid high",d," "); for(i=1;i<=n;i++) printf "| %s | %d | %d | %d |\n", d[i], r[d[i],3], r[d[i],5], r[d[i],10]}' docs/results/accuracy_phase1.csv -->
| density | 3 s | 5 s | 10 s |
|---|---|---|---|
| low | 22788 | 36292 | 72778 |
| mid | 178017 | 307462 | 627044 |
| high | 549062 | 954142 | 1974489 |

### Old defaults against new defaults

From the reduced grid in `sweep_phase1.csv` (5 s and 10 s clips; clean and speaker+echo; SNR inf, 10, 5, 0). `old_th4` is threshold 4, min_dt 1, floor −108 dB. `th6_dt5_fo5` is threshold 6, min_dt 5, floor −108 dB. Pooled noisy recall is the mean recall of the four speaker+echo cells with 5 s and 10 s clips at 10 dB and 5 dB SNR.

<!-- awk -F, 'NR>1 && ($1=="old_th4" || $1=="th6_dt5_fo5") { if($7==10 && $8=="clean" && $9=="inf"){e[$1]=$24; pk[$1]=$6} if($8=="speaker+echo" && ($9==10 || $9==5)){s[$1]+=$12; n[$1]++} } END{print "| config | peaks/s | entries per 10 s clean query | pooled noisy recall |"; print "|---|---|---|---|"; split("old_th4 th6_dt5_fo5",c," "); for(i=1;i<=2;i++) printf "| %s | %.1f | %d | %.3f |\n", c[i], pk[c[i]], e[c[i]], s[c[i]]/n[c[i]]}' docs/results/sweep_phase1.csv -->
| config | peaks/s | entries per 10 s clean query | pooled noisy recall |
|---|---|---|---|
| old_th4 | 82.8 | 6981925 | 0.737 |
| th6_dt5_fo5 | 60.6 | 1974582 | 0.787 |

## Detuned synthetic set

`accuracy_phase1_detune.csv` is the same grid on tracks generated with `DETUNE=1`. It is not the default dataset.

<!-- awk -F, 'FNR==1{f++; next} {ds=(f==1)?"base":"detune"; if($7==10 && $8=="clean" && $9=="inf"){cr[ds,$1]=$12; en[ds,$1]=$24} if($7==10 && $8=="speaker+echo" && $9==5) se[ds,$1]=$12; fp[ds,$1]+=int($16*$11+0.5)} END{print "| density | set | clean 10 s recall | speaker+echo 10 s at 5 dB | holdout FPs (of 3600) | entries per 10 s clean query |"; print "|---|---|---|---|---|---|"; n=split("low mid high",d," "); for(i=1;i<=n;i++){split("base detune",t," "); for(j=1;j<=2;j++) printf "| %s | %s | %.2f | %.2f | %d | %d |\n", d[i], t[j], cr[t[j],d[i]], se[t[j],d[i]], fp[t[j],d[i]], en[t[j],d[i]]}}' docs/results/accuracy_phase1.csv docs/results/accuracy_phase1_detune.csv -->
| density | set | clean 10 s recall | speaker+echo 10 s at 5 dB | holdout FPs (of 3600) | entries per 10 s clean query |
|---|---|---|---|---|---|
| low | base | 0.93 | 0.05 | 22 | 72778 |
| low | detune | 0.94 | 0.07 | 113 | 14227 |
| mid | base | 0.98 | 0.61 | 1 | 627044 |
| mid | detune | 0.98 | 0.71 | 74 | 136634 |
| high | base | 0.99 | 0.91 | 6 | 1974489 |
| high | detune | 0.99 | 0.98 | 20 | 607661 |

## Open issues from step 1.6

Copied unchanged from `docs/progress.md`. None is resolved.

- *Wrong offset at a repeated section.* All wrong-offset wins are ±11–14 s away, at the repeated verse or chorus. Single-bin scoring splits the true votes when frac(s/hop) ≈ 0.5: 37 of 404 such clips lost to the repeat, against 1 of 396 otherwise. This makes up all of the gap between recall and track recall on clean clips. Candidate fix (not made, scoring invariant): score adjacent-bin pairs, or offset_bin 2.
- *Peak test vs noise and band width.* The ratio test loses peaks quickly under broadband noise: at threshold 16, 10 dB SNR keeps 10% of peaks. A band of N bins can never pass threshold ≥ N, so the 11-bin lowest band is silent at threshold ≥ 11.
- *Cross-track false positives come from sustained notes.* Two diagnosed cases: 46 of 48 (detuned set, threshold 16) and 33 of 34 (original set, threshold 8, min_dt 8, clean version of the clip) winning votes are f1 == f2 pairs from one held pitch present in both tracks. Candidate fix (not made): drop or down-weight f1 == f2 pairs.
- *Decision rule.* `min_score` 5 accepts noise matches of 6 vs 3 on clips with only a few dozen pairs. `min_ratio × runner_up` depends on the background vote level, so it shifts with posting-list length.
- *Hash survival vs p².* Under noise, measured ≥ p² (1.0–1.3× at the default density, 2.5× at low density at 10 dB), because survival is correlated across peaks and no pairs re-form. Under speaker/echo, measured is 5–18% below p²: 6–11% of pairs whose two endpoints survive still change hash (fan-out slots taken by added peaks), and peak survival allows |Δf| ≤ 1 while hashes need exact f.
- *Monotonicity.* 13 violations in 216 rows. Most are 0.01. The largest is 0.03, where track recall also rises (echo, 10 s, mid: 0.90 at ∞, 0.93 at 30 dB), so light noise genuinely helps a few clips. Mechanism unverified.
- *min_magnitude.* The self-test's 148 "leakage" peaks came from float phase error in the test sine. Real Hann leakage peaks at −92 dB for a full-scale off-bin sine. The floor removes 0.0 peaks/s of music under the speaker channel at −80 dB.
- *Detune proposal* (`DETUNE=1 data/make_synthetic.sh`, off by default, `accuracy_phase1_detune.csv`). Distinct hashes 50k → 117k, entries scanned at the default 1.97M → 0.61M, speaker+echo 10 s @ 5 dB recall 0.91 → 0.98, **but holdout FPs rise** (6 → 20, 1 → 74, 22 → 113 of 3,600). Shorter posting lists lower the runner-up, so sustained-note pile-ups pass the ratio test. Not adopted. For review.
