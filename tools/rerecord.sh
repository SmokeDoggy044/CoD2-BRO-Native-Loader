#!/usr/bin/env bash
# Brings the missions' recordings to the current format (--rerecord), then proves each new file: replayed again,
# every frame must match. Watch it: tools/runs_status.py OUT --watch.
#   tools/rerecord.sh OUT
# Per mission: OUT/mN_convert (old recording replayed, new one written to OUT/mN_convert/NAME.rep with a copy of its
# saves) and OUT/mN_verify (the new one replayed: exit 0 = every frame matched and no replay faults; 5 = faults,
# the frames matched only by luck; 3 = frames differed); WINE=1 adds OUT/mN_verify_wine on
# the Windows build. recordings/ is not touched: install a verified file by copying NAME.rep and NAME.saves over.
# OUT/mN_verify_1x replays it again at real time (VERIFY_REAL_TIME=0 skips it): a pass at one speed can be luck.
# MISSIONS (default "0 1 2 3 4 5 6 9 11"; never 7: m7_playtest is broken), JOBS (default 3), SECS (default 1200).
set -u
out=${1:?usage: tools/rerecord.sh OUT}
root=$(cd "$(dirname "$0")/.." && pwd)
# LINUX_EXE / WINDOWS_EXE: other builds, e.g. copies, so a rebuild during a long batch changes nothing.
linux_exe=${LINUX_EXE:-$root/build/linux-loader-release/bin/cod_bro}
windows_exe=${WINDOWS_EXE:-$root/build/windows-mingw-release/bin/cod_bro.exe}
missions=${MISSIONS:-"0 1 2 3 4 5 6 9 11"}
jobs=${JOBS:-3}
mkdir -p "$out"
out=$(cd "$out" && pwd)
export WINEPREFIX=${WINEPREFIX:-$root/build/wine_test_prefix}

recording() {
    local rep
    rep=$(ls "$root/recordings/m$1/"*_playtest.rep 2>/dev/null | head -1)
    [[ -z $rep ]] && rep=$(ls "$root/recordings/m$1/"*.rep | grep -v zdev | head -1)
    echo "$rep"
}

# replay NAME MISSION REP SAVES [ARGS...]: one replay run in OUT/NAME (Linux, or Windows under Wine for *_wine).
replay() {
    local name=$1 m=$2 rep=$3 saves=$4
    shift 4
    local dir=$out/$name start
    rm -rf "$dir"
    mkdir -p "$dir/xdg"
    cp -a "$saves" "$dir/data"
    start=$SECONDS
    if [[ $name == *_wine ]]; then
        local args=() a
        for a in --data "$dir/data" --silent --level "$m" --replay-speed "${SPEED:-8}" --replay-no-draw --replay "$rep" "$@"; do
            if [[ $a == /* ]]; then args+=("Z:$a"); else args+=("$a"); fi
        done
        timeout -s INT -k 30 "${SECS:-1200}" wine "$windows_exe" "${args[@]}" > "$dir/log.txt" 2>&1
    else
        XDG_DATA_HOME=$dir/xdg timeout -s INT -k 30 "${SECS:-1200}" "$linux_exe" --data "$dir/data" --silent \
            --level "$m" --replay-speed "${SPEED:-8}" --replay-no-draw --replay "$rep" "$@" > "$dir/log.txt" 2>&1
    fi
    echo "$name exit $? seconds $((SECONDS - start))" >> "$out/exits.txt"
}

mission() {
    local m=$1 rep name new
    rep=$(recording "$m")
    name=$(basename "$rep" .rep)
    new=$out/m${m}_convert/$name.rep
    replay "m${m}_convert" "$m" "$rep" "${rep%.rep}.saves" --rerecord "$new"
    if [[ ! -s $new ]]; then return; fi
    cp -a "${rep%.rep}.saves" "${new%.rep}.saves"
    # Both verifies at once (they only read the new file; each run copies the saves).
    replay "m${m}_verify" "$m" "$new" "${new%.rep}.saves" &
    [[ ${WINE:-0} == 1 ]] && replay "m${m}_verify_wine" "$m" "$new" "${new%.rep}.saves" &
    wait
    # A second speed: a recording whose result depends on timing (sound, other title threads) can pass at one speed
    # by luck; at real time it must match too.
    [[ ${VERIFY_REAL_TIME:-1} == 1 ]] && SPEED=1 replay "m${m}_verify_1x" "$m" "$new" "${new%.rep}.saves"
}

rm -f "$out/exits.txt"
: > "$out/plan.txt"
for m in $missions; do
    rep=$(recording "$m")
    name=$(basename "$rep" .rep)
    echo "m${m}_convert $rep" >> "$out/plan.txt"
    echo "m${m}_verify $out/m${m}_convert/$name.rep" >> "$out/plan.txt"
    [[ ${VERIFY_REAL_TIME:-1} == 1 ]] && echo "m${m}_verify_1x $out/m${m}_convert/$name.rep" >> "$out/plan.txt"
    [[ ${WINE:-0} == 1 ]] && echo "m${m}_verify_wine $out/m${m}_convert/$name.rep" >> "$out/plan.txt"
done
for m in $missions; do
    while (( $(jobs -rp | wc -l) >= jobs )); do wait -n; done
    mission "$m" &
done
wait
echo "done: $(grep -c ' exit 0 ' "$out/exits.txt") of $(wc -l < "$out/exits.txt") runs exited 0 (verify runs must)"
# A source from before format 6 has no state hashes this build can compare: its conversion replays the old inputs on
# today's code and records whatever happens - self-consistent, but nothing says it is still the run that was played.
for log in "$out"/m*_convert/log.txt; do
    [ -f "$log" ] || continue
    grep -q "state not compared" "$log" &&
        echo "UNCHECKED: $(basename "$(dirname "$log")") converted an old recording (before format 6) - install only after a watched drawn replay, or record the mission again"
done
