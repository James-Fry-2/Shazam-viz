# data

Audio lives in `data/audio/` and is gitignored. Only the generator, the scripts
and the manifests are tracked.

## Synthetic tracks (CC0, generated)

```bash
data/make_synthetic.sh
```

Writes 200 indexed and 50 holdout tracks (30 s, 8 kHz, mono, 16-bit) to
`data/audio/NNNN.wav`, plus:

- `data/tracks.tsv`: `id<TAB>path<TAB>split`, with paths relative to the TSV. `split` is `indexed` or `holdout`.
- `data/tracks.json`: title, artist (`Synthetic`), licence (`CC0 (generated)`), seed and split per track.

Track `n` is generated from seed `n + 1`, so any track can be regenerated with
`build/synth <seed> <out.wav>`. `INDEXED`, `HOLDOUT` and `JOBS` override the
counts and the parallelism.

Each seed picks a tempo (80–160 BPM), a key, a major or minor scale and a
4-chord progression. The structure is verse, chorus, verse, chorus, so sections
repeat inside a track and give realistic offset ambiguity. Voices are a lead, a
pad and a bass (sums of partials under an ADSR envelope), an optional chorus
arpeggio, and kick, snare and hat noise bursts.

## Real music (FMA), planned for step 2.1

`data/fetch_fma.sh` will download `fma_metadata.zip` and `fma_small.zip` from the
URLs listed in the [FMA README](https://github.com/mdeff/fma), check their
published SHA1s, and unzip each into its own fresh directory. It will select N
tracks (default 200, seeded), read title, artist, licence and track id from
`tracks.csv`, convert with `ffmpeg -ac 1 -ar 8000 -sample_fmt s16`, mark 50 as
holdout, and write `tracks.tsv` and `tracks.json` in the same format as above.
Requires `ffmpeg` (see PLAN.md phase 0).
