#!/usr/bin/env python3
"""host_unreached_classify.py: sort tools/host_unreached.py's list into what to do with each function.

Classes (first that applies):
  static_init  only referenced from the .CRT$XC static initializer table: replaced by static_init_generated.cpp
  set_aside    already in tools/data/set_aside.csv (its kind is printed)
  thunk_c      a `jmp rel32` thunk whose target has a C row: needs only an alias row
  thunk        a `jmp rel32` thunk to a function without C
  c_no_row     a function with this name is defined in src/engine but has no functions.cpp row
  live         Xbox Live / UIX / XVOICE / XONLINE code (dead: Live is not part of the own engine, user 2026-11-12),
               by name/module, or every reference sits in a pointer table whose named members are all Live
  eh_dead      a C++ EH unwind action / catch funclet of a function that runs as our C (dead: our C has its own EH)
  eh_sdk       the same for an SDK library function (XGRPH ...) our platform layer replaces
  eh           the same for an engine function still without C (decompiling the owner removes it)
  noise        every reference is in .XTLID / DOLBY / XON_RD (title-id list, DSP image, Live data: not pointers)
  mslib        other SDK library code in .text (XAPILIB, XGRAPHICS, LIBCMT): replaced by our platform layer/CRT
  level        referenced from level code or level data: a mission can reach it
  virtual      a slot in a vtable that our C or level code installs: reachable, needs C
  vt_noc       a slot in a vtable whose constructors are themselves without C
  sdk_class    a slot in a vtable installed only by SDK/Live code
  codec_slot   a method of a NAL anim codec class (registry 0x008bb120..): runs only when an anim uses that codec
  command      a console command (cheat/debug list at 0x008ddae0): reachable only by typing or exec'ing it
  data_table   in a pointer table only data points at (class registries: see the holder addresses)
  table        in a pointer table nothing references (callback/command tables read by index ...)

usage: tools/host_unreached_classify.py [--out FILE] [--class NAME]   (default: summary + build/unreached/classes.txt)
"""
import argparse
import bisect
import csv
import pickle
import re
import struct
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import host_unreached as hu  # noqa: E402
import progress  # noqa: E402

ROOT = hu.ROOT
DATA = ROOT / "tools" / "data"
LIVE_NAME = re.compile(r"(Live|UIX::|XVOICE::|XONLINE|XOnline|Live_|LiveMgr|LiveRecord_|LiveMgrBase_|Net_RegisterSessionKey|"
                       r"Net_UnregisterSessionKeys|XNet|CXo|Xo[A-Z])")
XC_TABLE = (0x00897240, 0x008978d0)  # .CRT$XI/.CRT$XC pointer tables (tools/gen_static_init.py)
CMD_TABLE = (0x008ddae0, 0x008ddc00)  # the engine's {name, function} console command list
NAL_REGISTRY = (0x008bb120, 0x008bb2c0)  # NAL codec classes: {vtable, ?, 0, name, previous}
NOISE_SECTIONS = {".XTLID", "DOLBY", "XON_RD"}
ENGINE_SECTIONS = {".rdata", ".data"}


def code_refs(data, secs):
    """{target: [instruction address]} for every 5+ hex-digit operand of every code instruction (engine, SDK and level
    sections), cached in build/unreached/code_refs.pkl."""
    cache = ROOT / "build/unreached/code_refs.pkl"
    if cache.exists() and cache.stat().st_mtime > hu.DEFAULT_XBE.stat().st_mtime:
        return pickle.loads(cache.read_bytes())
    refs = defaultdict(list)
    for n, va, vs, raw, rs in secs:
        if n.startswith("$$") or n in (".rdata", ".data", ".data1", "XON_RD", "DOLBY", "dspimage", ".XTLID"):
            continue
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            tmp.write(data[raw:raw + rs])
            tmp.flush()
            dis = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386", "-M", "intel",
                                  f"--adjust-vma={va:#x}", tmp.name], capture_output=True, text=True).stdout
        for line in dis.splitlines():
            m = re.match(r"\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\w+)\s+(.*)", line)
            if m:
                for t in re.findall(r"0x([0-9a-f]{5,8})\b", m.group(3)):
                    refs[int(t, 16)].append(int(m.group(1), 16))
    refs = dict(refs)
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_bytes(pickle.dumps(refs))
    return refs


