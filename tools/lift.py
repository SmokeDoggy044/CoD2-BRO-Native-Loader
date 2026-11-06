#!/usr/bin/env python3
"""Lifts Ghidra decompiler output of level script bodies into draft C in our style.

Input: tools/ghidra/ExportDecompile.java output (DIR/<addr>.c, .sig, .glob, .str) and build/protos (run
tools/protos.py + ApplyPrototypes.java first, so callees and slots decompile with their arguments).
Rewrites: Ghidra types -> fixed-width types; the SEH frame and EH-state stores removed (write-only variables,
iterated); globals -> title_global<T>(address); string literals -> pointer<char>(address) with the text in a
comment; script-interface slot calls -> slot<our prototype>(slot address); calls to functions without C ->
title_function<Ghidra's prototype>(address); the function's own header from the decompiler's prototype.
Left for the agent: what the draft marks with LIFT (register arguments, unknown callee prototypes, Ghidra
helpers like CONCAT31), and every semantic check (vc71match, compare).

Output: OUT/<addr>.cpp per function and OUT/report.txt (per function: g++ -fsyntax-only errors, LIFT marks).
usage: tools/lift.py RAW_DIR OUT_DIR
"""
import csv
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CODE = ROOT / "game" / "code"
PROTOS = ROOT / "build" / "protos"

GTYPES = {"undefined8": "uint64_t", "undefined4": "uint32_t", "undefined2": "uint16_t", "undefined1": "uint8_t",
          "undefined": "uint8_t", "uint": "uint32_t", "int": "int32_t", "ushort": "uint16_t", "short": "int16_t",
          "byte": "uint8_t", "sbyte": "int8_t", "ulonglong": "uint64_t", "longlong": "int64_t", "dword": "uint32_t",
          "word": "uint16_t", "code": "uint8_t", "uchar": "uint8_t", "char": "char", "float": "float",
          "double": "double", "bool": "bool", "void": "void", "float10": "long double"}
CONV = {"__thiscall": "XB_THISCALL", "__stdcall": "XB_STDCALL", "__cdecl": "XB_CDECL", "__fastcall": "XB_FASTCALL"}


def known_structs():
    return {r["name"] for r in csv.DictReader(open(PROTOS / "structs.csv"))}


STRUCTS = None


def ctype(g):
    """Ghidra display type -> our C type (unknown structs -> uint8_t)."""
    g = g.strip()
    stars = g.count("*")
    base = g.replace("*", "").strip()
    if base.endswith("]"):
        return None
    base = GTYPES.get(base, base if base in STRUCTS else None)
    if base is None:
        base = "uint8_t" if stars else "uint32_t"
    return base + "*" * stars


def map_types(code):
    def sub(m):
        return GTYPES[m.group(0)]
    return re.sub(r"\b(" + "|".join(sorted((k for k in GTYPES if GTYPES[k] != k), key=len, reverse=True)) +
                  r")\b", sub, code)


def protect(code):
    """Split code into (text, is_string) pieces so rewrites skip string literals."""
    return re.split(r'("(?:[^"\\]|\\.)*")', code)


def storage_conv(params, purge, model):
    regs = [p.split("@")[1] for p in params if "@" in p]
    if regs and regs[0].startswith("ECX") and not any(r.startswith(("EAX", "EDX")) for r in regs):
        return "XB_THISCALL"
    if any(r.startswith(("EAX", "EDX", "ECX")) for r in regs):
        return None  # register arguments: the agent writes the convention
    if purge and int(purge) > 0:
        return "XB_STDCALL"
    return "XB_CDECL"


def parse_sig(line):
    addr, name, model, ret, params, varargs, custom, purge = line.split(";")
    params = [p for p in params.split("|") if p]
    return dict(addr=int(addr, 16), name=name, model=model, ret=ret, params=params, varargs=varargs == "1",
                purge=int(purge))


def fn_type(sig, conv):
    ret = ctype(sig["ret"]) or "uint32_t"
    ps = [ctype(p.split("@")[0].rsplit(" ", 1)[0]) or "uint32_t" for p in sig["params"]]
    if sig["varargs"]:
        ps.append("...")
    return f"{ret} {conv}({', '.join(ps)})"


