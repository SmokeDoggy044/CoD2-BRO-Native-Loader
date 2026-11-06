#!/usr/bin/env python3
"""hashdict.py [build|HASH...]: reverse Com_HashKeyNoCase over all strings in default.xbe + sp/ files."""
import os, re, sys, pickle
D = os.path.dirname(os.path.abspath(__file__))
P = os.path.join(os.path.dirname(os.path.dirname(D)), "build", "hashes.pkl")  # 139 MB, build/ is not committed
U = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(D))), "unpacked") + "/"


def h(s):
    v = 0
    for c in s.lower().encode("latin-1"):
        if c >= 0x80: c -= 0x100
        v = (((v << 5) | (v >> 27)) & 0xffffffff) ^ (c & 0xffffffff)
    return v


def build():
    words = set()
    files = [U + "default.xbe"]
    for root, _, fs in os.walk(U + "sp"):
        files += [os.path.join(root, f) for f in fs]
    for f in files:
        try:
            data = open(f, "rb").read()
        except OSError:
            continue
        for m in re.finditer(rb"[\x20-\x7e]{2,200}", data):
            s = m.group().decode()
            words.add(s)
            for t in re.split(r"[^A-Za-z0-9_#/\.\-]", s):
                if t: words.add(t)
    d = {}
    for w in words:
        d.setdefault(h(w), set()).add(w)
    pickle.dump(d, open(P, "wb"))
    print(len(d))


if sys.argv[1] == "build":
    build()
else:
    d = pickle.load(open(P, "rb"))
    for a in sys.argv[1:]:
        v = int(a, 16)
        print(f"0x{v:08x}", sorted(d.get(v, []), key=len)[:3])
