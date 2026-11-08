#!/usr/bin/env python3
"""Exports our C prototypes for Ghidra (tools/ghidra/ApplyPrototypes.java).

Every kFunctions entry whose code is a plain C function (code(&Name)) gets that function's declaration from
src/engine (*.h, *.cpp, *.inc), translated to Ghidra types. The script-interface slots (0x00922140 block, filled
by the levelreach.py init functions) get the prototype of the builtin stored in them.

Types: fixed-width scalars -> Ghidra builtins; pointers to our structs -> placeholder structs of that name
(category /bro); structs passed by value -> structs of their real size (a compiled sizeof probe). Skipped and
counted: asm entries that are not plain push sequences, parameters that are function pointers or references.

Output: build/protos/functions.csv (address,name,convention,regs,return,params and param_names with '|' between,
varargs,
storage = explicit parameter storage from an asm shim: eax|stack:8;purge:N, else empty),
build/protos/slots.csv (slot address, builtin address), build/protos/structs.csv (name,size; 0 = pointer only).

usage: tools/protos.py
"""
import collections
import csv
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402
import levelreach  # noqa: E402
import engine_src

ROOT = fnpack.ROOT
CODE = engine_src.ENGINE
OUT = ROOT / "build" / "protos"
ENTRY = re.compile(r'^    \{"(\w+)", (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+|0), code\(&?(\w+)\),', re.M)
CONV = r"(?:XB_(?:CDECL|STDCALL|THISCALL|FASTCALL|REGS[123])\s+)+"
DECL = re.compile(r'^(?:extern "C" )?(?:PLATFORM_HIDDEN )?(?:static )?(?:inline )?([\w:<>\s*]+?)\s*\b(' + CONV +
                  r')(\w+)\(([^;{()]*)\)\s*(?:noexcept\s*)?[;{]', re.M)
SCALARS = {"uint32_t": "uint", "int32_t": "int", "uint8_t": "byte", "int8_t": "sbyte", "uint16_t": "ushort",
           "int16_t": "short", "float": "float", "double": "double", "bool": "bool", "char": "char",
           "void": "void", "uintptr_t": "uint", "size_t": "uint", "unsigned": "uint", "int": "int",
           "uint64_t": "ulonglong", "int64_t": "longlong", "long double": "float10"}


def clean(t):
    t = re.sub(r"\b(const|volatile|struct|class|game::|std::)\b\s*", "", t)
    return re.sub(r"\s*\*", "*", t).strip()


def param_name(p):
    p = p.split("=")[0].strip()
    m = re.match(r"^.*?[\w*>]\s*\b(\w+)$", p)
    return m.group(1) if m and not re.fullmatch(r"(const\s+)?\w+", p) and m.group(1) not in SCALARS else ""


def param_type(p):
    p = p.split("=")[0].strip()
    if not p or p == "void":
        return None
    if p == "...":
        return "..."
    m = re.match(r"^(.*?[\w*&>])\s*\b\w+$", p)  # drop the parameter name
    t = clean(m.group(1) if m and not re.fullmatch(r"(const\s+)?\w+", p) else p)
    return t


SHIM = re.compile(r'PLATFORM_ASM_ENTRY\((\w+)_entry,\s*((?:"[^"]*"\s*|PLATFORM_ASM_CALL\(\w+\)\s*)+)\)', re.S)
ARGS_SHIM = re.compile(r"^(?:SCRIPT_ARGS_ENTRY|BRO_SCR_ARGS_ENTRY)\((\w+)\)", re.M)


