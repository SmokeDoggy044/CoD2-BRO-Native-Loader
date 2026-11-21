#!/usr/bin/env bash
# Memory pools over whole missions: replays each mission's playtest recording on the own exe with --mem-stats
# (speed 8), then tools/mem_sweep_sum.py puts the peaks side by side. Watch it: tools/runs_status.py OUT --watch.
#   tools/mem_sweep.sh OUT [EXTRA_ARGS...]     e.g. tools/mem_sweep.sh build/memsweep --memory 64
# MISSIONS (default "0 1 2 3 4 5 6 9 11"; never 7: m7_playtest is broken), JOBS (default 3), SECS (default 900),
# EXE (default the Linux release own exe; a .exe runs under Wine), DRAW=1 draws (a window; default no drawing),
# MODS=DIR texture packs (--mods-dir), SPEED (default 8). Settings stay in OUT (XDG_DATA_HOME; Wine: build/wine_test_prefix), saves
# are copies.
set -u
out=${1:?usage: tools/mem_sweep.sh OUT [EXTRA_ARGS...]}
shift
root=$(cd "$(dirname "$0")/.." && pwd)
exe=${EXE:-$root/build/linux-loader-release/bin/cod_bro}
missions=${MISSIONS:-"0 1 2 3 4 5 6 9 11"}
jobs=${JOBS:-3}
mkdir -p "$out"
out=$(cd "$out" && pwd)

recording() {
    local rep
    rep=$(ls "$root/recordings/m$1/"*_playtest.rep 2>/dev/null | head -1)
    [[ -z $rep ]] && rep=$(ls "$root/recordings/m$1/"*.rep | grep -v zdev | head -1)
    echo "$rep"
}

# Wine runs get a prefix of their own (never the player's ~/.wine: the game keeps settings.ini in its AppData).
if [[ $exe == *.exe ]]; then
    export WINEPREFIX=${WINEPREFIX:-$root/build/wine_test_prefix}
    [[ -d $WINEPREFIX ]] || WINEDEBUG=-all wineboot -i > /dev/null 2>&1
fi

# Wine wants Windows paths: absolute paths get the Z: drive.
command_for() {
    if [[ $exe == *.exe ]]; then
        local a args=()
        for a in "$@"; do if [[ $a == /* ]]; then args+=("Z:$a"); else args+=("$a"); fi; done
        echo wine "$exe" "${args[@]}"
    else
        echo "$exe" "$@"
    fi
}

run() {
    local m=$1; shift
    local rep name dir start
    rep=$(recording "$m")
    name=$(basename "$rep" .rep)
    dir=$out/$name
    rm -rf "$dir"
    mkdir -p "$dir/xdg"
    cp -a "${rep%.rep}.saves" "$dir/data"
    local args=(--data "$dir/data" --silent --level "$m" --replay-speed "${SPEED:-8}" --replay "$rep" --mem-stats "$dir/memory.txt")
    [[ ${DRAW:-0} == 1 ]] || args+=(--replay-no-draw)
    [[ -n ${MODS:-} ]] && args+=(--mods-dir "$MODS")
    start=$SECONDS
    # shellcheck disable=SC2046
    XDG_DATA_HOME=$dir/xdg timeout -s INT -k 30 "${SECS:-900}" $(command_for "${args[@]}" "$@") \
        > "$dir/log.txt" 2>&1
    echo "$name exit $? seconds $((SECONDS - start))" >> "$out/exits.txt"
}

rm -f "$out/exits.txt" "$out/summary.txt"
: > "$out/plan.txt"
for m in $missions; do
    rep=$(recording "$m")
    echo "$(basename "$rep" .rep) $rep" >> "$out/plan.txt"
done
for m in $missions; do
    while (( $(jobs -rp | wc -l) >= jobs )); do wait -n; done
    run "$m" "$@" &
done
wait
python3 -I "$root/tools/mem_sweep_sum.py" "$out" | tee "$out/summary.txt"
