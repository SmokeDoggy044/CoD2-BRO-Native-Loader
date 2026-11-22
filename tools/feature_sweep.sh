#!/usr/bin/env bash
# Starts each single-player mission unattended (--level N) and collects
# the warnings and errors the port logs there: untranslated render features (texture
# modes, cube/volume textures, FVF draws, texture formats), unimplemented functions.
# Only the start of each mission is covered: the player stands still until the run
# ends (a scripted A press is a jump in the level).
#
# usage: tools/feature_sweep.sh OUT_DIR [MISSIONS] [SECONDS]
#   MISSIONS  e.g. "3 4 5" (default: 0-13, 0 = prologue)
#   SECONDS   length of each run (default 110; the level shows at ~60-70 s)
# Result: OUT_DIR/m<N>/summary.txt per mission and OUT_DIR/report.txt.
set -u
out=${1:?usage: $0 OUT_DIR [MISSIONS] [SECONDS]}
missions=${2:-"0 1 2 3 4 5 6 7 8 9 10 11 12 13"}
seconds=${3:-110}
port=$(cd "$(dirname "$0")/.." && pwd)
loader=$port/build/linux-loader/bin/cod_bro
saves=${XDG_DATA_HOME:-$HOME/.local/share}/cod_bro
names=(prologue oran tunisia kasserine_pass kasserine liberator gela piano_lupo troina
       omaha_beach mons1 eilendorf buchholz dragons_teeth)

# --level skips the menus; A every 4 s gets past the briefing ("Press A to start"),
# whose load time varies.
press="a@10"
for t in $(seq 14 4 90); do press+=",a@$t"; done

[ -x "$loader" ] || { echo "build the loader first (cmake --build --preset linux-loader)"; exit 1; }
mkdir -p "$out"
for n in $missions; do
    dir=$out/m$n
    rm -rf "$dir" && mkdir -p "$dir"
    cp -a "$saves" "$dir/data"  # the run must not touch the real saves
    echo "mission $n (${names[$n]}): ${seconds}s"
    timeout -s INT -k 15 "$seconds" "$loader" --data "$dir/data" --silent --level "$n" \
        --diag "$dir" 30 --press "$press" > "$dir/stdout.txt" 2>&1
    rm -rf "$dir/data"
done

{
    echo "Feature sweep $(date '+%F %T'), ${seconds}s per mission"
    for n in $missions; do
        echo
        echo "== mission $n (${names[$n]})"
        if [ -f "$out/m$n/summary.txt" ]; then cat "$out/m$n/summary.txt"; else echo "no summary.txt"; fi
        # The mission must have been started for the result to mean anything.
        grep -m1 -o "starting mission [0-9]*" "$out/m$n/log.txt" 2>/dev/null || echo "(mission not started)"
    done
} > "$out/report.txt"
echo "report: $out/report.txt"
