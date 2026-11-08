#!/usr/bin/env python3
"""Registers the thread_switch rows' C (docs/research/*_classes.csv) in kFunctions as Status::Compare.

Those rows reach a script wait; their C was written and scored (vc71match --thread-switch) but kept out of
kFunctions until script threads had stacks of their own (scr_fiber.cpp, 11-07) and the harness could end a test
at a thread switch (Config::switch_return_word). Per row: the code target is NAME_entry when an asm entry of that
name exists (PLATFORM_ASM_ENTRY, *_ARGS_ENTRY), else NAME; the stack bytes come from the original's code as
tools/scaffold.py reads them (highest stack argument read, or ret N) - too few when the original only writes an
argument slot (MSVC reuses them as locals): 11-07 260 rows were raised to the C declaration's parameter sizes
(a sizeof probe compiled with functions.cpp's flags), which the compare then skips as the callee's; the result kind from our C declaration's
return type (void None, bool/uint8_t Al, float St0, 64-bit EdxEax, else Eax), from the original's code when the
declaration is not found. Rows without C (no declaration, no entry) are listed and skipped. Each registered row
leaves set_aside.csv and gets a functions.csv zero row.

usage: tools/ts_register.py [--dry-run]
"""
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import engine_src  # noqa: E402
import fnpack  # noqa: E402
import scaffold  # noqa: E402

ROOT = fnpack.ROOT
dry = "--dry-run" in sys.argv

rows = []
for path in sorted((ROOT / "docs" / "research").glob("*_classes.csv")):
    for r in csv.DictReader(open(path)):
        if r.get("class") == "thread_switch":
            rows.append((int(r["address"], 16), r["name"]))

functions_cpp = engine_src.FUNCTIONS_CPP.read_text()
registered = {int(a, 16) for a in re.findall(r'^\s+\{"\w+", (0x[0-9a-fA-F]+),', functions_cpp, re.M)}

sources = "\n".join(p.read_text() for p in engine_src.files("*.h") + engine_src.files("*.cpp")
                    + engine_src.files("*.inc"))
entries = set(re.findall(r"PLATFORM_ASM_ENTRY\((\w+)_entry,", sources))
entries |= set(re.findall(r"^\s*(?:SCRIPT_ARGS_ENTRY|BRO_SCR_ARGS_ENTRY)\((\w+)\)", sources, re.M))
CONV = r"(?:XB_(?:CDECL|STDCALL|THISCALL|FASTCALL|REGS[123])\s+)+"
decls = {}
for m in re.finditer(r'^(?:extern "C" )?(?:PLATFORM_HIDDEN )?(?:static )?(?:inline )?([\w:<>\s*]+?)\s*\b'
                     + CONV + r'(\w+)\(', sources, re.M):
    decls.setdefault(m.group(2), m.group(1).strip())
for m in re.finditer(r"BRO_SCR_ARGS_DECL\((\w+)\)", sources):
    decls.setdefault(m.group(1), "uint32_t")


def result_of(ret):
    ret = ret.replace("const ", "").strip()
    if ret == "void":
        return "None"
    if ret in ("bool", "uint8_t", "int8_t", "char"):
        return "Al"
    if ret in ("float", "double"):
        return "St0"
    if ret in ("uint64_t", "int64_t"):
        return "EdxEax"
    return "Eax"


functions, _ = fnpack.load_names()
starts = sorted(set(functions) | {a for a, _ in rows})
image = fnpack.Image()
scaffold.POPS = fnpack.pops_reader(image, starts)

new, missing, already = [], [], []
for address, name in sorted(set(rows)):
    if address in registered:
        already.append(name)
        continue
    target = f"{name}_entry" if name in entries else name if name in decls else None
    if target is None:
        missing.append(f"{address:08x} {name}")
        continue
    _, regs, slots, ret, _ = scaffold.analyse(image, functions, starts, address)
    result = result_of(decls[name]) if name in decls else scaffold.result_from_rets(
        scaffold.analyse(image, functions, starts, address)[0])
    if name in decls and name in entries and decls[name] == "uint32_t" and f"BRO_SCR_ARGS_ENTRY({name})" in sources:
        result = "Eax"
    new.append((address, name, f'    {{"{name}", {address:#010x}, 0, code(&{target}), {4 * slots}, '
                             f"Result::{result}, Status::Compare}},"))

print(f"{len(new)} to register, {len(already)} already in kFunctions, {len(missing)} without C")
for m in missing:
    print("  no C:", m)
if dry:
    for _, _, line in new[:20]:
        print(line)
    sys.exit()

scaffold.register([(a, line) for a, _, line in new])
names = {f"{a:08x}" for a, _, _ in new}
path = ROOT / "tools" / "data" / "set_aside.csv"
with open(path) as f:
    kept = [l for l in f if l.split(",", 1)[0] not in names]
path.write_text("".join(kept))
path = ROOT / "tools" / "data" / "functions.csv"
lines = path.read_text().split("\n")
head, body = lines[0], [l for l in lines[1:] if l]
have = {l.split(",", 1)[0] for l in body}
body += [f'{a:08x},{name},compare,0,0,0,0,0,0,""' for a, name, _ in new if f"{a:08x}" not in have]
path.write_text("\n".join([head] + sorted(body, key=lambda l: l.split(",", 1)[0])) + "\n")
print(f"registered {len(new)}")
