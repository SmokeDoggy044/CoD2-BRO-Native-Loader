"""Inventory of the game's fixed limits: what is sized once, and where it stops.

Four scans over our C (src/engine) and the original image:
  message  the original's own limit messages ("too many", "out of", "full", "exceeded", ...) in .rdata, with the
           functions that use them (our C: title_text/tstr/literal; the image: push imm32 sites)
  alloc    allocations with a constant size (Z_Malloc, Heap_AllocAligned, XPhysicalAlloc, Heap_CreateFromZone,
           boot_construct, small heap pool tables)
  array    title globals walked by a loop with a constant bound: base + i * stride, i < count
  cap      a title global compared against a constant (a count that stops at a fixed number)
Each row says whether level code (x86 from the dump) reaches the address (docs/formats/level_boundary.md).

usage: python3 -I tools/limits_scan.py [--out tools/data/limits.csv]
"""
import argparse
import csv
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from fnpack import Image, load_names  # noqa: E402

SRC = ROOT / "src" / "engine"
BOUNDARY = ROOT / "docs" / "formats" / "level_boundary.md"

MESSAGE = re.compile(r"too many|exceed|overflow|out of (memory|space|slots)|ran out|no (free|more)|is full|"
                     r"\bfull\b|pool empty|max(imum)? of|MAX_\w+|limit|can'?t alloc|cannot alloc|"
                     r"failed to alloc|not enough", re.I)
NUM = r"(0x[0-9a-fA-F]+|\d+)"
GLOBAL = r"0x00[89][0-9a-fA-F]{5}"
FUNC_DEF = re.compile(r"^[A-Za-z_][\w:<>,*&\s]*?\b([A-Za-z_]\w*)\s*\([^;]*\)\s*(const\s*)?\{\s*$")
ALLOC = [
    ("Z_Malloc", re.compile(r"\bZ_Malloc\(\s*" + NUM + r"\s*\)")),
    ("Heap_AllocAligned", re.compile(r"Heap_AllocAligned\)?\(\s*" + NUM + r"\s*,\s*" + NUM + r"\s*\)")),
    ("Heap_CreateFromZone", re.compile(r"Heap_CreateFromZone\([^,]+,\s*" + NUM + r"\s*\)")),
    ("XPhysicalAlloc", re.compile(r"physical_alloc\(\s*" + NUM)),
    ("boot_construct", re.compile(r"boot_construct\(\s*(" + GLOBAL + r")\s*,\s*" + NUM + r"\s*,\s*" + NUM)),
    # {size, align, count} rows of a kPools table (SmallHeap_Init configs); {count, size} rows for apsAddPool
    ("pool table", re.compile(r"\{\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*(?:,\s*(0x[0-9a-fA-F]+|\d+)\s*)?\}")),
    ("apsAddPool", re.compile(r"apsAddPool\(\s*" + NUM + r"\s*,\s*" + NUM + r"\s*\)")),
]
LOOP = re.compile(r"for\s*\([^;]*;\s*\w+\s*<\s*(?:\w+\()?" + NUM + r"\)?\s*;")
INDEXED = re.compile(r"(" + GLOBAL + r")\s*\+\s*(?:\w+\()?\w+\)?\s*\*\s*" + NUM)
CAP = re.compile(r"title_global<u?int\d+_t>\((" + GLOBAL + r")\)\s*(>=|<|>|<=)\s*" + NUM)


def number(text):
    return int(text, 0)


def level_addresses():
    found = set()
    if BOUNDARY.exists():
        for m in re.finditer(r"\|\s*([0-9a-f]{8})\s*\|", BOUNDARY.read_text()):
            found.add(int(m.group(1), 16))
    return found


def source_files():
    for path in sorted(SRC.rglob("*.cpp")):
        if "levels" in path.parts or "levelscript" in path.parts:
            continue
        yield path


def with_functions(path):
    """(line number, text, enclosing function) for every line."""
    current = ""
    for i, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        m = FUNC_DEF.match(line)
        if m:
            current = m.group(1)
        yield i, line, current


def rdata_messages(image):
    out = {}
    for name, start, _, raw, raw_size in image.sections:
        if name != ".rdata":
            continue
        blob = image.data[raw:raw + raw_size]
        for m in re.finditer(rb"[\x20-\x7e\t\n]{6,}", blob):
            text = m.group(0).decode()
            if MESSAGE.search(text) and (m.start() == 0 or blob[m.start() - 1] == 0):
                out[start + m.start()] = text.strip()
    return out