def eh_tables(data, secs, word, crefs, fn_of):
    """{address in .rdata: owner functions} for the C++ EH tables: every FuncInfo (magic 0x19930520 in .rdata) with its
    unwind actions and catch handlers. Owner = the function whose prologue installs the FuncInfo's handler stub
    (`mov eax, FuncInfo; jmp ___CxxFrameHandler`); an empty set = no stub loads this FuncInfo (the linker folded its
    function away), so nothing can ever run its funclets."""
    stubs = defaultdict(list)  # FuncInfo -> stub addresses
    for n, va, vs, raw, rs in secs:
        if n == ".text" or n in hu.SDK or not n.startswith((".", "$$")) and n not in ("XON_RD", "DOLBY", "dspimage"):
            for m in re.finditer(rb"\xb8(....)\xe9", data[raw:raw + rs], re.S):
                stubs[struct.unpack("<I", m.group(1))[0]].append(va + m.start())
    out = {}
    for n, va, vs, raw, rs in secs:
        if n != ".rdata":
            continue
        for off in range(0, rs - 3, 4):
            if struct.unpack_from("<I", data, raw + off)[0] != 0x19930520:
                continue
            info = va + off
            owners = {fn_of(u) for st in stubs.get(info, []) for u in crefs.get(st, [])}
            max_state, unwind, ntry, trymap = (word(info + k) or 0 for k in (4, 8, 12, 16))
            for i in range(max_state if max_state < 4096 else 0):
                out.setdefault(unwind + 8 * i + 4, set()).update(owners)
            for t in range(ntry if ntry < 256 else 0):
                ncatch, handlers = word(trymap + 20 * t + 12) or 0, word(trymap + 20 * t + 16) or 0
                for c in range(ncatch if ncatch < 64 else 0):
                    out.setdefault(handlers + 16 * c + 12, set()).update(owners)
    return out


