#!/usr/bin/env python3
"""boundary.py: the engine contract the original level code needs (plan 2026-11-06: engine = our own C,
the 14 level sections stay original x86 from the player's dump).

Scans every function in the level sections (PROLOGUE..DRAGEETH; PROLOGUE also holds shared helpers other
levels call) and records each reference that leaves the function's own section:
  slot     call/jmp/read of the import block 0x00922140 (slot = offset / 4) -> builtin, per level
  block    words in the block past the filled slots (shared values levels read/write)
  call     direct CALL/JMP into .text (engine functions the level calls without a slot)
  xlevel   calls into another level section (PROLOGUE's shared helpers)
  global   .data/.bss operands (read / write / address taken), with the struct offsets reached through a
           register loaded from that global (heuristic, linear: reset at branches, calls clobber eax/ecx/edx)
  ret      struct offsets read/written through eax right after a slot call (the builtin's returned object)
  code     .text addresses used as values (callbacks handed to the engine)
  rdata    .rdata operands (constants/strings: come from the dump with the levels; counted only)
And the other direction: level code addresses stored by level code or found in engine data/code (the
entry points and callbacks the engine calls back into).

usage: tools/levels/boundary.py [--out docs/level_boundary]   (writes .md summary + .csv rows)
Reads the XBE and exported symbols only (no Ghidra needed).
"""
import argparse
import bisect
import collections
import csv
import os
import re
import struct
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
os.chdir(os.path.dirname(TOOLS))
import fnpack  # noqa: E402
import levelreach  # noqa: E402

BLOCK = levelreach.BLOCK
LEVELS = levelreach.LEVELS
INIT_NAME = re.compile(r"G_Setup\w*ScriptImports$")
STORE = re.compile(r"^mov DWORD PTR \[e\w\w(?:\+0x([0-9a-f]+))?\],0x([0-9a-f]+)$")
HEX = re.compile(r"0x([0-9a-f]{5,8})\b")
MEM = re.compile(r"(?:(BYTE|WORD|DWORD|QWORD|XMMWORD|TBYTE) PTR )?(?:ds:)?\[([^\]]+)\]|ds:0x([0-9a-f]+)")
REG32 = ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp")
SUB = {"al": "eax", "ah": "eax", "ax": "eax", "bl": "ebx", "bh": "ebx", "bx": "ebx", "cl": "ecx", "ch": "ecx",
       "cx": "ecx", "dl": "edx", "dh": "edx", "dx": "edx", "si": "esi", "di": "edi", "bp": "ebp"}
BRANCH = re.compile(r"^(j\w+|ret\w*|loop\w*)\b")
NO_WRITE = ("cmp", "test", "push", "ucomiss", "comiss", "fld", "fild", "fcom", "fcomp", "fadd", "fsub", "fmul",
            "fdiv", "fsubr", "fdivr", "fucom", "fucomp", "bt")


def load_data_names():
    names = {}
    for file in ("manual_names.csv", "cvar_globals.csv"):
        with open(fnpack.DATA / file, newline="") as f:
            for row in csv.DictReader(f):
                if row["kind"] == "data":
                    names[int(row["address"], 16)] = row["name"]
    return names


