#!/usr/bin/env python3
"""Finds the Compare-status function whose C breaks an own-main replay (own engine host, 0.3), by bisection
with XBCMP_ORIGINALS: each step replays RECORDING with --own-main and only a subset of the suspects as C; a step
fails when the log shows FAIL (default: a differing frame or a crash) within SECONDS.

usage: tools/bisect_own.py RECORDING OUT_DIR [--seconds 20] [--fail REGEX] [--suspects FILE] [-- OPTIONS]
Suspects: every Status::Compare row of functions.cpp (or FILE, one name per line)."""
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
ap.add_argument('--loader', default=str(PORT / 'build/linux-loader-release/bin/cod_bro_loader'))
ap.add_argument('rest', nargs='*')
a = ap.parse_args()
compare = re.findall(r'\{"(\w+)", 0x[0-9a-f]{8},.*Status::Compare\}', (PORT / 'src/engine/shared/functions.cpp').read_text())
suspects = Path(a.suspects).read_text().split() if a.suspects else compare
rec = Path(a.recording).resolve()
saves = Path(str(rec)[:-4] + '.saves')
mission = subprocess.run([sys.executable, '-I', str(PORT / 'tools/replay_info.py'), str(rec)], capture_output=True,
                         text=True).stdout.split()[1]
out = Path(a.out)
out.mkdir(parents=True, exist_ok=True)
step = 0


def fails(as_c):
    """True when the run with only `as_c` (of the suspects) as C fails."""
    global step
    step += 1
    keep = set(compare) - set(as_c)
    data = out / 'data'
    shutil.rmtree(data, ignore_errors=True)
    shutil.copytree(saves, data)
    env = dict(os.environ, XBCMP_ORIGINALS=','.join(sorted(keep)) or 'none_')
    cmd = [a.loader, '--own-main', '--data', str(data), '--silent', '--level', mission, '--replay', str(rec)] + a.rest
    log = out / f'step_{step}.txt'
    with open(log, 'w') as f:
        p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, env=env)
        end = time.time() + a.seconds
        while time.time() < end and p.poll() is None:
            time.sleep(0.5)
        p.kill()
        p.wait()
    hit = re.search(a.fail, log.read_text(errors='replace'))
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
