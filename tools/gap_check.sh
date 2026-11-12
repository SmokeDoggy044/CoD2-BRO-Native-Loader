#!/usr/bin/env bash
# gap_check.sh LOADER OUT [BASELINE_OUT] - the per-batch check for 0.3 gap-function work.
# Replays m4_smoke, m2_playtest, m0_playtest and m1_playtest with --own-main --text-clear (speed 8, no drawing), dumps game state
# (m4 frame 2600, m2 25900, m0 28300, m1 57300), prints whether each replay reached its end, the spots of original code
# still run (union of both, OUT/spots.txt), and - with BASELINE_OUT - whether the dumps are byte-identical
# to the baseline's and which spots went away / are new.
# Runs are serialized across worktrees with flock on cod_bro/wt/compare.lock.
set -u
loader=$(readlink -f "$1"); out=$(readlink -m "$2"); base=${3:+$(readlink -m "$3")}
rec=/mnt/ms1/Projects/Decomps/Xbox/cod_bro/decomp/recordings
lock=/mnt/ms1/Projects/Decomps/Xbox/cod_bro/wt/compare.lock
rm -rf "$out"; mkdir -p "$out"
run() {  # name level recording frame seconds
    local d=$out/$1; mkdir -p "$d/dump"; cp -a "$rec/$1/$3.saves" "$d/data"
    flock "$lock" timeout -s INT -k 15 "$5" "$loader" --own-main --text-clear "$d/clear.txt" --data "$d/data" \
        --silent --level "$2" --replay-speed 8 --replay-no-draw --replay "$rec/$1/$3.rep" \
        --replay-dump "$4" "$d/dump" > "$d/log.txt" 2>&1
    local rc=$?
    local end; end=$(grep -m1 -o "recording ends at frame [0-9]*" "$d/log.txt")
    echo "$1: exit $rc, ${end:-DID NOT REACH THE END (see $d/log.txt)}, dump files $(ls "$d/dump" | wc -l)"
}
run m4 4 m4_smoke 2600 240
run m2 2 m2_playtest 25900 600
run m0 0 m0_playtest 28300 600
run m1 1 m1_playtest 57300 900
cat "$out"/m*/clear.txt 2>/dev/null | awk '{print $1}' | sort -u > "$out/spots.txt"
echo "spots still run: $(wc -l < "$out/spots.txt")"
if [ -n "$base" ]; then
    for m in m4 m2 m0 m1; do
        if diff -rq "$base/$m/dump" "$out/$m/dump" > /dev/null; then echo "$m dump: identical to baseline"
        else echo "$m dump: DIFFERS from baseline ($(diff -rq "$base/$m/dump" "$out/$m/dump" | wc -l) files)"; fi
    done
    echo "spots gone: $(comm -23 "$base/spots.txt" "$out/spots.txt" | wc -l), new: $(comm -13 "$base/spots.txt" "$out/spots.txt" | tr '\n' ' ')"
fi
