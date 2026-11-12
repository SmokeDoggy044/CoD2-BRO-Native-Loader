#!/usr/bin/env bash
# tools/np_scan.sh MISSION RANGE TAG - plain vs own-main dumps of recordings/mN/mN_playtest.rep over RANGE (N-M[:S]),
# then the first dumped frame with non-pointer differences
P=$(cd "$(dirname "$0")/.." && pwd); cd $P; m=$1; R=$2; T=$3
run(){ O=$P/build/trap/np_${T}_$1; rm -rf $O; mkdir -p $O; cp -a recordings/m$m/m${m}_playtest.saves $O/data; timeout -s INT ${SECS:-200} build/linux-loader-release/bin/cod_bro_loader $2 --replay-no-draw --data $O/data --silent --level $m --replay-speed 8 --replay recordings/m$m/m${m}_playtest.rep --replay-dump $R $O > $O/log.txt 2>&1; }
run plain "" & run own --own-main & wait
for f in $(ls build/trap/np_${T}_own | sed -n 's/frame_\([0-9]*\)_level.bin/\1/p' | sort -n); do
  [ -f build/trap/np_${T}_plain/frame_${f}_level.bin ] || continue
  d=$(python3 tools/replay_diff.py build/trap/np_${T}_plain build/trap/np_${T}_own $f --limit 100000 | tail -n +2 | { if [ "${FILTER:-}" = lc ]; then grep -E '^  (level|client)|^  entity [0-9]+ \+0x(2c|30|34):'; else cat; fi; } | grep -v -E ': 8[0-3][0-9a-f]{6} \([^)]*\) / 8[0-3][0-9a-f]{6} |: address |: 00[0-9a-f]{2}0000 \([^)]*\) / 00[0-9a-f]{2}0000 ')
  if [ -n "$d" ]; then echo "first non-pointer difference at dumped frame $f (previous dump clean)"; echo "$d" | head -${LINES_OUT:-20}; exit 0; fi
  last=$f
done
echo "no non-pointer differences through frame $last"
