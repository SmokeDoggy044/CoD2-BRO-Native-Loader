#!/usr/bin/env python3
"""Generates the hash -> global tables of the anim-handle dispatchers (BroScr_BAnimHandles_<tree>).

Each dispatcher is a compiled binary compare tree on a hash register (`cmp eax,imm; ja; je`); every
key ends in the same leaf shape:

    test OUT,OUT; je set
    mov R,[global]; mov [OUT],R; mov eax,[global]; ret      get: *out = global, return it
  set:
    mov [global],VALUE; mov eax,VALUE; ret                  set: global = value, return it

and every other value ends in `xor eax,eax; ret`. R is EAX or one scratch register per leaf (such a
leaf leaves the global in that register: EDX in generic_human, ECX in mg42). This script walks the tree for every compared key and for both neighbours of each
key (so every interval between keys is checked to reach the default), checks each leaf against the
shape word by word, and writes one sorted table per function:

    game/code/bro_scr_anim_handles_tables.inc   {hash, global, register the get leaf loads: 0 EAX, 1 ECX, 2 EDX}

usage: tools/gen_anim_handles.py FUNCTION... [--dry-run]
  FUNCTION  symbol name or hex address of a dispatcher (its size comes from the symbol list)
"""
import argparse
import bisect
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402

OUT = fnpack.ROOT / "game" / "code" / "bro_scr_anim_handles_tables.inc"
JUMPS = {  # on (old value, immediate) of the last cmp/sub/dec
         "ja": lambda v, k: v > k, "jae": lambda v, k: v >= k, "jb": lambda v, k: v < k,
         "jbe": lambda v, k: v <= k, "je": lambda v, k: v == k, "jne": lambda v, k: v != k}


def step(text, key_reg, v):
    """One tree instruction on the key value v: (new v, flags as (old v, imm)) or None."""
    m = re.fullmatch(rf"(cmp|sub) {key_reg},0x([0-9a-f]+)", text)
    if m:
        k = int(m.group(2), 16)
        return (v if m.group(1) == "cmp" else (v - k) & 0xffffffff), (v, k)
    if text == f"dec {key_reg}":  # ZF only: only je/jne may follow (checked by the caller)
        return (v - 1) & 0xffffffff, (v, 1)
    return None


def walk(rows, entry, key_reg, value):
    """Follows the compare tree for one key value; returns the leaf address or 'default'."""
    pc, flags, v, zf_only = entry, None, value, False
    for _ in range(200):
        text = rows[pc][1]
        r = step(text, key_reg, v)
        if r:
            v, flags = r
            zf_only = text.startswith("dec")
            pc = rows[pc][0]
            continue
        m = re.fullmatch(r"(j\w+) 0x([0-9a-f]+)", text)
        if m and m.group(1) in JUMPS:
            if zf_only and m.group(1) not in ("je", "jne"):
                sys.exit(f"{pc:#x}: {m.group(1)} after dec")
            pc = int(m.group(2), 16) if JUMPS[m.group(1)](*flags) else rows[pc][0]
            continue
        if m and m.group(1) == "jmp":
            pc = int(m.group(2), 16)
            continue
        if text == "xor eax,eax" and rows[rows[pc][0]][1] == "ret":
            return "default"
        if re.fullmatch(r"test (\w+),\1", text):
            return pc
        sys.exit(f"{pc:#x}: unexpected '{text}' in the compare tree")
    sys.exit(f"compare tree too deep at {pc:#x}")


def keys_of(rows, entry, key_reg):
    """Every key that reaches a leaf: a DFS over both sides of each branch with the running
    offset (sub/dec); a leaf is reached by equality only, which fixes the key."""
    keys, todo, seen = set(), [(entry, 0, None, None)], set()
    while todo:
        pc, off, last, eq = todo.pop()
        if (pc, off, last, eq) in seen:
            continue
        seen.add((pc, off, last, eq))
        text = rows[pc][1]
        m = re.fullmatch(rf"(cmp|sub|dec) {key_reg}(?:,0x([0-9a-f]+))?", text)
        if m:
            k = int(m.group(2), 16) if m.group(2) else 1
            todo.append((rows[pc][0], off + k if m.group(1) != "cmp" else off, (k + off) & 0xffffffff, None))
            continue
        m = re.fullmatch(r"(j\w+) 0x([0-9a-f]+)", text)
        if m and m.group(1) == "jmp":
            todo.append((int(m.group(2), 16), off, last, eq))
        elif m:
            j, t, nxt = m.group(1), int(m.group(2), 16), rows[pc][0]
            todo.append((t, off, last, last if j == "je" else eq))
            todo.append((nxt, off, last, last if j == "jne" else eq))
        elif re.fullmatch(r"test (\w+),\1", text):
            if eq is None:
                sys.exit(f"{pc:#x}: leaf reached without an equality test")
            keys.add(eq)
        elif not (text == "xor eax,eax" and rows[rows[pc][0]][1] == "ret"):
            sys.exit(f"{pc:#x}: unexpected '{text}' in the compare tree")
    return sorted(keys)


