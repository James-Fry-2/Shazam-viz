#!/bin/sh
# Regenerates the phase 1 accuracy results in docs/results.
#   grid     full PLAN.md 1.6 grid at three densities, default config -> accuracy_phase1.{csv,txt}
#   window1  the same grid with --score-window 1                       -> accuracy_phase1_window1.{csv,txt}
#   window2  the same grid with --score-window 2                       -> accuracy_phase1_window2.{csv,txt}
#   detune   the same grid on data/tracks-detune.tsv                   -> accuracy_phase1_detune.{csv,txt}
#   sweep    reduced-grid parameter sweep                              -> sweep_phase1.{csv,txt}
# detune and sweep pin --score-window 1, the setting their committed results were produced with.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
acc="$root/build/fp-accuracy"
out="$root/docs/results"
make -C "$root" build/fp-accuracy >/dev/null
mkdir -p "$out"

densities="low:16 mid:10 high:6"

run_grid() {
    manifest=$1
    name=$2
    extra=${3:-}
    rm -f "$out/$name.csv" "$out/$name.txt"
    for d in $densities; do
        # shellcheck disable=SC2086
        "$acc" "$manifest" --threshold "${d#*:}" --label "${d%%:*}" $extra --out "$out/$name.csv" --append >>"$out/$name.txt"
    done
}

# label:flags; the reduced grid keeps the noisy speaker+echo cells that drive the parameter choice.
sweep_configs="
old_th4:--threshold 4 --min-dt 1 --fan-out 5 --min-magnitude-db -108
old_th6:--threshold 6 --min-dt 1 --fan-out 5 --min-magnitude-db -108
old_th8:--threshold 8 --min-dt 1 --fan-out 5 --min-magnitude-db -108
old_th12:--threshold 12 --min-dt 1 --fan-out 5 --min-magnitude-db -108
old_th16:--threshold 16 --min-dt 1 --fan-out 5 --min-magnitude-db -108
"
for th in 12 8; do
    for md in 1 2 3 5; do
        for fo in 3 5 8 10; do
            sweep_configs="${sweep_configs}th${th}_dt${md}_fo${fo}:--threshold $th --min-dt $md --fan-out $fo --min-magnitude-db -108
"
        done
    done
done
sweep_configs="$sweep_configs
th8_dt8_fo8:--threshold 8 --min-dt 8 --fan-out 8 --min-magnitude-db -108
th4_dt5_fo3:--threshold 4 --min-dt 5 --fan-out 3 --min-magnitude-db -108
th4_dt5_fo5:--threshold 4 --min-dt 5 --fan-out 5 --min-magnitude-db -108
th5_dt5_fo5:--threshold 5 --min-dt 5 --fan-out 5 --min-magnitude-db -108
th6_dt5_fo5:--threshold 6 --min-dt 5 --fan-out 5 --min-magnitude-db -108
th6_dt5_fo8:--threshold 6 --min-dt 5 --fan-out 8 --min-magnitude-db -108
th6_dt5_fo5_db-80:--threshold 6 --min-dt 5 --fan-out 5 --min-magnitude-db -80
th6_dt5_fo5_db-60:--threshold 6 --min-dt 5 --fan-out 5 --min-magnitude-db -60
th6_dt5_fo5_db-50:--threshold 6 --min-dt 5 --fan-out 5 --min-magnitude-db -50
"

case ${1:-grid} in
grid)
    run_grid "$root/data/tracks.tsv" accuracy_phase1
    ;;
window1)
    run_grid "$root/data/tracks.tsv" accuracy_phase1_window1 "--score-window 1"
    ;;
window2)
    run_grid "$root/data/tracks.tsv" accuracy_phase1_window2 "--score-window 2"
    ;;
detune)
    run_grid "$root/data/tracks-detune.tsv" accuracy_phase1_detune "--score-window 1"
    ;;
sweep)
    rm -f "$out/sweep_phase1.csv" "$out/sweep_phase1.txt"
    echo "$sweep_configs" | while IFS= read -r line; do
        [ -n "$line" ] || continue
        # shellcheck disable=SC2086
        "$acc" "$root/data/tracks.tsv" --lengths 5,10 --channels clean,speaker+echo --snrs inf,10,5,0 \
            --score-window 1 ${line#*:} --label "${line%%:*}" --out "$out/sweep_phase1.csv" --append >>"$out/sweep_phase1.txt"
    done
    ;;
*)
    echo "usage: $0 [grid|window1|window2|detune|sweep]" >&2
    exit 2
    ;;
esac
