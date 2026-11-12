#!/usr/bin/env bash
# Checks every reference recording in recordings/ (one per level: recordings/<mission>/*.rep + saves/) with
# tools/replay_check.sh - each in the default builds (Linux release, debug, Windows under Wine), one after another.
#
# usage: tools/replay_all.sh OUT_DIR [FILTER]
#   FILTER: only recordings whose path contains it (e.g. m4/)
#   SHOTS=... / SHOT_MAX_SHARE=... / EXTRA=... are passed on (pictures per build, see replay_check.sh)
# Result: replay_check's lines per recording, then one summary line each; OUT_DIR/<mission>_<name>/ holds the logs.
# Exit code 1 if any recording did not match in every build.
set -u
out=${1:?usage: $0 OUT_DIR [FILTER]}
filter=${2:-}
port=$(cd "$(dirname "$0")/.." && pwd)
mapfile -t recs < <(find "$port/recordings" -mindepth 2 -maxdepth 2 -name '*.rep' | sort)
[ ${#recs[@]} -eq 0 ] && { echo "no recordings in $port/recordings"; exit 2; }
mkdir -p "$out"
summary=() failed=0
for rec in "${recs[@]}"; do
    [[ -n $filter && $rec != *"$filter"* ]] && continue
    rel=${rec#"$port/recordings/"}
    dir="$out/$(echo "${rel%.rep}" | tr / _)"
    echo "== $rel"
    if "$port/tools/replay_check.sh" "$rec" "$dir"; then
        summary+=("ok      $rel")
    else
        summary+=("FAILED  $rel  (see $dir)")
        failed=1
    fi
done
echo "== summary"
printf '%s\n' "${summary[@]}"
exit $failed