def image_users(image, functions, addresses):
    """Functions of the original that push a string's address (push imm32)."""
    starts = sorted(functions)
    import bisect
    users = {a: set() for a in addresses}
    wanted = {struct.pack("<I", a): a for a in addresses}
    for name, start, _, raw, raw_size in image.sections:
        if name in (".rdata", ".data", ".bss"):
            continue
        blob = image.data[raw:raw + raw_size]
        j = blob.find(b"\x68")
        while 0 <= j < len(blob) - 4:
            address = wanted.get(blob[j + 1:j + 5])
            if address is not None:
                k = bisect.bisect_right(starts, start + j) - 1
                if k >= 0:
                    users[address].add(functions[starts[k]])
            j = blob.find(b"\x68", j + 1)
    return users


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=str(ROOT / "tools" / "data" / "limits.csv"))
    args = parser.parse_args()

    image = Image()
    functions, data_names = load_names()
    levels = level_addresses()
    rows = []

    def add(kind, address, what, size, count, stride, function, where, note=""):
        rows.append({
            "kind": kind, "address": f"{address:08x}" if address else "",
            "what": what or data_names.get(address, ""), "size": size, "count": count, "stride": stride,
            "function": function, "where": where,
            "level_reached": "yes" if address and address in levels else "", "note": note,
        })

    messages = rdata_messages(image)
    message_refs = {a: set() for a in messages}
    lines = {path: list(with_functions(path)) for path in source_files()}
    for path, numbered in lines.items():
        rel = path.relative_to(ROOT)
        for i, line, function in numbered:
            for m in re.finditer(r"0x00[78][0-9a-fA-F]{5}", line):
                address = int(m.group(0), 16)
                if address in message_refs:
                    message_refs[address].add(f"{function} ({rel}:{i})")
            for label, pattern in ALLOC:
                for m in pattern.finditer(line):
                    if label == "pool table":
                        window = " ".join(text for _, text, _ in numbered[max(0, i - 4):i])
                        if "kPools" not in window:
                            continue
                        if m.group(3) is not None:  # {size, align, count}
                            size, count = number(m.group(1)), number(m.group(3))
                        elif "apsAddPool" in " ".join(t for _, t, f in numbered[i:i + 12] if f == function):
                            count, size = number(m.group(1)), number(m.group(2))
                        else:  # {size, count} (Sys_InitHeaps)
                            size, count = number(m.group(1)), number(m.group(2))
                        add("alloc", 0, f"pool of {count} x 0x{size:x}", size * count, count, size, function,
                            f"{rel}:{i}")
                    elif label == "apsAddPool":
                        count, size = number(m.group(1)), number(m.group(2))
                        add("alloc", 0, f"pool of {count} x 0x{size:x}", size * count, count, size, function,
                            f"{rel}:{i}")
                    elif label == "boot_construct":
                        add("alloc", int(m.group(1), 16), "", number(m.group(3)), 1, "", function, f"{rel}:{i}",
                            "boot_construct")
                    elif label == "Heap_AllocAligned":
                        size = number(m.group(2))
                        if size >= 0x1000:
                            add("alloc", 0, label, size, "", "", function, f"{rel}:{i}")
                    else:
                        size = number(m.group(1))
                        if size >= 0x1000:
                            add("alloc", 0, label, size, "", "", function, f"{rel}:{i}")
            loop = LOOP.search(line)
            if loop:
                count = number(loop.group(1))
                body = " ".join(text for _, text, _ in numbered[i - 1:i + 3])
                for m in INDEXED.finditer(body):
                    stride = number(m.group(2))
                    if 1 < count <= 0x10000 and stride > 0:
                        add("array", int(m.group(1), 16), "", count * stride, count, stride, function,
                            f"{rel}:{i}")
            for m in CAP.finditer(line):
                value = number(m.group(3))
                if 4 <= value <= 0x10000:
                    add("cap", int(m.group(1), 16), "", "", value, "", function, f"{rel}:{i}", f"{m.group(2)} {value}")

    image_refs = image_users(image, functions, list(messages))
    for address, text in sorted(messages.items()):
        users = sorted(message_refs[address]) or sorted(f"{f} (original)" for f in image_refs[address])
        if not users:
            continue
        add("message", address, text[:100], "", "", "", "; ".join(users)[:300], "")

    # One row per (kind, address, count, stride, function): the same array walked twice in a function is one row.
    seen, unique = set(), []
    for row in rows:
        key = (row["kind"], row["address"], row["count"], row["stride"], row["function"], row["what"])
        if key in seen:
            continue
        seen.add(key)
        unique.append(row)
    order = {"message": 0, "alloc": 1, "array": 2, "cap": 3}
    unique.sort(key=lambda r: (order[r["kind"]], r["address"], str(r["where"])))
    with open(args.out, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(unique[0].keys()))
        writer.writeheader()
        writer.writerows(unique)
    counts = {}
    for row in unique:
        counts[row["kind"]] = counts.get(row["kind"], 0) + 1
    print(f"{args.out}: " + ", ".join(f"{k} {v}" for k, v in counts.items()) +
          f"; level-reached {sum(1 for r in unique if r['level_reached'])}")


if __name__ == "__main__":
    main()
