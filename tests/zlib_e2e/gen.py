# Writes the inputs for zlib_e2e: raw data sets, zlib/raw deflate streams (with corrupted
# variants) and zip archives.
import os, random, struct, sys, zlib, zipfile, io
out = sys.argv[1]
rnd = random.Random(1234)
def text(n):
    words = [b"the", b"quick", b"brown", b"fox", b"jumps", b"over", b"lazy", b"dog", b"zlib", b"inflate", b"\n"]
    s = bytearray()
    while len(s) < n: s += rnd.choice(words) + b" "
    return bytes(s[:n])
def noise(n): return bytes(rnd.getrandbits(8) for _ in range(n))
def runs(n):
    s = bytearray()
    while len(s) < n: s += bytes([rnd.getrandbits(8)]) * rnd.randint(1, 300)
    return bytes(s[:n])
def mixed(n):
    s = bytearray()
    while len(s) < n: s += rnd.choice([text, noise, runs])(rnd.randint(1, 5000))
    return bytes(s[:n])
datas = [b"", b"a", text(100), noise(1000), runs(5000), text(70000), mixed(150000), noise(70000), bytes(66000), mixed(40000)]
with open(os.path.join(out, "data.bin"), "wb") as f:
    f.write(struct.pack("<I", len(datas)))
    for d in datas: f.write(struct.pack("<I", len(d)) + d)
cases = []
for di, d in enumerate(datas):
    for level in (0, 1, 6, 9):
        for wbits in (15, -15, 9, -9, 12):
            if wbits in (9, -9) and level == 0: continue
            c = zlib.compressobj(level, zlib.DEFLATED, wbits, 8, zlib.Z_DEFAULT_STRATEGY)
            # sync flushes in the middle make stored/empty blocks
            comp = c.compress(d[: len(d) // 2]) + c.flush(zlib.Z_SYNC_FLUSH) + c.compress(d[len(d) // 2 :]) + c.flush()
            if wbits < 0: comp += b"\0"  # the dummy byte raw inflate wants
            cases.append((abs(wbits) if wbits > 0 else -abs(wbits), d, comp))
    for strat in (zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED):
        c = zlib.compressobj(6, zlib.DEFLATED, 15, 9, strat)
        cases.append((15, d, c.compress(d) + c.flush()))
# corrupted streams: flipped bits, truncation, bad header, bad check
bad = []
for wb, d, comp in cases[:: 7]:
    if len(comp) < 8: continue
    for _ in range(4):
        b = bytearray(comp)
        i = rnd.randrange(len(b)); b[i] ^= 1 << rnd.randrange(8)
        bad.append((wb, d, bytes(b)))
    bad.append((wb, d, comp[: len(comp) // 2]))
    if wb > 0:
        bad.append((wb, d, bytes([comp[0] ^ 0x10]) + comp[1:]))
        bad.append((wb, d, comp[:-1] + bytes([comp[-1] ^ 0xff])))
        bad.append((wb, d, bytes([0x78, 0xbb]) + comp[2:]))  # preset dictionary flag
for _ in range(300):
    bad.append((-15, b"", noise(rnd.randint(1, 400))))
cases += bad
with open(os.path.join(out, "inflate.bin"), "wb") as f:
    f.write(struct.pack("<I", len(cases)))
    for wb, d, comp in cases: f.write(struct.pack("<iII", wb, len(d), len(comp)) + d + comp)
# zip archives
for zi in range(4):
    p = os.path.join(out, f"test{zi}.zip")
    with zipfile.ZipFile(p, "w") as z:
        for k in range(6 + zi * 3):
            d = rnd.choice(datas[1:])[: rnd.randint(0, 80000)]
            info = zipfile.ZipInfo(f"dir{k % 3}/File_{zi}_{k}.txt", (2004, 5, 17, 13, 45, 22))
            info.compress_type = rnd.choice([zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED])
            if k % 4 == 1: info.extra = b"\x55\x54\x05\x00\x01" + noise(4)
            if k % 5 == 2: info.comment = b"comment " + text(rnd.randint(1, 40))
            z.writestr(info, d)
        z.comment = text(50) if zi % 2 else b""
# a zip with a corrupted member (crc / data) and one truncated
with open(os.path.join(out, "test1.zip"), "rb") as f: z = bytearray(f.read())
z[200] ^= 0x40; z[5000] ^= 0x01
open(os.path.join(out, "test4.zip"), "wb").write(z)
open(os.path.join(out, "test5.zip"), "wb").write(z[: len(z) * 2 // 3])
print(len(cases), "inflate cases")
