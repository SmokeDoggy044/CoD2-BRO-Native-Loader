#!/usr/bin/env bash
# Quick check that a change (renamed symbols, a decompiled function) still plays:
# starts a mission unattended, presses A every 4 s to get past the movie and the
# briefing ("Press A to start"), keeps going in the level, and saves frames.
# Mission 4 by default: the A presses do not kill the player there (in mission 5
# they fire the turret and fail the mission).
#
# usage: tools/smoke_test.sh OUT_DIR [MISSION] [SECONDS] [LOADER OPTIONS...]
# Result: OUT_DIR/stdout.txt, OUT_DIR/frame_*.bmp (every 1200 frames),
# OUT_DIR/last.png (the last three frames side by side); prints the warning/error
# count and how the run ended.
set -u
out=${1:?usage: $0 OUT_DIR [MISSION] [SECONDS]}
mission=${2:-4}
seconds=${3:-130}
port=$(cd "$(dirname "$0")/.." && pwd)
loader=${LOADER:-$port/build/linux-loader/bin/cod_bro_loader}
saves=${XDG_DATA_HOME:-$HOME/.local/share}/cod_bro

press="a@10"
for t in $(seq 14 4 110); do press+=",a@$t"; done

[ -x "$loader" ] || { echo "build the loader first (cmake --build --preset linux-loader)"; exit 1; }
rm -rf "$out" && mkdir -p "$out"
cp -a "$saves" "$out/data"  # the run must not touch the real saves
timeout -s INT -k 15 "$seconds" "$loader" --data "$out/data" --silent --level "$mission" \
    --press "$press" --screenshots 1200 "$out" "${@:4}" > "$out/stdout.txt" 2>&1
status=$?
rm -rf "$out/data"

echo "exit $status (124/130 = stopped by the timeout as planned)"
echo "warnings/errors: $(grep -ciE ' (warn|error) ' "$out/stdout.txt")"
grep -iE "crash|unimplemented|exception" "$out/stdout.txt" | head -5
last=$(ls "$out"/frame_*.bmp 2>/dev/null | sort -t_ -k2 -n | tail -3)
if [ -n "$last" ] && command -v magick > /dev/null; then
    magick montage $last -tile 3x1 -geometry 427x240+2+2 "$out/last.png"
    echo "last frames: $out/last.png ($(echo "$last" | xargs -n1 basename | tr '\n' ' '))"
fi
