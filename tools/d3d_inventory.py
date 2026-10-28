#!/usr/bin/env python3
"""Inventory of the graphics library code and data the decompiled C reaches.

Scans game/code/*.cpp for title addresses inside the title's D3D8, D3DX and XGraphics
sections and writes one row per (C function, library address, kind) to
tools/data/d3d_calls.csv. Kind: call (title_function), data (title_global / title_pointer /
title_data or a bare address). The C function is the enclosing definition; its module
comes from tools/data/progress.csv (static helpers without a row: "helper").

  tools/d3d_inventory.py            write the CSV, print a summary per library function
  tools/d3d_inventory.py --by-func  summary per C function instead
"""
import collections
import csv
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CODE = ROOT / "game" / "code"
DATA = ROOT / "tools" / "data"

# XBE sections (default.xbe).
LIBRARIES = [
    ("D3DX", 0x003ABF60, 0x003C60D8),
    ("D3D", 0x003E4EC0, 0x003F973C),
    ("XGRPH", 0x003F9740, 0x0041A20C),
]

# D3D's texture-stage and render-state shadows, dirty flags (the C writes them directly).
D3D_STATE = (0x003F5C00, 0x003F8600)

ADDRESS = re.compile(r"0x00([0-9a-fA-F]{6})\b")
ACCESS = re.compile(r"(title_function|title_global|title_pointer|title_data)\s*<")
DEFINITION = re.compile(r"([A-Za-z_][\w:~]*)\s*\(")
SKIP_WORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_assert"}


def library_of(address):
    for name, start, end in LIBRARIES:
        if start <= address < end:
            return name
    return None


def load_symbols():
    symbols = {}
    for line in open(DATA / "default_xbe_symbols.txt"):
        if line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            symbols[int(parts[0], 16)] = parts[1]
    return symbols


def load_modules():
    modules = {}
    for row in csv.DictReader(open(DATA / "progress.csv")):
        modules[row["name"]] = row["module"]
    return modules


def strip_comment(line):
    # Good enough for this code base: no "//" inside string literals near addresses.
    index = line.find("//")
    return (line, "") if index < 0 else (line[:index], line[index + 2:].strip())


def scan(path):
    """Yields (function, line number, address, kind, comment)."""
    depth = 0  # brace depth, namespaces not counted
    namespace_depths = []
    function = None
    pending = None  # the last name( seen at depth 0, waiting for its {
    for number, raw in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
        code, comment = strip_comment(raw)
        stripped = code.strip()
        if depth == 0 and stripped and not stripped.startswith("#"):
            match = None
            for match in DEFINITION.finditer(code):
                if match.group(1) not in SKIP_WORDS:
                    break
            else:
                match = None
            if match and not stripped.startswith(("namespace", "}")):
                pending = match.group(1).split("::")[-1]
        for address_match in ADDRESS.finditer(code):
            address = int(address_match.group(1), 16)
            if not library_of(address):
                continue
            before = code[: address_match.start()]
            accesses = list(ACCESS.finditer(before))
            kind = "data"
            if accesses and accesses[-1].group(1) == "title_function":
                kind = "call"
            yield function or pending or "(file scope)", number, address, kind, comment
        for char in code:
            if char == "{":
                if re.match(r"\s*(inline\s+)?namespace\b", code) and depth == len(namespace_depths) * 0 and not function:
                    namespace_depths.append(depth)
                    continue
                if depth == 0 and function is None:
                    function = pending
                depth += 1
            elif char == "}":
                if depth == 0:
                    if namespace_depths:
                        namespace_depths.pop()
                    continue
                depth -= 1
                if depth == 0:
                    function = None
                    pending = None
        if depth == 0 and stripped.endswith(";"):
            pending = None