def main():
    global STRUCTS, SYMBOLS
    SYMBOLS = {}
    for line in open(ROOT / "tools" / "data" / "default_xbe_symbols.txt"):
        f = line.split()
        if len(f) >= 2 and re.fullmatch(r"[0-9a-fA-F]{8}", f[0]):
            SYMBOLS.setdefault(f[1], int(f[0], 16))
    raw, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    STRUCTS = known_structs()
    protos = {int(r["address"], 16): r for r in csv.DictReader(open(PROTOS / "functions.csv"))}
    c_names = {r["name"] for r in protos.values()}
    slots = {int(r["slot"], 16): int(r["builtin"], 16) for r in csv.DictReader(open(PROTOS / "slots.csv"))}
    cmd = next(e["command"] for e in json.load(open(ROOT / "build" / "linux-loader" / "compile_commands.json"))
               if "/game/code/" in e["file"])
    incs = re.findall(r"-I\S+", cmd)
    headers = "".join(f'#include "{p.name}"\n' for p in sorted(CODE.glob("*.h")))
    report = []

    for cfile in sorted(raw.glob("*.c")):
        a = cfile.stem
        text = cfile.read_text()
        sigs = [parse_sig(l) for l in (raw / f"{a}.sig").read_text().split("\n") if l]
        own, callees = sigs[0], {s["name"]: s for s in sigs[1:]}
        globs = {}
        for l in (raw / f"{a}.glob").read_text().split("\n"):
            if l:
                n, addr, t = l.split(";", 2)
                globs[n] = (int(addr, 16), t)
        strs = {}
        for l in (raw / f"{a}.str").read_text().split("\n"):
            if l:
                addr, s = l.split(";", 1)
                strs.setdefault(s, int(addr, 16))
        marks = []

        body = text[text.index("{"):]
        body = re.sub(r"/\* WARNING:[^*]*\*/\n?", "", body)
        # SEH frame
        body = "\n".join(l for l in body.split("\n") if "unaff_FS_OFFSET" not in l)
        # write-only locals (EH state, the registration record)
        for _ in range(4):
            decls = re.findall(r"^  [\w *]+?\**\b(\w+)(?: \[\d+\])?;$", body, re.M)
            changed = False
            for v in decls:
                stmt = re.compile(rf"^\s*{v}(?:\._\d+_\d+_)? = [^;]*;\n", re.M)
                rest = stmt.sub("", body)
                if len(re.findall(rf"\b{v}\b", rest)) == 1:  # only the declaration is left
                    body = re.sub(rf"^  [\w *]+?\**\b{v}(?: \[\d+\])?;\n", "", stmt.sub("", body), flags=re.M)
                    changed = True
            if not changed:
                break

        pieces = protect(body)
        for i in range(0, len(pieces), 2):
            p = pieces[i]
            # slot calls
            def slot_call(m):
                n = m.group(1)
                if n not in globs:
                    marks.append(f"slot {n}")
                    return m.group(0)
                s_addr = globs[n][0]
                b = slots.get(s_addr)
                pr = protos.get(b) if b else None
                if pr and pr["storage"]:
                    st = pr["storage"].split(";")[0].split("|")
                    purge = int(pr["storage"].split("purge:")[1])
                    nstack = sum(1 for w in st if w.startswith("stack"))
                    if all(w.startswith("stack") for w in st):
                        pr = dict(pr, convention="stdcall" if purge else "cdecl", storage="")
                    elif st and st[0] == "ecx" and all(w.startswith("stack") for w in st[1:]) and purge == 4 * nstack:
                        pr = dict(pr, convention="thiscall", storage="")
                if pr and pr["name"] and not pr["params"] and pr["varargs"] == "1":
                    marks.append(f"slot {n}: stack arguments through a pointer (count from the call)")
                if not pr or pr["storage"]:
                    marks.append(f"slot {n} prototype")
                    return f"slot<LIFT_SLOT_{n}>({s_addr:#010x})("
                ps = [t for t in pr["params"].split("|") if t] + (["..."] if pr["varargs"] == "1" else [])
                conv = {"thiscall": "XB_THISCALL", "stdcall": "XB_STDCALL", "fastcall": "XB_FASTCALL"}.get(
                    pr["convention"], "XB_CDECL")
                if int(pr["regs"]):
                    marks.append(f"slot {n} regparm")
                return f"slot<{pr['return']} {conv}({', '.join(ps)})>({s_addr:#010x})(  /* {pr['name']} */ "
            p = re.sub(r"\(\*(\w+)\)\(", slot_call, p)
            # calls to functions without C
            def call(m):
                n = m.group(1)
                if n in c_names or n not in callees:
                    return m.group(0)
                s = callees[n]
                conv = storage_conv(s["params"], s["purge"], s["model"])
                if conv is None:
                    marks.append(f"register arguments: {n}")
                    conv = "XB_CDECL /*LIFT regs*/"
                return f"title_function<{fn_type(s, conv)}>({s['addr']:#010x})(  /* {n} */ "
            p = re.sub(r"(?<![\w.>])(\w+)\(", call, p)
            # globals
            def glob(m):
                n = m.group(0)
                if n not in globs:
                    hx = re.fullmatch(r"(?:DAT|PTR|LAB|UNK|s|u)_([0-9a-f]{8})", n) or \
                        re.fullmatch(r"(?:PTR|s|u)_\w+_([0-9a-f]{8})", n)
                    if hx:
                        globs[n] = (int(hx.group(1), 16), "undefined")
                    elif n in SYMBOLS and n not in callees and n not in c_names:
                        globs[n] = (SYMBOLS[n], "undefined1")
                    else:
                        return n
                addr, t = globs[n]
                ct = ctype(t) if t != "undefined" else "uint32_t"
                if ct is None:
                    ct = "uint8_t"
                    return f"pointer<uint8_t>({addr:#010x})"  # array global: its address
                return f"title_global<{ct}>({addr:#010x})"
            p = re.sub(r"\b[A-Za-z_]\w*\b", glob, p)
            p = map_types(p)
            for helper in ("CONCAT", "SUB4", "ZEXT", "SEXT", "CARRY", "SBORROW", "POPCOUNT", "_0_1_", "_1_3_"):
                if helper in p:
                    marks.append(f"Ghidra helper {helper}")
            pieces[i] = p
        for i in range(1, len(pieces), 2):
            s = pieces[i]
            if s in strs:
                pieces[i] = f"pointer<char>({strs[s]:#010x}) /* {s} */"
            else:
                marks.append(f"string without address {s[:30]}")
        body = "".join(pieces)

        conv = storage_conv(own["params"], own["purge"], own["model"])
        if conv is None:
            marks.append("own register arguments")
            conv = "XB_CDECL /*LIFT regs*/"
        ret = ctype(own["ret"]) or "uint32_t"
        params = []
        for k, p in enumerate(own["params"]):
            t, n = p.split("@")[0].rsplit(" ", 1)
            params.append(f"{ctype(t) or 'uint32_t'} {n}")
        if own["varargs"]:
            params.append("...")
        head = f"// {own['addr']:#010x} (tools/lift.py draft)\n{ret} {conv} {own['name']}({', '.join(params)})"
        src = head + " " + body.strip() + "\n"
        (out / f"{a}.cpp").write_text(src)

        test = out / f"{a}.check.cpp"
        test.write_text(headers + '#include "bro_scr_ai_common.h"\nnamespace game {\nusing bro_ai::pointer;\nusing bro_ai::slot;\n' + src + "}\n")
        r = subprocess.run(["c++", "-m32", "-std=c++20", "-fsyntax-only", "-w", *incs, str(test)],
                           capture_output=True, text=True)
        errors = [l for l in r.stderr.split("\n") if ": error:" in l and str(test) in l]
        test.unlink()
        report.append((a, own["name"], len(errors), sorted(set(marks)), errors[:3]))

    with open(out / "report.txt", "w") as f:
        clean = sum(1 for r in report if not r[2])
        f.write(f"{len(report)} drafts, {clean} compile as they are\n")
        for a, n, e, marks, errs in report:
            f.write(f"{a} {n}: {e} errors; marks: {', '.join(marks) or '-'}\n")
            for x in errs:
                f.write("    " + x.split(": error: ", 1)[1][:160] + "\n")
    print((out / "report.txt").read_text().split("\n")[0])


if __name__ == "__main__":
    main()
