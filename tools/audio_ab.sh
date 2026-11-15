#!/usr/bin/env bash
# Sound A/B over one recording (engine improvements phase 3): each variant replays it with --audio-capture (silent,
# no drawing, SPEED times faster), all in parallel; then tools/audio_compare.py compares them (first = reference).
#
#   tools/audio_ab.sh OUT RECORDING.rep NAME="[LOADER::]OPTIONS" [NAME="..."] ...
#
# LOADER defaults to the release build; e.g. ref="build/lab_reference/p30/cod_bro_loader::" new="" off="--occlusion off".
# The recording's saves (RECORDING.saves) are copied per variant. Environment: SPEED (default 4), EXTRA (options for
# every variant), TIMEOUT (seconds, default from the recording's length).
set -u
port=$(cd "$(dirname "$0")/.." && pwd)
out=$1 rec=$(realpath "$2")
shift 2
mkdir -p "$out" && out=$(realpath "$out")
saves=${rec%.rep}.saves
read -r frames mission < <(python3 -I "$port/tools/replay_info.py" "$rec") || exit 2
speed=${SPEED:-4}
seconds=${TIMEOUT:-$((frames / 60 / ${speed%.*} + 60))}
level=()
[ "$mission" != "-" ] && level=(--level "$mission")
specs=()
for v in "$@"; do
    name=${v%%=*} spec=${v#*=}
    loader="$port/build/linux-loader-release/bin/cod_bro_loader" opts=$spec
    if [[ $spec == *::* ]]; then loader=${spec%%::*}; opts=${spec#*::}; fi
    [[ $loader != /* ]] && loader="$port/$loader"
    (
        data="$out/$name.data"
        rm -rf "$data" && cp -a "$saves" "$data"
        # shellcheck disable=SC2086
        timeout -s INT -k 15 "$seconds" "$loader" --data "$data" "${level[@]}" --replay "$rec" --replay-no-draw \
            --replay-speed "$speed" --audio-capture "$out/$name.wav" ${EXTRA:-} $opts > "$out/$name.txt" 2>&1
        echo "$name: exit $?, $(grep -o 'all [0-9]* frames matched\|frame [0-9]* differs.*' "$out/$name.txt" | head -1)"
        rm -rf "$data"
    ) &
    specs+=("$name=$out/$name.wav")
done
wait
python3 "$port/tools/audio_compare.py" --out "$out/compare" "${specs[@]}"
