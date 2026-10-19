#!/usr/bin/env python3
"""Writes the boilerplate for a batch of functions to decompile: per function a
declaration in game/code/MODULE.h, a stub in MODULE.cpp (an asm entry for register
arguments, the callees as ready title_function<> lines, an #error where the body goes)
and its kFunctions entry (stack bytes, result kind; sorted by address). A new module
gets its files, its CMake entry and its include in functions.cpp.

usage: tools/scaffold.py MODULE FUNCTION... [--dry-run]
  MODULE     game/code/<MODULE>.cpp/.h (created when missing)
  FUNCTION   hex address or symbol name (skipped when already in kFunctions)

The signatures are guesses from fnpack.py's linear scan: parameters are uint32_t
named after where they come from (eax, ecx, xmm1 as float, arg4 = [esp+0x4]); the
result kind comes from what the callers read after the call (AL, EAX, ST0), else
from the writes before each ret. Each printed line says what was guessed: check it
against the disassembly, then rename and retype. The #error keeps an unfinished stub
from building.
"""
import argparse
import bisect
import re
import sys
from pathlib import Path

import fnpack

ROOT = fnpack.ROOT
CODE = ROOT / "game" / "code"
FUNCTIONS_CPP = CODE / "functions.cpp"
CMAKE = CODE / "CMakeLists.txt"
POPS = None
GCC_REGS = ["eax", "edx", "ecx"]  # regparm order
ENTRY_ORDER = ["eax", "ecx", "edx", "ebx", "esi", "edi"]


def analyse(image, functions, starts, va):
    i = bisect.bisect_right(starts, va)
    end = starts[i] if i < len(starts) else va + 0x400
    rows = fnpack.disassemble(image, va, min(end - va, 0x4000))
    read_first, stack_args, ret_sizes, result_writes = fnpack.convention(rows, POPS)
    regs = [r for r, saved in read_first if not saved]
    slots = max(stack_args) + 1 if stack_args else 0
    ret = max(ret_sizes) if ret_sizes else 0
    if ret and ret // 4 > slots:  # callee-popped arguments it never reads
        slots = ret // 4
    return rows, regs, slots, ret, result_writes


def result_from_callers(image, callers):
    """What the code after each call reads first: AL, EAX or the x87 result."""
    votes = []
    for site, kind in callers[:4]:
        if kind != "call":
            continue
        for _, _, text in fnpack.disassemble(image, site + 5, 24)[:4]:
            mnemonic, ops = fnpack.split_operands(text)
            if mnemonic.startswith(("fst", "fcom", "fucom", "fxch")) or (mnemonic.startswith("f") and "st" in text):
                votes.append("St0")
                break
            reads = set()  # the names as written: al vs eax
            for k, op in enumerate(ops):
                if "[" in op or k > 0 or not fnpack.WRITE_ONLY.match(mnemonic):
                    reads |= set(re.findall(r"\b(al|ax|eax)\b", op))
            if "al" in reads:
                votes.append("Al")
                break
            if reads & {"eax", "ax"}:
                votes.append("Eax")
                break
            if ops and fnpack.regs_in(ops[0]) & {"eax"} or mnemonic in ("call", "ret", "jmp"):
                votes.append("None")
                break
    return max(set(votes), key=votes.count) if votes else None


def result_from_rets(rows):
    """The instruction before each ret (past pops, esp adjustments and leave): a write
    to AL/EAX or an x87 load there is the result."""
    kinds = set()
    for i, (_, _, text) in enumerate(rows):
        if not text.startswith("ret"):
            continue
        j = i - 1
        while j >= 0 and re.match(r"(pop|leave|add esp|lea esp|mov esp|fstp st\(0\))", rows[j][2]):
            j -= 1
        before = rows[j][2] if j >= 0 else ""
        mnemonic, ops = fnpack.split_operands(before)
        dest = ops[0] if ops and not fnpack.NO_DEST.match(mnemonic) else ""
        if dest == "al":
            kinds.add("Al")
        elif dest in ("eax", "ax") or mnemonic == "cdq":
            kinds.add("Eax")
        elif mnemonic.startswith("f") and not mnemonic.startswith(("fst", "fist", "fcom", "fucom", "fn", "fldcw",
                                                                    "fxch", "ffree")):
            kinds.add("St0")
        else:
            kinds.add("None")
    for kind in ("Eax", "Al", "St0"):
        if kind in kinds:
            return kind
    return "None"


C_RESULT = {"None": "void", "Al": "uint8_t", "Eax": "uint32_t", "St0": "float", "EdxEax": "uint64_t"}


