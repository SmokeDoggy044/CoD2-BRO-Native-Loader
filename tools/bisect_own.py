#!/usr/bin/env python3
"""Finds the Compare-status function whose C breaks an own-main replay (own engine host, 0.3), by bisection
with XBCMP_ORIGINALS: each step replays RECORDING with --own-main and only a subset of the suspects as C; a step
fails when the log shows FAIL (default: a differing frame or a crash) within SECONDS.

usage: tools/bisect_own.py RECORDING OUT_DIR [--seconds 20] [--fail REGEX] [--suspects FILE] [-- OPTIONS]
Suspects: every Status::Compare row of functions.cpp (or FILE, one name per line); --always-c FILE keeps those
names as C in every step (when keeping them original fails for another reason). --dump FRAME --reference DIR:
a step fails when FRAME's state differs from a plain replay's dump in more than pool pointers (use --speed 8)."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument('recording')
ap.add_argument('out')
ap.add_argument('--seconds', type=float, default=20)
ap.add_argument('--fail', default=r'frame \d+ differs|Segmentation fault|crash  ')
ap.add_argument('--suspects')
ap.add_argument('--always-c', help='FILE: names kept as C in every step (one per line)')
ap.add_argument('--loader', default=str(PORT / 'build/linux-loader-release/bin/cod_bro_loader'))
ap.add_argument('--dump', type=int, help='FRAME: a step fails when that frame has non-pointer differences from --reference')
ap.add_argument('--reference', help='DIR with a plain replay dump of --dump FRAME (frame_<FRAME>_*.bin)')
ap.add_argument('--keep', help='REGEX: with --dump, only replay_diff lines matching it count (e.g. level/client lines)')
ap.add_argument('--speed', type=int, default=0, help='--replay-speed N for every step')
ap.add_argument('rest', nargs='*')
a = ap.parse_args()
compare = re.findall(r'\{"(\w+)", 0x[0-9a-f]{8},.*Status::Compare\}', (PORT / 'src/engine/shared/functions.cpp').read_text())
suspects = Path(a.suspects).read_text().split() if a.suspects else compare
always_c = set(Path(a.always_c).read_text().split()) if a.always_c else set()
rec = Path(a.recording).resolve()
saves = Path(str(rec)[:-4] + '.saves')
mission = subprocess.run([sys.executable, '-I', str(PORT / 'tools/replay_info.py'), str(rec)], capture_output=True,
                         text=True).stdout.split()[1]
out = Path(a.out)
out.mkdir(parents=True, exist_ok=True)
step = 0
# replay_diff lines that only differ in pool pointers (0x80xxxxxx) or pool ids (high 16 bits)
POINTERISH = re.compile(r': 8[0-3][0-9a-f]{6} \([^)]*\) / 8[0-3][0-9a-f]{6} |: address |: 00[0-9a-f]{2}0000 \([^)]*\) / 00[0-9a-f]{2}0000 ')


def fails(as_c):
    """True when the run with only `as_c` (of the suspects) as C fails."""
    global step
    step += 1
    keep = set(compare) - set(as_c) - always_c
    data = out / 'data'
    shutil.rmtree(data, ignore_errors=True)
    shutil.copytree(saves, data)
    env = dict(os.environ, XBCMP_ORIGINALS=','.join(sorted(keep)) or 'none_')
    cmd = [a.loader, '--own-main', '--data', str(data), '--silent', '--level', mission, '--replay', str(rec)] + a.rest
    if a.speed:
        cmd += ['--replay-speed', str(a.speed)]
    dump = out / f'dump_{step}'
    if a.dump:
        shutil.rmtree(dump, ignore_errors=True)
        dump.mkdir()
        cmd += ['--replay-dump', str(a.dump), str(dump)]
    log = out / f'step_{step}.txt'
    with open(log, 'w') as f:
        p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, env=env)
        end = time.time() + a.seconds
        done = dump / f'frame_{a.dump}_entities.bin' if a.dump else None
        while time.time() < end and p.poll() is None:
            if done and done.exists():  # the dump is written: the rest of the run does not matter
                time.sleep(1)
                break
            time.sleep(0.5)
        p.kill()
        p.wait()
    hit = re.search(a.fail, log.read_text(errors='replace'))
    if a.dump and not hit:
        if not (dump / f'frame_{a.dump}_level.bin').exists():
            hit = re.match('.*', 'no dump (frame not reached in time)')
        else:
            diff = subprocess.run([sys.executable, '-I', str(PORT / 'tools/replay_diff.py'), a.reference, str(dump),
                                   str(a.dump), '--limit', '100000'], capture_output=True, text=True).stdout
            real = [l for l in diff.splitlines()[1:] if not POINTERISH.search(l) and (not a.keep or re.search(a.keep, l))]
            if real:
                hit = re.match('.*', 'state differs: ' + real[0].strip())
    print(f'step {step}: {len(as_c)} as C -> {"FAIL " + hit.group(0) if hit else "ok"}', flush=True)
    return bool(hit)


if not fails(suspects):
    print('all suspects as C: no failure')
    sys.exit(0)
group = suspects
while len(group) > 1:
    half = len(group) // 2
    first, second = group[:half], group[half:]
    if fails(first):
        group = first
    elif fails(second):
        group = second
    else:
        print(f'needs both halves together ({len(group)} functions): {" ".join(group[:20])}...')
        Path(out / 'remaining.txt').write_text('\n'.join(group) + '\n')
        sys.exit(1)
print('culprit:', group[0])