def module_of(a, name, mods):
    prefixes, ranges, hints = mods
    for rx, m in prefixes:
        if name and rx.match(name):
            return m
    if a in hints:
        return hints[a]
    for lo, hi, m in ranges:
        if lo <= a < hi:
            return m
    return "unassigned"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build/unreached/classes.txt"))
    ap.add_argument("--class", dest="cls")
    args = ap.parse_args()

    data = hu.DEFAULT_XBE.read_bytes()
    secs = hu.sections(data)
    refs, section_of, names = hu.collect(data)
    mods = progress.load_modules()
    fsrc = (ROOT / "src/engine/shared/functions.cpp").read_text()
    rows = {int(m.group(2), 16): m.group(1) for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]{8}),', fsrc)}
    aside = {int(r["address"], 16): r for r in csv.DictReader(open(DATA / "set_aside.csv"))}
    text_lo, text_hi = next((va, va + vs) for n, va, vs, _, _ in secs if n == ".text")

    def word(a):
        for n, va, vs, raw, rs in secs:
            if va <= a < va + rs:
                return struct.unpack_from("<I", data, raw + a - va)[0]
        return None

    def byte(a):
        for n, va, vs, raw, rs in secs:
            if va <= a < va + rs:
                return data[raw + a - va]
        return None

    crefs = code_refs(data, secs)
    starts = sorted(a for a in set(names) | set(rows) if text_lo <= a < text_hi)

    def fn_of(a):
        if not (text_lo <= a < text_hi):
            return a  # level code: the address itself (a level section is never cleared)
        i = bisect.bisect_right(starts, a) - 1
        return starts[i] if i >= 0 else None

    eh = eh_tables(data, secs, word, crefs, fn_of)

    # Names defined in our engine C (a definition: name followed by '(' at the start of a declarator).
    defined = set()
    for p in (ROOT / "src/engine").rglob("*.cpp"):
        for m in re.finditer(r"^[\w:<>*&\s]*?\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?\{", p.read_text(errors="replace"), re.M):
            defined.add(m.group(1))

    # Pointer tables: runs of aligned words in .rdata/.data that point into .text.
    def table_of(at):
        lo = at
        while lo - 4 >= 0 and (w := word(lo - 4)) is not None and text_lo <= w < text_hi:
            lo -= 4
        hi = at
        while (w := word(hi + 4)) is not None and text_lo <= w < text_hi:
            hi += 4
        return lo, hi

    def is_live(a):
        n = names.get(a) or rows.get(a) or ""
        return bool(LIVE_NAME.search(n)) or module_of(a, n, mods) == "online"

    def live_table(at):
        lo, hi = table_of(at)
        members = [word(x) for x in range(lo, hi + 4, 4)]
        named = [m for m in members if names.get(m) or rows.get(m)]
        live = [m for m in named if is_live(m)]
        return bool(named) and len(live) == len(named), len(named), len(live)

    drefs = defaultdict(list)  # .rdata/.data address -> data words pointing at it
    for n, va, vs, raw, rs in secs:
        if n in ENGINE_SECTIONS:
            for off in range(0, rs - 3, 4):
                w = struct.unpack_from("<I", data, raw + off)[0]
                if 0x007b2fe0 <= w < 0x0098fb80:
                    drefs[w].append(va + off)

    def data_start(at):
        """Like vt_start, for runs that only data words point at (class registries holding vtable pointers)."""
        a = at
        while True:
            if a in drefs:
                return a
            p = word(a - 4)
            if p is None or not (text_lo <= p < text_hi):
                return None
            a -= 4

    def vt_start(at):
        """The start of the pointer run holding `at`: the nearest word at or before it that code references."""
        a = at
        while True:
            if a in crefs:
                return a
            p = word(a - 4)
            if p is None or not (text_lo <= p < text_hi):
                return None
            a -= 4

    def owner_kind(o):
        if o is None:
            return "none"
        if is_live(o):
            return "live"
        if o in rows:
            return "c"
        if section_of(o) in hu.SDK:
            return "sdk"
        if not (text_lo <= o < text_hi):
            return "level"
        return "noc"

    def cmd_name(at):
        """The name of a {name, function} console command table entry (0x008ddae0..), or None."""
        if not (CMD_TABLE[0] <= at < CMD_TABLE[1]):
            return None
        n = word(at - 4)
        for sn, va, vs, raw, rs in secs:
            if sn == ".rdata" and n and va <= n < va + rs:
                o = raw + n - va
                return data[o:data.index(b"\0", o)].decode(errors="replace")
        return None

    out, counts = [], Counter()
    for a in sorted(refs):
        r = refs[a]
        name = names.get(a, "?")
        mod = module_of(a, None if name == "?" else name, mods)
        secs_of_refs = {s for _, s, _ in r}
        note = ""
        cls = None
        if all(XC_TABLE[0] <= at < XC_TABLE[1] for at, _, _ in r):
            cls, note = "static_init", "only in .CRT$XC: our generated static initializers replace it"
        elif a in aside:
            cls, note = "set_aside", aside[a]["kind"]
        elif all(at in eh for at, s, _ in r):
            owners = set().union(*(eh[at] for at, s, _ in r))
            if not owners:
                cls = "eh_dead"  # no handler stub loads its FuncInfo
            elif all(o in rows for o in owners):
                cls = "eh_dead"  # the owner runs as our C, with our compiler's EH
            elif all(section_of(o) in hu.SDK for o in owners):
                cls = "eh_sdk"  # owner is SDK library code (XGRAPHICS ...) that our platform layer replaces
            else:
                cls = "eh"
            note = "owner " + ",".join(f"{o:08x} {rows.get(o) or names.get(o, '?')}" for o in sorted(owners)[:3])
        elif byte(a) == 0xE9:
            t = (a + 5 + struct.unpack("<i", struct.pack("<I", word(a + 1)))[0]) & 0xFFFFFFFF
            cls = "thunk_c" if t in rows else "thunk"
            note = f"-> {t:08x} {rows.get(t) or names.get(t, '?')}"
        elif name != "?" and name in defined:
            cls = "c_no_row"
        elif is_live(a):
            cls = "live"
        elif secs_of_refs <= NOISE_SECTIONS:
            cls = "noise"
        elif mod == "mslib" or re.match(r"(XAPILIB|XGRAPHICS|LIBCMT)::", name):
            cls = "mslib"
        elif secs_of_refs - ENGINE_SECTIONS - NOISE_SECTIONS:
            cls = "level"
        else:
            owners = set()
            for at, sec, _ in r:
                if sec in ENGINE_SECTIONS:
                    start = vt_start(at)
                    owners |= {fn_of(u) for u in crefs.get(start, [])} if start else set()
            kinds = {owner_kind(o) for o in owners}
            note = "by " + ",".join(f"{o:08x} {rows.get(o) or names.get(o, '?')}" for o in sorted(owners)[:3])
            if not owners:
                holders = sorted({h for at, sec, _ in r if (st := data_start(at)) for h in drefs[st]})
                cls = "data_table" if holders else "table"
                if holders and all(NAL_REGISTRY[0] <= h < NAL_REGISTRY[1] for h in holders):
                    cls = "codec_slot"  # a NAL anim codec class method: runs only if an anim uses that codec
                note = ("held at " + ",".join(f"{h:08x}" for h in holders[:4])) if holders else "no references"
                cmds = [cmd_name(at) for at, sec, _ in r if sec in ENGINE_SECTIONS]
                if cmds and all(cmds):
                    cls, note = "command", "console command " + ",".join(cmds)
            elif kinds <= {"live"}:
                cls = "live"
            elif kinds <= {"live", "sdk"}:
                cls = "sdk_class"
            elif "c" in kinds or "level" in kinds:
                cls = "virtual"  # a class our C (or level code) constructs: reachable through its vtable
            else:
                cls = "vt_noc"  # every constructor is itself without C
        counts[cls] += 1
        refstr = " ".join(f"{at:08x}({s},{k})" for at, s, k in r[:4]) + (" ..." if len(r) > 4 else "")
        out.append(f"{cls:9} {a:08x} {name:40} {mod:11} refs={len(r):<3} {note}  [{refstr}]")
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text("\n".join(out) + "\n")
    if args.cls:
        print("\n".join(l for l in out if l.startswith(args.cls + " ")))
    print(f"# {len(out)} unreached: " + ", ".join(f"{k} {v}" for k, v in counts.most_common()) + f"  -> {args.out}")


if __name__ == "__main__":
    main()