def leaf(rows, pc):
    """Checks one leaf; returns (out_reg, value_reg, global, get_reg)."""
    seq = []
    for _ in range(9):
        seq.append(rows[pc][1])
        pc = rows[pc][0]
    m = re.fullmatch(r"test (\w+),\1", seq[0])
    out = m.group(1)
    je = re.fullmatch(r"je 0x([0-9a-f]+)", seq[1])
    g1 = re.fullmatch(r"mov (eax|ecx|edx),(?:DWORD PTR )?ds:0x([0-9a-f]+)", seq[2])
    if not (je and g1):
        sys.exit(f"leaf shape: {seq}")
    reg, glob = g1.group(1), g1.group(2)
    want = [f"mov DWORD PTR [{out}],{reg}", f"mov eax,ds:0x{glob}", "ret"]
    want_set = re.fullmatch(rf"mov DWORD PTR ds:0x{glob},(\w+)", seq[6])
    if seq[3:6] != want or not want_set:
        sys.exit(f"leaf shape: {seq}")
    value = want_set.group(1)
    if seq[7:9] != [f"mov eax,{value}", "ret"]:
        sys.exit(f"leaf shape: {seq}")
    return out, value, int(glob, 16), reg


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("functions", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    functions, _ = fnpack.load_names()
    by_name = {n: a for a, n in functions.items()}
    starts = sorted(functions)
    image = fnpack.Image()
    blocks = ["// Generated by tools/gen_anim_handles.py - do not edit. {hash, global, register the get leaf loads: 0 EAX, 1 ECX, 2 EDX}"]
    for item in args.functions:
        va = by_name[item] if item in by_name else int(item, 16)
        name = functions.get(va, f"FUN_{va:08x}")
        end = starts[bisect.bisect_right(starts, va)]
        code = fnpack.disassemble(image, va, end - va)
        rows = {}
        for i, (a, _size, text) in enumerate(code):
            rows[a] = (code[i + 1][0] if i + 1 < len(code) else None, text)
        key_reg = re.fullmatch(r"cmp (\w+),0x[0-9a-f]+", rows[va][1]).group(1)
        keys = keys_of(rows, va, key_reg)
        entries, roles, scratch = [], set(), set()
        for k in keys:
            target = walk(rows, va, key_reg, k)
            if target == "default":
                sys.exit(f"{name}: key {k:#x} reaches the default")
            out, value, glob, reg = leaf(rows, target)
            roles.add((out, value))
            entries.append((k, glob, ("eax", "ecx", "edx").index(reg)))
            if reg != "eax":
                scratch.add(reg)
            for n in (k - 1, k + 1):
                if 0 <= n <= 0xffffffff and n not in keys and walk(rows, va, key_reg, n) != "default":
                    sys.exit(f"{name}: non-key {n:#x} reaches a leaf")
        if len(roles) != 1:
            sys.exit(f"{name}: mixed register roles {roles}")
        out, value = roles.pop()
        print(f"{name}: {len(entries)} keys, key {key_reg}, out {out}, value {value}, "
              f"{sum(e[2] != 0 for e in entries)} leaves load {'/'.join(sorted(scratch)) or '-'}, {len({e[1] for e in entries})} globals")
        blocks.append(f"// {name} ({va:#010x}): key {key_reg}, out pointer {out}, value {value}, "
                      f"scratch {'/'.join(sorted(scratch)) or '-'}.")
        blocks.append(f"static const AnimHandleEntry k{name}[] = {{")
        blocks += [f"    {{0x{k:08x}, 0x{g:08x}, {e}}}," for k, g, e in entries]
        blocks.append("};")
    if not args.dry_run:
        OUT.write_text("\n".join(blocks) + "\n")
        print(f"wrote {OUT.relative_to(fnpack.ROOT)}")


if __name__ == "__main__":
    main()
