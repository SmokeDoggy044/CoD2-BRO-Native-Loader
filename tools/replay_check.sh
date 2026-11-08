#!/usr/bin/env bash
# Replays one recording (--record) in several builds and compares the results: each build gets a fresh copy
# of the save folder the recording was made from, runs --silent --replay --replay-continue (so a crash just
# after the recording's end still shows), and is stopped 20 s after the recording's length.
#
# usage: tools/replay_check.sh RECORDING SAVES MISSION OUT_DIR [NAME=LOADER ...]
#   SAVES: a copy of the save folder as it was when the recording started (never the real one)
#   default builds: release, debug (Linux) and windows (MinGW release under Wine, build/wine_wrap.sh)
#   SHOTS=600,1800,... (environment): each build also saves those frames' pictures to OUT_DIR/<name>.shots/
#   (--replay-shots), and tools/shot_compare.py compares them with the first build's
# Result: one line per build; OUT_DIR/<name>.txt holds each run's log.
set -u
rec=${1:?usage: $0 RECORDING SAVES MISSION OUT_DIR [NAME=LOADER ...]}
saves=${2:?}
mission=${3:?}
out=${4:?}
shift 4
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
frames=$(python3 -I - "$rec" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
i, n = 12, 0
while i + 3 <= len(d):
    if d[i] == ord("F"):
        n += 1
    i += 3 + struct.unpack_from("<H", d, i + 1)[0]
print(n)
EOF
)
seconds=$((frames / 50 + 20))  # the game runs at 60; slower builds (debug, Wine) get some room
echo "recording: $frames frames; each run stops after ${seconds} s"
run() {
    local name=$1 loader=$2 data="$out/$1.data"
    rm -rf "$data" && cp -a "$saves" "$data"
    timeout -s INT -k 15 "$seconds" "$loader" --data "$data" --silent --level "$mission" \
        --replay "$rec" --replay-continue ${SHOTS:+--replay-shots "$SHOTS" "$out/$name.shots"} \
        > "$out/$name.txt" 2>&1
    echo $? > "$out/$name.exit"
    rm -rf "$data"
}
for b in "${builds[@]}"; do
    name=${b%%=*} loader=${b#*=}
    [ -x "$loader" ] || { echo "$name: $loader is missing (build it first)"; continue; }
    run "$name" "$loader" &
done
wait
printf '%-10s %-8s %s\n' build exit result
for b in "${builds[@]}"; do
    name=${b%%=*}
    [ -f "$out/$name.exit" ] || continue
    log="$out/$name.txt"
    result=$(grep -m1 -oE '(all [0-9]+ frames matched|[0-9]+ of [0-9]+ frames differed, the first at frame [0-9]+)' "$log")
    crash=$(grep -m1 -oE 'Segmentation fault at .*|unimplemented .*' "$log")
    [ -z "$result" ] && result="no replay result (see $log)"
    printf '%-10s %-8s %s%s\n' "$name" "$(cat "$out/$name.exit")" "$result" "${crash:+; then CRASH: $crash}"
    rm -f "$out/$name.exit"
done
if [ -n "${SHOTS:-}" ]; then
    first=${builds[0]%%=*}
    for b in "${builds[@]:1}"; do
        name=${b%%=*}
        echo "pictures, $first vs $name:"
        python3 -I "$port/tools/shot_compare.py" "$out/$first.shots" "$out/$name.shots"
    done
fi