def signature(name, regs, slots, ret, result):
    """(declaration lines, definition head, asm entry or None, code() target, how)."""
    stack = [f"uint32_t arg{4 + 4 * n:x}" for n in range(slots)]
    ctype = C_RESULT[result]
    odd = [r for r in regs if r in ("ebx", "esi", "edi") or r.startswith("xmm")]
    gcc = [r for r in regs if r in GCC_REGS]
    if odd:
        ordered = [r for r in ENTRY_ORDER if r in regs] + sorted(r for r in regs if r.startswith("xmm"))
        params = [f"float {r}" if r.startswith("xmm") else f"uint32_t {r}" for r in ordered] + stack
        pushes = [f'"pushl {4 * slots}(%esp)\\n"'] * slots
        size = 4 * slots
        for r in reversed(ordered):
            pushes += ([f'"subl $4, %esp\\n"', f'"movss %{r}, (%esp)\\n"'] if r.startswith("xmm")
                       else [f'"pushl %{r}\\n"'])
            size += 4
        body = "\n".join(f"                   {p}" for p in pushes)
        entry = (f"PLATFORM_ASM_ENTRY({name}_entry,\n{body} PLATFORM_ASM_CALL({name}) "
                 f'"addl ${size}, %esp\\n"\n                   "{f"ret ${ret}" if ret else "ret"}")\n')
        head = f'extern "C" PLATFORM_HIDDEN {ctype} XB_CDECL {name}({", ".join(params) or "void"})'
        decl = [head + ";", f'extern "C" void {name}_entry(void);']
        how = f"asm entry ({', '.join(ordered)}" + (f" + {slots} stack" if slots else "") + ")"
        return decl, head.replace('extern "C" PLATFORM_HIDDEN ', ""), entry, f"{name}_entry", how
    pop = "XB_STDCALL" if ret else "XB_CDECL"
    if gcc == ["ecx"] and (ret == 4 * slots):
        cc, params, how = "XB_THISCALL", ["uint32_t ecx"] + stack, "thiscall"
    elif gcc == ["ecx", "edx"] and ret == 4 * slots and ret:
        cc, params, how = "XB_FASTCALL", ["uint32_t ecx", "uint32_t edx"] + stack, "fastcall"
    elif gcc:
        n = max(GCC_REGS.index(r) for r in gcc) + 1
        cc = f"XB_REGS{n} {pop}"
        params = [f"uint32_t {r}" if r in gcc else f"uint32_t unused_{r}" for r in GCC_REGS[:n]] + stack
        how = f"regparm({n}) {pop[3:].lower()}"
    else:
        cc, params, how = pop, stack, pop[3:].lower()
    head = f"{ctype} {cc} {name}({', '.join(params)})"
    return [head + ";"], head, None, name, how + (f", {slots} stack" if slots else "")


def callee_lines(image, functions, starts, ours, rows, va, size):
    lines, seen = [], set()
    for _, _, text in rows:
        mnemonic, ops = fnpack.split_operands(text)
        if mnemonic not in ("call", "jmp") or not ops:
            continue
        try:
            target = int(ops[0].split()[0], 16)
        except ValueError:
            continue
        if va <= target < va + size or target in seen or image.section(target) in fnpack.DATA_SECTIONS:
            continue
        seen.add(target)
        name = functions.get(target, f"FUN_{target:08x}")
        if target in ours:
            lines.append(f"    // {name}(...);  // ours")
            continue
        _, regs, slots, ret, writes = analyse(image, functions, starts, target)
        result = result_from_rets(rows)
        decl, _, entry, _, how = signature("F", regs, slots, ret, result)
        if entry:
            lines.append(f"    // {name} ({target:#010x}): {how} - needs an asm call or title_call_at")
            continue
        ftype = decl[0].rstrip(";").replace(" F(", "(")
        args = re.sub(r"uint32_t |float ", "", ftype[ftype.index("(") + 1:-1])
        lines.append(f"    // title_function<{ftype}>({target:#010x})({args});  // {name}")
    return lines


def insert_before_namespace_end(path, text):
    source = path.read_text()
    marker = "\n}  // namespace game"
    at = source.rindex(marker)
    path.write_text(source[:at].rstrip("\n") + "\n\n" + text.rstrip("\n") + "\n" + source[at:])