def shims():
    """C name -> storage list for asm entries that only push registers/stack words before calling the C body:
    the C parameters are the last pushes (earlier ones save registers), first parameter pushed last.
    Also C name -> "args" for the SCRIPT_ARGS shims (stack arguments read through a pointer)."""
    out = {}
    for path in engine_src.files("*.cpp") + engine_src.files("*.inc"):
        text = path.read_text()
        for m in ARGS_SHIM.finditer(text):
            out[m.group(1)] = "args"
        for m in SHIM.finditer(text):
            body = m.group(2)
            if re.findall(r"PLATFORM_ASM_CALL\((\w+)\)", body) != [m.group(1)]:
                continue
            split = lambda part: [i.strip() for q in re.findall(r'"([^"]*)"', part) for i in q.split("\\n") if i.strip()]
            before, after = body.split("PLATFORM_ASM_CALL", 1)
            pushes = split(before)
            rest = split(after.split(")", 1)[1])
            storage = []
            for n, ins in enumerate(pushes):
                r = re.fullmatch(r"pushl %(e\w\w)", ins)
                st = re.fullmatch(r"pushl (0x[0-9a-f]+|\d+)\(%esp\)", ins)
                if r:
                    storage.append(r.group(1))
                elif st:
                    off = int(st.group(1), 0) - 4 * n
                    storage.append(f"stack:{off}" if off >= 4 else None)
                else:
                    storage = None
                    break
            if storage is None:
                continue
            ret = next((i for i in rest if i.startswith("ret")), "ret")
            purge = int(ret.split("$")[1], 0) if "$" in ret else 0
            out[m.group(1)] = (storage, purge)
    return out