def xbe_sites():
    """The original's call sites and immediates into the libraries, outside them: catches
    callers without C. Yields (site, target, kind); immediates may be false hits."""
    import struct
    ENTRIES = {a for a, n in load_symbols().items() if library_of(a) and not n.startswith("FUN_")}
    data = (ROOT.parent / "unpacked" / "default.xbe").read_bytes()
    base = struct.unpack_from("<I", data, 0x104)[0]
    count, table = struct.unpack_from("<II", data, 0x11C)
    for i in range(count):
        o = table - base + i * 56
        _, va, vsize, raw, rsize, name = struct.unpack_from("<6I", data, o)
        section = data[name - base:data.index(b"\0", name - base)].decode()
        if section in ("D3D", "D3DX", "XGRPH", "DSOUND", "XONLINE", "XNET", "XPP") or section.startswith((".r", ".d", "$", "XON", "DOLBY", "dsp", ".XT")):
            continue
        body = data[raw:raw + rsize]
        for j in range(len(body) - 4):
            value = struct.unpack_from("<I", body, j)[0]
            if body[j - 1:j] in (b"\xe8", b"\xe9") and j:
                target = (va + j + 4 + value) & 0xFFFFFFFF
                if library_of(target):
                    yield va + j - 1, target, "call"
            # Immediates: library entry points and D3D's state shadows only (other values in the
            # range are mostly float constants).
            if value in ENTRIES or (D3D_STATE[0] <= value < D3D_STATE[1] and value % 4 == 0):
                yield va + j, value, "imm"


def uncovered():
    """--xbe: library uses in functions that have no C yet."""
    import bisect
    symbols = load_symbols()
    rows = list(csv.DictReader(open(DATA / "progress.csv")))
    starts = [int(r["address"], 16) for r in rows]
    found = collections.defaultdict(set)
    for site, target, kind in xbe_sites():
        k = bisect.bisect_right(starts, site) - 1
        if k < 0:
            continue
        row = rows[k]
        if row["state"] in ("named", "unnamed", "library"):
            found[(row["module"], row["state"], row["name"], row["address"])].add(
                f"{kind}:{symbols.get(target, f'{target:08x}').split('::')[-1]}")
    for (module, state, name, address), targets in sorted(found.items()):
        print(f"{module:10} {state:8} {address} {name:40} {'; '.join(sorted(targets))[:170]}")


def main():
    if "--xbe" in sys.argv:
        uncovered()
        return
    by_func = "--by-func" in sys.argv
    symbols = load_symbols()
    modules = load_modules()
    rows = []
    for path in sorted(CODE.glob("*.cpp")):
        for function, number, address, kind, comment in scan(path):
            rows.append({
                "file": path.name,
                "line": number,
                "function": function,
                "module": modules.get(function, "helper"),
                "library": library_of(address),
                "address": f"{address:08x}",
                "symbol": symbols.get(address, ""),
                "kind": kind,
                "comment": comment,
            })
    out = DATA / "d3d_calls.csv"
    with open(out, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    if by_func:
        groups = collections.defaultdict(set)
        for r in rows:
            groups[(r["module"], r["file"], r["function"])].add(r["symbol"] or r["comment"][:40] or r["address"])
        for (module, file, function), targets in sorted(groups.items()):
            print(f"{module:10} {file:16} {function:40} {len(targets):3}  {'; '.join(sorted(targets))[:160]}")
    else:
        groups = collections.defaultdict(lambda: {"sites": 0, "functions": set(), "modules": set(), "comments": set()})
        for r in rows:
            g = groups[(r["library"], r["kind"], r["address"], r["symbol"])]
            g["sites"] += 1
            g["functions"].add(r["function"])
            g["modules"].add(r["module"])
            if r["comment"]:
                g["comments"].add(r["comment"][:50])
        for (library, kind, address, symbol), g in sorted(groups.items()):
            label = symbol or (sorted(g["comments"])[0] if g["comments"] else "")
            print(f"{library:5} {kind:4} {address} {label[:60]:60} sites {g['sites']:3} funcs {len(g['functions']):3}"
                  f"  {','.join(sorted(g['modules']))}")
    print(f"{len(rows)} accesses -> {out.relative_to(ROOT)}", file=sys.stderr)


if __name__ == "__main__":
    main()