def ensure_module(module):
    header, source = CODE / f"{module}.h", CODE / f"{module}.cpp"
    created = []
    if not header.exists():
        header.write_text("#pragma once\n\n// TODO: what this module is.\n\n#include <cstddef>\n#include <cstdint>\n\n"
                          '#include "compiler.h"\n\nnamespace game {\n\n}  // namespace game\n')
        created.append(header.name)
    if not source.exists():
        source.write_text(f'#include "{module}.h"\n\n#include "title_data.h"\n\nnamespace game {{\n\n'
                          "}  // namespace game\n")
        created.append(source.name)
        cmake = CMAKE.read_text()
        files = re.search(r"set\(game_code_sources\n(.*?)\)", cmake, re.S)
        names = sorted(set(files.group(1).split()) | {f"{module}.cpp"})
        CMAKE.write_text(cmake[:files.start(1)] + "".join(f"    {n}\n" for n in names) + cmake[files.end(1):])
    functions = FUNCTIONS_CPP.read_text()
    include = f'#include "{module}.h"\n'
    if include not in functions:
        includes = re.findall(r'#include "(\w+)\.h"\n', functions)
        block = [i for i in includes if i != "functions"]
        anchor = next((f'#include "{i}.h"\n' for i in sorted(block) if i > module), None)
        if anchor:
            functions = functions.replace(anchor, include + anchor, 1)
        else:
            last = f'#include "{sorted(block)[-1]}.h"\n'
            functions = functions.replace(last, last + include, 1)
        FUNCTIONS_CPP.write_text(functions)
    return created


def register(entries):
    """Insert kFunctions lines before the first entry with a larger address."""
    lines = FUNCTIONS_CPP.read_text().split("\n")
    for address, line in sorted(entries):
        rows = [(i, int(m.group(1), 16)) for i, l in enumerate(lines)
                for m in [re.match(r'\s+\{"\w+", 0x([0-9a-fA-F]+),', l)] if m]
        at = next((i for i, a in rows if a > address), rows[-1][0] + 1)
        lines.insert(at, line)
    FUNCTIONS_CPP.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("module")
    parser.add_argument("functions", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    functions, _ = fnpack.load_names()
    by_name = {n: a for a, n in functions.items()}
    starts = sorted(functions)
    _, ours = fnpack.load_states()
    image = fnpack.Image()
    targets = []
    for item in args.functions:
        va = by_name.get(item)
        if va is None:
            try:
                va = int(item, 16)
            except ValueError:
                sys.exit(f"{item}: no such symbol")
        if va in ours and not args.dry_run:  # a dry run shows known ones too: a check of the guesses
            print(f"{item}: already in kFunctions, skipped")
            continue
        targets.append(va)
    callers = image.code_refs(targets)
    global POPS
    POPS = fnpack.pops_reader(image, starts)

    decls, defs, entries, report = [], [], [], []
    for va in targets:
        name = functions.get(va, f"FUN_{va:08x}")
        if name.startswith("FUN_"):
            print(f"{name}: unnamed - name it first (naming.md); scaffolded under its FUN_ name")
        rows, regs, slots, ret, writes = analyse(image, functions, starts, va)
        size = rows[-1][0] + rows[-1][1] - va if rows else 0
        from_callers = result_from_callers(image, callers.get(va, []))
        result = from_callers or result_from_rets(rows)
        decl, head, entry, target, how = signature(name, regs, slots, ret, result)
        decls.append(f"// {va:#010x}: TODO.\n" + "\n".join(decl))
        callees = callee_lines(image, functions, starts, ours, rows, va, size)
        stub = (entry + "\n" if entry else "") + head + " {\n" + "".join(c + "\n" for c in callees) \
            + f'#error "decompile {name} ({va:#010x})"\n' + "}\n"
        defs.append(stub)
        entries.append((va, f'    {{"{name}", {va:#010x}, 0, code(&{target}), {4 * slots}, '
                            f"Result::{result}, Status::Compare}},"))
        report.append(f"{name} {va:08x}: {how}, ret {ret:#x}, result {result} "
                      f"({'callers' if from_callers else 'ret writes'}), {len(callees)} callees")

    if args.dry_run:
        print("\n".join(decls) + "\n\n" + "\n".join(defs) + "\n" + "\n".join(line for _, line in entries))
    elif targets:
        created = ensure_module(args.module)
        insert_before_namespace_end(CODE / f"{args.module}.h", "\n".join(decls))
        insert_before_namespace_end(CODE / f"{args.module}.cpp", "\n".join(defs))
        register(entries)
        if created:
            print(f"created {', '.join(created)} (CMake list and functions.cpp include added)")
    print("\n".join(report))


if __name__ == "__main__":
    main()
