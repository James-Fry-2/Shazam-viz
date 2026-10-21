#!/bin/sh
# Writes 200 indexed + 50 holdout synthetic tracks, data/tracks.tsv and data/tracks.json.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
indexed=${INDEXED:-200}
holdout=${HOLDOUT:-50}
jobs=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}

make -C "$root" build/synth >/dev/null
synth="$root/build/synth"
audio="$here/audio"
mkdir -p "$audio"

total=$((indexed + holdout))
# Track id n uses seed n+1, so a track is reproducible from its id alone.
seq 0 $((total - 1)) | xargs -P "$jobs" -I{} sh -c \
    'id=$1; "$2" $((id + 1)) "$3/$(printf %04d "$id").wav" >/dev/null' _ {} "$synth" "$audio"

tsv="$here/tracks.tsv"
json="$here/tracks.json"
printf 'id\tpath\tsplit\n' >"$tsv"
printf '[\n' >"$json"
id=0
while [ "$id" -lt "$total" ]; do
    name=$(printf %04d "$id")
    if [ "$id" -lt "$indexed" ]; then split=indexed; else split=holdout; fi
    printf '%d\taudio/%s.wav\t%s\n' "$id" "$name" "$split" >>"$tsv"
    sep=,
    [ "$id" -eq $((total - 1)) ] && sep=
    printf '  {"id": %d, "title": "Synthetic Track %s", "artist": "Synthetic", "licence": "CC0 (generated)", "seed": %d, "split": "%s"}%s\n' \
        "$id" "$name" $((id + 1)) "$split" "$sep" >>"$json"
    id=$((id + 1))
done
printf ']\n' >>"$json"
echo "wrote $total tracks ($indexed indexed, $holdout holdout) to $audio"
