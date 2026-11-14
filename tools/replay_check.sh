#!/usr/bin/env bash
# Replays one recording (--record) in several builds and compares the results: each build gets a fresh copy
# of the save folder the recording was made from, runs --silent --replay --replay-continue (so a crash just
# after the recording's end still shows), and is stopped 20 s after the recording's length.
#
# usage: tools/replay_check.sh RECORDING OUT_DIR [NAME=LOADER ...]
#    or: tools/replay_check.sh RECORDING SAVES MISSION OUT_DIR [NAME=LOADER ...]
#   SAVES: a copy of the save folder as it was when the recording started (never the real one); default:
#   <recording without .rep>.saves/, else saves/ next to the recording, or SAVES=... in the environment
#   MISSION: default the recording's own --level (none = it started at the menus), or MISSION=...
#   default builds: release, debug (Linux) and windows (MinGW release under Wine, build/wine_wrap.sh)
#   SHOTS=600,1800,... (environment): each build also saves those frames' pictures to OUT_DIR/<name>.shots/
#   (--replay-shots), and tools/shot_compare.py compares them with the first build's; SHOT_MAX_SHARE (default
#   0.05) = percent of pixels allowed to differ (Wine's back end differs on ~0.01%)
#   EXTRA="..." (environment): more loader options for every build, e.g. EXTRA=--replay-no-draw (game state only,
#   about twice as fast; the window stays black)
#   KEEP_DATA=1 (environment): keep each build's save folder (OUT_DIR/<name>.data) to compare the files it wrote
#   SPEED=N (environment): --replay-speed N (sound and vertical blank N times faster; ~2.8x is this PC's limit on
#   mission 2, draw calls); the runs then exit at the recording's end (no --replay-continue)
# Result: one line per build; OUT_DIR/<name>.txt holds each run's log. Exit code 1 if any build did not match
# every frame (or its pictures differ).
set -u
usage="usage: $0 RECORDING OUT_DIR [NAME=LOADER ...]  (or RECORDING SAVES MISSION OUT_DIR ...)"
rec=${1:?$usage}
if [ $# -ge 4 ] && [[ $3 =~ ^[0-9]+$ ]] && [[ $2 != *=* ]]; then
    saves=$2 MISSION=$3 out=$4
    shift 4
else
    out=${2:?$usage}
    saves=${SAVES:-${rec%.rep}.saves}
    [ -n "${SAVES:-}" ] || [ -d "$saves" ] || saves=$(dirname "$rec")/saves
    shift 2
fi
[ -d "$saves" ] || { echo "no save folder $saves (SAVES=...)"; exit 2; }
port=$(cd "$(dirname "$0")/.." && pwd)
builds=("$@")
[ ${#builds[@]} -eq 0 ] && builds=(
    "release=$port/build/linux-loader-release/bin/cod_bro_loader"
    "debug=$port/build/linux-loader/bin/cod_bro_loader"
    "windows=$port/build/wine_wrap.sh")
rec=$(realpath "$rec")
saves=$(realpath "$saves")
mkdir -p "$out"
out=$(realpath "$out")
read -r frames recorded_mission < <(python3 -I "$port/tools/replay_info.py" "$rec") || exit 2
mission=${MISSION:-$recorded_mission}
level=()
[ "$mission" != "-" ] && level=(--level "$mission")
seconds=$((frames / 50 + 20))  # the game runs at 60; slower builds (debug, Wine) get some room
echo "recording: $frames frames, mission $mission; each run stops after ${seconds} s"
run() {
    local name=$1 loader=$2 data="$out/$1.data"
    rm -rf "$data" && cp -a "$saves" "$data"
    local pace=(--replay-continue)
    [ -n "${SPEED:-}" ] && pace=(--replay-speed "$SPEED")
    timeout -s INT -k 15 "$seconds" "$loader" --data "$data" --silent "${level[@]}" \
        --replay "$rec" "${pace[@]}" ${EXTRA:-} ${SHOTS:+--replay-shots "$SHOTS" "$out/$name.shots"} \
        > "$out/$name.txt" 2>&1
    echo $? > "$out/$name.exit"
    [ -n "${KEEP_DATA:-}" ] || rm -rf "$data"
}
for b in "${builds[@]}"; do
    name=${b%%=*} loader=${b#*=}
    [ -x "$loader" ] || { echo "$name: $loader is missing (build it first)"; continue; }
    run "$name" "$loader" &
done
wait
failed=0
printf '%-10s %-8s %s\n' build exit result
for b in "${builds[@]}"; do
    name=${b%%=*}
    [ -f "$out/$name.exit" ] || continue
    log="$out/$name.txt"
    result=$(grep -a -m1 -oE '(all [0-9]+ frames matched|[0-9]+ of [0-9]+ frames differed, the first at frame [0-9]+)' "$log")
    crash=$(grep -a -m1 -oE 'Segmentation fault at .*|unimplemented .*' "$log")
    [ -z "$result" ] && result="no replay result (see $log)"
    [[ $result == all*matched && -z $crash ]] || failed=1
    printf '%-10s %-8s %s%s\n' "$name" "$(cat "$out/$name.exit")" "$result" "${crash:+; then CRASH: $crash}"
    rm -f "$out/$name.exit"
done
if [ -n "${SHOTS:-}" ]; then
    first=${builds[0]%%=*}
    for b in "${builds[@]:1}"; do
        name=${b%%=*}
        echo "pictures, $first vs $name:"
        python3 -I "$port/tools/shot_compare.py" "$out/$first.shots" "$out/$name.shots" \
            --max-share "${SHOT_MAX_SHARE:-0.05}" && echo "  ok" || failed=1
    done
fi
exit $failed
