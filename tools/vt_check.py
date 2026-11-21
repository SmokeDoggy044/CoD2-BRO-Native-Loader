#!/usr/bin/env python3
"""VirusTotal results of a release's Windows files, looked up by SHA-256 (nothing is uploaded).

  vt_check.py [--rescan] FILE_OR_ZIP...   every .exe and .dll given, or inside each .zip given

--rescan asks VirusTotal to scan again the files it already has (a vendor's fix shows only in a new scan;
nothing is uploaded), then waits for the results.

Prints, per file, the detections (malicious + suspicious) of the latest analysis, the vendors that flag it and
their labels, or "not on VirusTotal" for a file nobody has uploaded yet. Exit code 1 when any file is flagged.

The API key comes from the VT_API_KEY environment variable or ~/.config/vt_api_key (one line, the 64-character
key); it is never printed. The free key allows 4 requests a minute, so lookups are spaced 15 s apart.
"""
import datetime
import hashlib
import json
import os
import pathlib
import sys
import time
import urllib.error
import urllib.request
import zipfile

API = 'https://www.virustotal.com/api/v3/files/'
INTERVAL = 15.0  # seconds between requests (free API: 4 a minute)


def api_key():
    key = os.environ.get('VT_API_KEY', '').strip()
    if not key:
        path = pathlib.Path.home() / '.config' / 'vt_api_key'
        if path.exists():
            key = path.read_text().strip()
    if len(key) != 64:
        sys.exit('no VirusTotal API key: set VT_API_KEY or write it to ~/.config/vt_api_key')
    return key


def windows_files(paths):
    """(name, bytes) of every .exe/.dll given or inside a given .zip."""
    for p in map(pathlib.Path, paths):
        if p.suffix.lower() == '.zip':
            with zipfile.ZipFile(p) as z:
                for info in z.infolist():
                    if info.filename.lower().endswith(('.exe', '.dll')):
                        yield info.filename, z.read(info)
        elif p.suffix.lower() in ('.exe', '.dll'):
            yield str(p), p.read_bytes()


def lookup(key, sha256):
    request = urllib.request.Request(API + sha256, headers={'x-apikey': key})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise


def rescan(key, sha256):
    request = urllib.request.Request(API + sha256 + '/analyse', data=b'', method='POST',
                                     headers={'x-apikey': key})
    try:
        with urllib.request.urlopen(request, timeout=30):
            return True
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return False
        raise


def main(argv):
    again = '--rescan' in argv
    argv = [a for a in argv if a != '--rescan']
    if not argv:
        print(__doc__)
        return 2
    key = api_key()
    flagged = False
    last = 0.0
    if again:
        for name, data in windows_files(argv):
            time.sleep(max(0.0, last + INTERVAL - time.monotonic()))
            last = time.monotonic()
            print(f'{name}: rescan {"asked" if rescan(key, hashlib.sha256(data).hexdigest()) else "skipped (not on VirusTotal)"}')
        print('waiting 3 minutes for the scans...')
        time.sleep(180)
    for name, data in windows_files(argv):
        sha256 = hashlib.sha256(data).hexdigest()
        time.sleep(max(0.0, last + INTERVAL - time.monotonic()))
        last = time.monotonic()
        result = lookup(key, sha256)
        if result is None:
            print(f'{name}: not on VirusTotal ({sha256[:16]})')
            continue
        a = result['data']['attributes']
        stats = a.get('last_analysis_stats', {})
        bad = stats.get('malicious', 0) + stats.get('suspicious', 0)
        total = sum(stats.get(k, 0) for k in ('malicious', 'suspicious', 'undetected', 'harmless'))
        when = datetime.datetime.fromtimestamp(a.get('last_analysis_date', 0), datetime.timezone.utc)
        print(f'{name}: {bad}/{total} flag it (analysed {when:%Y-%m-%d %H:%M} UTC, {sha256[:16]})')
        for engine, r in sorted(a.get('last_analysis_results', {}).items()):
            if r.get('category') in ('malicious', 'suspicious'):
                print(f'    {engine}: {r.get("result")}')
        flagged = flagged or bad > 0
    return 1 if flagged else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