def main():
    text = (engine_src.FUNCTIONS_CPP).read_text()
    entries = [(int(m.group(2), 16), m.group(4)) for m in ENTRY.finditer(text)]
    decls = {}
    for path in engine_src.files("*.h") + engine_src.files("*.cpp") + engine_src.files("*.inc"):
        for m in DECL.finditer(path.read_text()):
            decls.setdefault(m.group(3), (m.group(1), m.group(2), m.group(4)))
    aliases = {}  # inline constexpr auto* X = &Template<...>;
    for path in engine_src.files("*.h") + engine_src.files("*.cpp"):
        for m in re.finditer(r"constexpr auto\*?\s+(\w+)\s*=\s*&(\w+)<", path.read_text()):
            aliases.setdefault(m.group(1), m.group(2))
    for alias, template in aliases.items():
        if alias not in decls and template in decls:
            decls[alias] = decls[template]

    skipped = collections.Counter()
    structs = {}
    rows = {}
    shim = shims()
    for address, entry in entries:
        if address in rows:
            continue
        cname = entry[:-6] if entry.endswith("_entry") and entry[:-6] in shim else entry
        storage = ""
        if cname != entry:
            sh = shim[cname]
            if sh == "args":  # stack arguments through a pointer: count unknown here
                rows[address] = (address, cname, "cdecl", 0, "uint32_t", "", 1, "", "")
                continue
            if cname not in decls:
                skipped["shim without a declaration"] += 1
                continue
            nparams = len([p for p in decls[cname][2].split(",") if param_type(p) not in (None, "...")])
            regs_pushed = sh[0]
            if nparams > len(regs_pushed) or None in regs_pushed[len(regs_pushed) - nparams:]:
                skipped["shim pushes do not cover the parameters"] += 1
                continue
            args = list(reversed(regs_pushed[len(regs_pushed) - nparams:])) if nparams else []
            storage = "|".join(args) + f";purge:{sh[1]}"
        if cname not in decls:
            skipped["no declaration (asm entry or not XB_ convention)"] += 1
            continue
        ret, conv, params = decls[cname]
        ret = clean(ret)
        convs = conv.split()
        cc = next((c[3:].lower() for c in convs if not c.startswith("XB_REGS")), "cdecl")
        regs = next((int(c[-1]) for c in convs if c.startswith("XB_REGS")), 0)
        types = [param_type(p) for p in params.split(",")] if params.strip() else []
        names = [param_name(p) for p in params.split(",") if param_type(p) not in (None, "...")] if params.strip() else []
        types = [t for t in types if t]
        varargs = "..." in types
        types = [t for t in types if t != "..."]
        if any("&" in t or "(" in t for t in types + [ret]):
            skipped["reference or function-pointer type"] += 1
            continue
        if regs and (cc == "thiscall" or cc == "fastcall"):
            skipped["regparm with thiscall/fastcall"] += 1
            continue
        out = []
        for t in [ret] + types:
            base = t.rstrip("*")
            if base == "long double" and t == base:
                out.append(t)
                continue
            if base not in SCALARS and not re.fullmatch(r"\w+", base):
                out = None
                break
            if base not in SCALARS:
                if t == base:
                    structs[base] = structs.get(base) or -1  # by value: size needed
                else:
                    structs.setdefault(base, 0)
            out.append(t)
        if out is None:
            skipped["unparsed type"] += 1
            continue
        rows[address] = (address, cname, cc, regs, out[0], "|".join(out[1:]), int(varargs), "|".join(names),
                          storage)

    # sizeof probe for structs passed by value
    by_value = sorted(n for n, s in structs.items() if s == -1)
    OUT.mkdir(parents=True, exist_ok=True)
    headers = sorted(p.name for p in engine_src.files("*.h"))
    probe = OUT / "probe.cpp"
    probe.write_text("".join(f'#include "{h}"\n' for h in headers) + "#include <cstdio>\nusing namespace game;\n"
                     "int main() {\n" + "".join(f'    std::printf("{n} %u\\n", unsigned(sizeof({n})));\n'
                                                for n in by_value) + "}\n")
    cmd = next(e["command"] for e in json.load(open(ROOT / "build" / "linux-loader" / "compile_commands.json"))
               if "/src/engine/" in e["file"])
    incs = re.findall(r"-I\S+", cmd)
    run = subprocess.run(["c++", "-m32", "-std=c++20", "-fsyntax-only", *incs, str(probe)], capture_output=True,
                         text=True)
    sizes = {}
    if run.returncode == 0:
        exe = OUT / "probe"
        subprocess.run(["c++", "-m32", "-std=c++20", "-c", *incs, str(probe), "-o", str(OUT / "probe.o")],
                       check=True)
        # link only the probe: the sizeof lines need no game code
        subprocess.run(["c++", "-m32", str(OUT / "probe.o"), "-o", str(exe)], check=True)
        for line in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.split("\n"):
            if line:
                n, s = line.split()
                sizes[n] = int(s)
    else:
        sys.exit("sizeof probe failed:\n" + run.stderr[:3000])
    for n in by_value:
        structs[n] = sizes[n]

    with open(OUT / "functions.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["address", "name", "convention", "regs", "return", "params", "varargs", "param_names", "storage"])
        for r in sorted(rows.values()):
            w.writerow([f"{r[0]:08x}", *r[1:]])
    with open(OUT / "structs.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["name", "size"])
        for n in sorted(structs):
            w.writerow([n, structs[n]])

    # slots
    image = fnpack.Image()
    functions, _ = fnpack.load_names()
    starts = sorted(functions)
    import bisect
    slots = {}
    for init in levelreach.SLOT_INITS:
        end = starts[bisect.bisect_right(starts, init)]
        for _, _, t in fnpack.disassemble(image, init, end - init):
            m = levelreach.STORE.match(t)
            if m and image.section(int(m.group(2), 16)) not in fnpack.DATA_SECTIONS:
                slots[0x00922140 + int(m.group(1), 16)] = int(m.group(2), 16)
    with open(OUT / "slots.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["slot", "builtin"])
        for s, b in sorted(slots.items()):
            if b in rows:
                w.writerow([f"{s:08x}", f"{b:08x}"])
    typed = sum(1 for b in slots.values() if b in rows)
    print(f"{len(rows)} prototypes, {len(structs)} structs ({len(by_value)} by value), "
          f"slots {typed}/{len(slots)} typed")
    for k, v in skipped.most_common():
        print(f"  skipped {v}: {k}")


if __name__ == "__main__":
    main()
