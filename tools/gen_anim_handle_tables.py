#!/usr/bin/env python3
"""Generates the level-script anim-handle tables (src/engine/levelscript/bro_scr_anim_handles.inc).

Each BroScr_AnimHandles_<set> function (0x00123fc0..0x00124a30) maps an anim-name hash to a
.data word holding that anim's handle: a compiled switch (binary compare tree, or a chain of
`sub`/`dec` + `je`) whose cases all read `test out,out; je set; mov r,[G]; mov [out],r; mov
eax,[G]; ret; set: mov [G],edx; mov eax,edx; ret`. The hash comes in EAX or ECX, the out
pointer in the other one, the value in EDX. This script walks every path of each function's
switch (tracking the `sub`/`dec` offsets), checks each case block has that shape, and writes
one line per function: BRO_ANIM_HANDLE_TABLE(name, hash register, {hash, global}...).

usage: tools/gen_anim_handle_tables.py [--dry-run]
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402
import engine_src

OUT = engine_src.path("bro_scr_anim_handles.inc")

TABLES = [
    ("BroScr_AnimHandles_tankdeadguys", 0x00123FC0),
    ("BroScr_AnimHandles_flakvierling", 0x00124030),
    ("BroScr_AnimHandles_generic_human", 0x001240A0),
    ("BroScr_AnimHandles_treeburst", 0x00124720),
    ("BroScr_AnimHandles_flak88", 0x00124790),
    ("BroScr_AnimHandles_panzeriv", 0x00124960),
    ("BroScr_AnimHandles_stuka", 0x001249B0),
    ("BroScr_AnimHandles_willyjeep", 0x00124A00),
    ("BroScr_AnimHandles_flakvierling_base", 0x00124A30),
]

IMM = r"(0x[0-9a-f]+)"


def code(image, va):
    """{address: (next address, text)} for the function body (up to the next function)."""
    lines = {}
    size = 0x1000
    for addr, n, text in fnpack.disassemble(image, va, size):
        lines[addr] = (addr + n, re.sub(r"<[^>]*>", "", text).strip())
        if addr > va and text.startswith("int3"):
            break
    return lines


def case_block(lines, addr, out_reg):
    """The global a case block reads/writes; checks the block's exact shape."""
    seq = []
    a = addr
    for _ in range(9):
        nxt, text = lines[a]
        seq.append(text)
        a = nxt
    m = re.fullmatch(rf"test {out_reg},{out_reg}", seq[0])
    assert m, (hex(addr), seq)
    assert seq[1].startswith("je "), seq
    g = re.search(r"ds:(0x[0-9a-f]+)", seq[2])
    assert g, seq
    glob = int(g.group(1), 16)
    assert re.fullmatch(rf"mov DWORD PTR \[{out_reg}\],(e[a-d]x)", seq[3]), seq
    assert seq[4] == f"mov eax,ds:0x{glob:x}" and seq[5] == "ret", seq
    set_addr = int(seq[1].split()[1], 16)
    s1 = lines[set_addr][1]
    s2 = lines[lines[set_addr][0]][1]
    s3 = lines[lines[lines[set_addr][0]][0]][1]
    assert s1 == f"mov DWORD PTR ds:0x{glob:x},edx" and s2 == "mov eax,edx" and s3 == "ret", (s1, s2, s3)
    return glob


def walk(name, va, image):
    lines = code(image, va)
    first = lines[va][1]
    hash_reg = re.match(r"(?:cmp|sub) (e[a-d]x),", first).group(1)
    out_reg = {"eax": "ecx", "ecx": "eax"}[hash_reg]
    entries = {}
    seen = set()
    stack = [(va, 0, None)]  # address, offset subtracted so far, pending compare value
    while stack:
        a, offset, cmp = stack.pop()
        if (a, offset, cmp) in seen:
            continue
        seen.add((a, offset, cmp))
        nxt, text = lines[a]
        op = text.split()[0]
        if op == "cmp":
            m = re.fullmatch(rf"cmp {hash_reg},{IMM}", text)
            assert m, text
            stack.append((nxt, offset, int(m.group(1), 16) + offset))
        elif op == "sub":
            m = re.fullmatch(rf"sub {hash_reg},{IMM}", text)
            assert m, text
            offset = (offset + int(m.group(1), 16)) & 0xFFFFFFFF
            stack.append((nxt, offset, offset))
        elif op == "dec":
            assert text == f"dec {hash_reg}", text
            offset = (offset + 1) & 0xFFFFFFFF
            stack.append((nxt, offset, offset))
        elif op in ("je", "jne", "ja", "jb", "jae", "jbe"):
            target = int(text.split()[1], 16)
            if op == "je":
                h = cmp & 0xFFFFFFFF
                glob = case_block(lines, target, out_reg)
                assert entries.get(h, glob) == glob, (name, hex(h))
                entries[h] = glob
                stack.append((nxt, offset, cmp))
            elif op == "jne":
                h = cmp & 0xFFFFFFFF
                glob = case_block(lines, nxt, out_reg)
                assert entries.get(h, glob) == glob, (name, hex(h))
                entries[h] = glob
                stack.append((target, offset, cmp))
            else:
                stack.append((target, offset, cmp))
                stack.append((nxt, offset, cmp))
        elif op == "xor":
            assert text == "xor eax,eax" and lines[nxt][1] == "ret", text
        else:
            raise SystemExit(f"{name}: unexpected {text} at {a:#x}")
    return hash_reg, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    image = fnpack.Image()
    out = ["// Generated by tools/gen_anim_handle_tables.py - rerun it instead of editing.",
           "// BRO_ANIM_HANDLE_TABLE(name, address, hash register, {anim hash, handle word}...)"]
    total = 0
    for name, va in TABLES:
        hash_reg, entries = walk(name, va, image)
        total += len(entries)
        rows = ", ".join(f"{{0x{h:08x}, 0x{g:08x}}}" for h, g in sorted(entries.items()))
        out.append(f"BRO_ANIM_HANDLE_TABLE({name}, 0x{va:08x}, {hash_reg}, {rows})")
        print(f"{name}: hash in {hash_reg}, {len(entries)} cases")
    text = "\n".join(out) + "\n"
    if a.dry_run:
        print(text)
    else:
        OUT.write_text(text)
        print(f"wrote {OUT} ({total} cases)")


if __name__ == "__main__":
    main()