def filled_slots(image, starts):
    """slot -> builtin, from every G_Setup*ScriptImports (they store code addresses through the block pointer
    0x0095f49c; levelreach.SLOT_INITS has only four of the seven)."""
    functions, _ = fnpack.load_names()
    inits = [a for a, n in functions.items() if INIT_NAME.match(n)]
    slots = {}
    for init in inits:
        end = starts[bisect.bisect_right(starts, init)]
        for _, _, text in fnpack.disassemble(image, init, end - init):
            m = STORE.match(text)
            if m and image.section(int(m.group(2), 16)) not in fnpack.DATA_SECTIONS:
                slots[int(m.group(1) or "0", 16) // 4] = int(m.group(2), 16)
    return slots


def engine_fields(image, functions, starts, pointers):
    """Offsets .text code reaches through a register loaded from one of `pointers` (same linear heuristic)."""
    out = collections.defaultdict(lambda: collections.defaultdict(set))  # ptr -> off -> functions
    _, start, _, _, raw_size = next(x for x in image.sections if x[0] == ".text")
    regs = {}
    for line in levelreach.objdump(image, start, raw_size).splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        text = re.sub(r"\s+", " ", parts[2]).strip()
        va = int(parts[0].strip(" :"), 16)
        mnemonic = text.split(" ")[0]
        for m in re.finditer(r"\[(e\w\w)(?:\+0x([0-9a-f]+))?\]", text):
            if m.group(1) in regs:
                o = starts[bisect.bisect_right(starts, va) - 1]
                out[regs[m.group(1)]][int(m.group(2) or "0", 16)].add(functions[o])
        if re.match(r"(j|ret|call|int3)", mnemonic):
            regs = {}
            continue
        dst = text.split(" ")[1].split(",")[0] if " " in text else ""
        regs.pop(dst, None)
        m = re.match(r"mov (e\w\w),(?:DWORD PTR )?ds:0x([0-9a-f]+)$", text)
        if m and int(m.group(2), 16) in pointers:
            regs[m.group(1)] = int(m.group(2), 16)
    return out


def states_crt(name):
    return bool(re.match(r"(_|LIBCMT::|\?\?_)", name))


def operands(text):
    mnemonic, _, rest = text.partition(" ")
    return mnemonic, fnpack.split_operands(text)[1] if rest else []


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("--out", default="docs/level_boundary")  # .md + .csv
    args = ap.parse_args()
    functions, _ = fnpack.load_names()
    data_names = load_data_names()
    starts = sorted(functions)
    image = fnpack.Image()
    sec = {n: (s, s + rs) for n, s, _, _, rs in image.sections}
    slots = filled_slots(image, starts)
    block_end = BLOCK + 4 * (max(slots) + 1)

    def owner(va):
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else None

    def fname(va):
        o = owner(va)
        if o is None:
            return hex(va)
        return functions[o] if o == va else f"{functions[o]}+{va - o:#x}"

    def gname(va):
        if va in data_names:
            return data_names[va]
        keys = sorted(data_names)
        i = bisect.bisect_right(keys, va) - 1
        if i >= 0 and va - keys[i] < 0x40:
            return f"{data_names[keys[i]]}+{va - keys[i]:#x}"
        return f"g_{va:08x}"

    rows = []  # kind, level, function, target, detail
    slot_use = collections.defaultdict(collections.Counter)  # slot -> level -> sites
    glob = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))  # va -> access -> level
    glob_fields = collections.defaultdict(collections.Counter)  # (va, off) -> access
    ret_fields = collections.defaultdict(collections.Counter)  # (slot, off) -> access
    calls = collections.defaultdict(collections.Counter)  # engine fn -> level
    xlevel = collections.defaultdict(collections.Counter)
    code_vals = collections.defaultdict(collections.Counter)
    callbacks = collections.defaultdict(set)  # level fn -> how it's handed out
    rdata = collections.Counter()
    funcs_per_level = collections.Counter()
    fs_sites = collections.Counter()
    seh_funcs = set()
    alias = collections.defaultdict(set)  # level global -> block words holding the same pointer

    def close_run(run):
        if not run:
            return
        words = [x for x in run[1] if BLOCK <= x < block_end + 0x200 and (x - BLOCK) // 4 not in slots]
        for x in run[1]:
            for wd in words:
                if x != wd:
                    alias[x].add(wd)

    for level in LEVELS:
        lo, hi = sec[level]
        level_starts = [a for a in starts if lo <= a < hi]
        funcs_per_level[level] = len(level_starts)
        for k, fstart in enumerate(level_starts):
            fend = level_starts[k + 1] if k + 1 < len(level_starts) else hi
            fn = functions[fstart]
            regs = {}  # reg -> ("g", va) | ("ret", slot)
            run = None
            for va, size, text in fnpack.disassemble(image, fstart, fend - fstart):
                mnemonic, ops = operands(text)
                if "fs:" in text:
                    fs_sites[text.split("fs:")[1].split(",")[0]] += 1
                    seh_funcs.add(fstart)
                # memory operands with an absolute address or a tracked base register
                for i, op in enumerate(ops):
                    m = MEM.search(op)
                    if not m:
                        continue
                    inner = m.group(2) or ("0x" + m.group(3))
                    write = i == 0 and mnemonic not in NO_WRITE and len(ops) > 1 or mnemonic in ("fstp", "fst", "fistp", "inc", "dec") or (mnemonic.startswith("set") and i == 0)
                    access = "lea" if mnemonic == "lea" else ("write" if write else "read")
                    abs_m = re.fullmatch(r"0x([0-9a-f]+)", inner.strip())
                    if abs_m:
                        a = int(abs_m.group(1), 16)
                        s = image.section(a)
                        if BLOCK <= a < block_end + 0x200:
                            slot = (a - BLOCK) // 4
                            if slot in slots and mnemonic in ("call", "jmp"):
                                slot_use[slot][level] += 1
                            elif slot in slots:
                                slot_use[slot][level] += 1
                                rows.append(("slot_read", level, fn, f"{slot}", text))
                            else:
                                glob[a][access][level] += 1
                        elif s in (".data", ".bss", ".data1", "XON_RD", "DOLBY"):
                            glob[a][access][level] += 1
                        elif s == ".rdata":
                            rdata[level] += 1
                        continue
                    base = re.match(r"(e\w\w)(?:\+0x([0-9a-f]+)|-0x([0-9a-f]+))?$", inner.strip())
                    if base and base.group(1) in regs:
                        off = int(base.group(2) or "0", 16) - int(base.group(3) or "0", 16)
                        src = regs[base.group(1)]
                        if src[0] == "g":
                            glob_fields[(src[1], off)][access] += 1
                        else:
                            ret_fields[(src[1], off)][access] += 1
                    elif base is None:  # [reg+reg*n+0xADDR]: indexed global array
                        for h in HEX.finditer(inner):
                            a = int(h.group(1), 16)
                            if image.section(a) in (".data", ".bss", ".data1") and not (BLOCK <= a < block_end):
                                glob[a][access + "[i]"][level] += 1
                # immediates: addresses as values
                if mnemonic not in ("call", "jmp") and not mnemonic.startswith("j"):
                    for op in ops:
                        if "[" in op or "ds:" in op:
                            continue
                        h = re.fullmatch(r"0x([0-9a-f]+)", op.strip())
                        if not h:
                            continue
                        a = int(h.group(1), 16)
                        s = image.section(a)
                        if s == ".text" and a in functions:
                            code_vals[a][level] += 1
                        elif s in LEVELS and a in functions:
                            callbacks[a].add(f"{mnemonic} in {level}")
                        elif s in (".data", ".bss", ".data1") and not (lo <= a < hi):
                            if BLOCK <= a < block_end:
                                rows.append(("slot_addr", level, fn, f"{(a - BLOCK) // 4}", text))
                            else:
                                glob[a]["addr"][level] += 1
                # direct calls
                if mnemonic in ("call", "jmp") and ops and re.fullmatch(r"0x[0-9a-f]+", ops[0]):
                    t = int(ops[0], 16)
                    s = image.section(t)
                    if s == ".text":
                        calls[t][level] += 1
                    elif s in LEVELS and s != level:
                        xlevel[t][level] += 1
                    elif s not in LEVELS and not (fstart <= t < fend):
                        rows.append(("call_other", level, fn, hex(t), text))
                # aliases: a run of adjacent stores of one register to globals (the level publishing an object
                # pointer: its own copies plus block words, e.g. 0x00921cbc, 0x00921cc4, 0x00922ef8, 0x00922ee0)
                m = re.fullmatch(r"(?:DWORD PTR )?ds:0x([0-9a-f]+)", ops[0]) if mnemonic == "mov" and len(ops) == 2 else None
                if m and ops[1] in REG32 and run and run[0] == ops[1]:
                    run[1].append(int(m.group(1), 16))
                else:
                    close_run(run)
                    run = [ops[1], [int(m.group(1), 16)]] if m and ops[1] in REG32 else None
                # register tracking
                if BRANCH.match(mnemonic):
                    regs.clear()

                    continue
                if mnemonic == "call":
                    for r in ("eax", "ecx", "edx"):
                        regs.pop(r, None)
                    m = re.fullmatch(r"DWORD PTR ds:0x(922[0-9a-f]{3})", ops[0]) if ops else None
                    if m:
                        regs["eax"] = ("ret", (int(m.group(1), 16) - BLOCK) // 4)
                    continue
                if ops:
                    dst = SUB.get(ops[0], ops[0])
                    if dst in REG32 and mnemonic not in NO_WRITE:
                        regs.pop(dst, None)

                        if mnemonic == "mov" and len(ops) == 2:
                            m = re.fullmatch(r"(?:DWORD PTR )?ds:0x([0-9a-f]+)", ops[1])
                            if m:
                                a = int(m.group(1), 16)
                                if image.section(a) in (".data", ".bss") and not (BLOCK <= a < block_end):
                                    regs[dst] = ("g", a)
                            elif ops[1] in regs:
                                regs[dst] = regs[ops[1]]
                    if mnemonic == "xchg":
                        regs.clear()

    # level code referenced from outside the level sections (entries / callbacks)
    level_starts_all = {a for a in starts if image.section(a) in LEVELS}
    for name, start, _, raw, raw_size in image.sections:
        if name in LEVELS:
            continue
        blob = image.data[raw:raw + raw_size]
        step = 4 if name in fnpack.DATA_SECTIONS else 1
        for j in range(0, len(blob) - 3, step):
            (w,) = struct.unpack_from("<I", blob, j)
            if w in level_starts_all:
                callbacks[w].add(f"{'data' if step == 4 else 'code'} {name} {start + j:#010x}")

    # engine references to each global: the exact address as a word in non-level code or data
    eng_refs = collections.Counter()
    for name, start, _, raw, raw_size in image.sections:
        if name in LEVELS:
            continue
        blob = image.data[raw:raw + raw_size]
        step = 4 if name in fnpack.DATA_SECTIONS else 1
        for j in range(0, len(blob) - 3, step):
            (w,) = struct.unpack_from("<I", blob, j)
            if w in glob:
                eng_refs[w] += 1

    def in_block(a):
        return BLOCK <= a < block_end + 0x200

    def gclass(a):
        levels = set().union(*[set(v) for v in glob[a].values()])
        if in_block(a):
            return "block"
        if eng_refs[a] or len(levels) > 1:
            return "shared"
        return "private"

    out = args.out
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out + ".csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["kind", "address", "name", "detail", "levels"])
        lv = lambda c: " ".join(f"{k}:{v}" for k, v in sorted(c.items()))  # noqa: E731
        for slot in sorted(slot_use):
            w.writerow(["slot", f"{slot}", functions.get(slots[slot], hex(slots[slot])), f"{slots[slot]:08x}",
                        lv(slot_use[slot])])
        for t in sorted(calls):
            w.writerow(["call", f"{t:08x}", fname(t), "", lv(calls[t])])
        for t in sorted(xlevel):
            w.writerow(["xlevel", f"{t:08x}", fname(t), image.section(t), lv(xlevel[t])])
        for a in sorted(glob):
            for acc in sorted(glob[a]):
                w.writerow(["global", f"{a:08x}", gname(a), f"{acc} {gclass(a)} engine_refs={eng_refs[a]}",
                            lv(glob[a][acc])])
        for (a, off), c in sorted(glob_fields.items()):
            w.writerow(["global_field", f"{a:08x}", f"{gname(a)}->{off:#x}", " ".join(f"{k}:{v}" for k, v in c.items()), ""])
        for (s, off), c in sorted(ret_fields.items()):
            w.writerow(["ret_field", f"{s}", f"{functions.get(slots.get(s), s)}()->{off:#x}",
                        " ".join(f"{k}:{v}" for k, v in c.items()), ""])
        for a in sorted(code_vals):
            w.writerow(["code_value", f"{a:08x}", fname(a), "", lv(code_vals[a])])
        for a in sorted(callbacks):
            w.writerow(["entry", f"{a:08x}", functions[a], "; ".join(sorted(callbacks[a]))[:300], image.section(a)])
        for r in rows:
            w.writerow([r[0], r[3], r[2], r[4], r[1]])

    def top(counter_map, n, label):
        items = sorted(counter_map.items(), key=lambda kv: -sum(kv[1].values()))
        return [f"| {label(k)} | {sum(v.values())} | {len(v)} |" for k, v in items[:n]]

    entries_engine = {a: v for a, v in callbacks.items() if any(not x.split()[0] in ("push", "mov") for x in v)}
    with open(out + ".md", "w") as f:
        p = lambda s="": print(s, file=f)  # noqa: E731
        p("# Level code -> engine boundary (generated by tools/levels/boundary.py)")
        p()
        p("What the original level sections (kept as x86 from the player's dump) need from the engine: our own engine")
        p("implementation must keep every item here at the same address and layout (or a compatible thunk).")
        p()
        p(f"Level functions scanned: {sum(funcs_per_level.values())} "
          f"({', '.join(f'{k} {v}' for k, v in funcs_per_level.items())}).")
        p()
        p("| item | distinct | sites |")
        p("|---|---|---|")
        p(f"| import-block slots used | {len(slot_use)} of {len(slots)} filled | {sum(sum(c.values()) for c in slot_use.values())} |")
        p(f"| direct calls into .text | {len(calls)} functions | {sum(sum(c.values()) for c in calls.values())} |")
        p(f"| calls into another level section | {len(xlevel)} functions | {sum(sum(c.values()) for c in xlevel.values())} |")
        sites = lambda xs: sum(sum(sum(c.values()) for c in glob[a].values()) for a in xs)  # noqa: E731
        gl = [a for a in glob if gclass(a) == "shared"]
        pv = [a for a in glob if gclass(a) == "private"]
        bl = [a for a in glob if gclass(a) == "block"]
        p(f"| shared globals (.data/.bss) | {len(gl)} | {sites(gl)} |")
        p(f"| level-private globals (no contract) | {len(pv)} | {sites(pv)} |")
        p(f"| block words past the slots | {len(bl)} | {sites(bl)} |")
        p(f"| struct fields via a global | {len(glob_fields)} | {sum(sum(c.values()) for c in glob_fields.values())} |")
        p(f"| fields of a builtin's returned object | {len(ret_fields)} | {sum(sum(c.values()) for c in ret_fields.values())} |")
        p(f"| engine code addresses as values | {len(code_vals)} | {sum(sum(c.values()) for c in code_vals.values())} |")
        p(f"| engine function addresses that must stay (calls + values) | {len(set(calls) | set(code_vals))} | |")
        p(f"| level code referenced from outside (entries) | {len([a for a, v in callbacks.items() if any(x.startswith(('data', 'code')) for x in v)])} | |")
        p(f"| level code addresses stored by level code (callbacks) | {len([a for a, v in callbacks.items() if any(not x.startswith(('data', 'code')) for x in v)])} | |")
        p(f"| .rdata operands (dump data, no contract) | | {sum(rdata.values())} |")
        p()
        shared = [a for a in sorted(glob) if gclass(a) == "shared"]
        private = [a for a in sorted(glob) if gclass(a) == "private"]
        crt = sorted(t for t in set(calls) | set(code_vals) if states_crt(functions.get(t, "")))
        p("## Runtime contract (not addresses)")
        p()
        p(f"- SEH: {len(seh_funcs)} level functions link an exception frame through fs:[0] "
          f"({', '.join(f'fs:{k} {v}' for k, v in fs_sites.items())} sites); handlers end in the engine's MSVC")
        p("  ___CxxFrameHandler with the levels' own FuncInfo tables in .rdata. FS must point at an NT_TIB whose")
        p("  ExceptionList is per script thread (the thread switch must save/restore it), and C++ throws from")
        p("  level code must unwind through engine C frames correctly (or never cross them).")
        p(f"- MSVC CRT entry points called directly: {', '.join(functions[t] for t in crt)}.")
        p("- Script threads: the Scr_Wait* family is called directly (call sites under 'Direct engine calls'); it")
        p("  switches the level code's native x86 stack, so our engine keeps stack-switching threads for level code.")
        p("- Calling conventions: each function's original one (seen: caller-cleaned pushes for slot calls,")
        p("  ecx = this for ScriptString_*; not checked per function yet - fnpack.py shows it).")
        p()
        p("## Engine globals the levels share")
        p()
        p("Globals the engine also references (engine refs = the exact address as a word outside the level")
        p("sections) or that more than one level touches. Struct fields: offsets reached through a register")
        p("loaded from the global (r/w/l = read/write/lea).")
        p()
        p("| address | name | access (sites) | levels | engine refs | fields |")
        p("|---|---|---|---|---|---|")
        fields_of = collections.defaultdict(list)
        for (a, off), c in sorted(glob_fields.items()):
            fields_of[a].append(f"{off:#x}({'/'.join(k[0] for k in sorted(c))})")
        for a in shared:
            acc = ", ".join(f"{k} {sum(v.values())}" for k, v in sorted(glob[a].items()))
            levels = set().union(*[set(v) for v in glob[a].values()])
            fl = " ".join(fields_of.get(a, []))
            p(f"| {a:08x} | {gname(a)} | {acc} | {len(levels)} | {eng_refs[a]} | {fl} |")
        p()
        p("## Level-private globals")
        p()
        p("Touched by one level only and never named by engine code: the level's own data (from the dump). The")
        p("engine only has to leave these addresses alone. Per level: count, address range.")
        p()
        per = collections.defaultdict(list)
        for a in private:
            per[next(iter(set().union(*[set(v) for v in glob[a].values()])))].append(a)
        for lvl in LEVELS:
            v = per.get(lvl, [])
            if v:
                nf = sum(len(fields_of.get(a, [])) for a in v)
                p(f"- {lvl}: {len(v)} globals {v[0]:08x}-{v[-1]:08x}, {nf} fields through them")
        p()
        p("## Shared script objects (pointers the levels publish in block words)")
        p()
        p("Each level's entry constructs objects (its own ctors and vtables), keeps the pointers in its .bss block")
        p("(prologue: 0x00921cbc..) and publishes them in block words past the slots; the engine's BroScr_* runtime")
        p("(compiled shared scripts in .text) and Scr_* read them there. Because the level code builds them, every")
        p("offset the engine uses is layout the engine must keep, and offsets on both sides doubly so; level-only")
        p("offsets are the level's own variables. Grouped by block words that always hold the same pointer.")
        p()
        published = sorted({w for ws in alias.values() for w in ws})
        holders_of = {w: frozenset(g for g, ws in alias.items() if w in ws and not (BLOCK <= g < block_end + 0x200))
                      for w in published}
        groups = collections.defaultdict(list)
        for w in published:
            groups[holders_of[w]].append(w)
        eng = engine_fields(image, functions, starts, set(published))
        for holders, words in sorted(groups.items(), key=lambda kv: kv[1]):
            e_offs = collections.defaultdict(set)
            for w in words:
                for o, fns in eng[w].items():
                    e_offs[o] |= fns
            l_offs = collections.defaultdict(lambda: [collections.Counter(), set()])
            for g in holders:
                for (a, off), c in glob_fields.items():
                    if a == g:
                        l_offs[off][0].update(c)
                        l_offs[off][1].update(set().union(*[set(v) for v in glob[a].values()]))
            offs = sorted(set(e_offs) | set(l_offs))
            both = [o for o in offs if o in e_offs and o in l_offs]
            only_e = [o for o in offs if o not in l_offs]
            only_l = [o for o in offs if o not in e_offs]
            names = ", ".join(f"{(w - BLOCK) // 4} ({w:08x} {gname(w)})" for w in words)
            p(f"### block words {names}")
            p()
            hs = sorted(holders)
            p(f"{len(both)} offsets on both sides, {len(only_e)} engine only, {len(only_l)} level only. Level copies: "
              f"{len(hs)} globals ({' '.join(f'{g:08x}' for g in hs[:12])}{' ...' if len(hs) > 12 else ''})")
            p()
            if both:
                p("| offset | level access | levels | engine functions |")
                p("|---|---|---|---|")
                for o in both:
                    c, lv_set = l_offs[o]
                    fns = sorted(e_offs[o])
                    p(f"| {o:#x} | {'/'.join(sorted(c))} | {len(lv_set)} | {len(fns)}: {', '.join(fns[:3])} |")
                p()
            if only_e:
                p(f"Engine-only offsets: {' '.join(f'{o:#x}' for o in only_e)}")
                p()
            if only_l:
                p(f"Level-only offsets: {' '.join(f'{o:#x}' for o in only_l)}")
                p()
        p("## Import block words past the builtin slots")
        p()
        p("| address | name | access (sites) | levels | engine refs |")
        p("|---|---|---|---|---|")
        for a in sorted(glob):
            if gclass(a) == "block":
                acc = ", ".join(f"{k} {sum(v.values())}" for k, v in sorted(glob[a].items()))
                levels = set().union(*[set(v) for v in glob[a].values()])
                p(f"| {a:08x} (slot {(a - BLOCK) // 4}) | {gname(a)} | {acc} | {len(levels)} | {eng_refs[a]} |")
        p()
        p("## Fields of objects returned by builtins (eax after the slot call)")
        p()
        by_s = collections.defaultdict(list)
        for (s, off), c in sorted(ret_fields.items()):
            by_s[s].append(f"{off:#x}({'/'.join(k[0] for k in sorted(c))})")
        for s, offs in sorted(by_s.items()):
            p(f"- slot {s} {functions.get(slots.get(s), '?')}: {' '.join(offs)}")
        p()
        states, _ = fnpack.load_states()
        fixed = set(calls) | set(code_vals) | {slots[x] for x in slot_use}
        p("## Engine functions by decomp state")
        p()
        p("Everything levels reach: direct calls, code values and slot builtins (state from progress.csv).")
        p()
        for kind, group in (("direct call or value", set(calls) | set(code_vals)), ("slot builtin", {slots[x] for x in slot_use})):
            c = collections.Counter(states.get(a, "?") for a in group)
            p(f"- {kind}: {len(group)} ({', '.join(f'{k} {v}' for k, v in c.most_common())})")
        p()
        p("## Direct engine calls (no slot)")
        p()
        p("| function | sites | levels | state |")
        p("|---|---|---|---|")
        for t in sorted(calls, key=lambda t: -sum(calls[t].values())):
            p(f"| {fname(t)} | {sum(calls[t].values())} | {len(calls[t])} | {states.get(t, '?')} |")
        p()
        p("## Shared helpers in another level section")
        p()
        for line in top(xlevel, 10 ** 6, fname):
            p(line)
        p()
        p("## Engine code handed out as values (callbacks)")
        p()
        for line in top(code_vals, 10 ** 6, fname):
            p(line)
        p()
        p("## Level entry points the engine finds (pointers outside the level sections)")
        p()
        p("ModuleEntry functions the engine calls from its module table (the rest of the .text hits are byte")
        p("coincidences, listed for checking). Pointers from .rdata are the levels' own vtables/functor tables")
        p("(dump data, no contract).")
        p()
        rdata_only = 0
        for a, v in sorted(callbacks.items()):
            ext = sorted(x for x in v if x.startswith(("data", "code")))
            if not ext:
                continue
            if all(x.startswith("data .rdata") for x in ext):
                rdata_only += 1
                continue
            p(f"- {a:08x} {functions[a]} ({image.section(a)}): {'; '.join(ext)[:200]}")
        p()
        p(f"Referenced only from .rdata: {rdata_only} level functions.")
        p()
        p("## Slots, by sites")
        p()
        p("| slot | builtin | sites | levels |")
        p("|---|---|---|---|")
        for s in sorted(slot_use, key=lambda s: -sum(slot_use[s].values())):
            p(f"| {s} | {functions.get(slots[s], hex(slots[s]))} | {sum(slot_use[s].values())} | {len(slot_use[s])} |")
        unused = sorted(set(slots) - set(slot_use))
        p()
        p(f"Filled slots no level uses ({len(unused)}): {' '.join(map(str, unused))}")
        other = [r for r in rows if r[0] == "call_other"]
        if other:
            p()
            p(f"## Odd calls ({len(other)}: outside .text and level sections)")
            for r in other[:40]:
                p(f"- {r[1]} {r[2]}: {r[4]}")
    print(f"wrote {out}.md and {out}.csv")


if __name__ == "__main__":
    main()
