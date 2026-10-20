#!/usr/bin/env python3
"""Static match check: compiles game/code/ with the title's own compiler (Visual C++
7.1 from XDK 5849, under Wine) and diffs each decompiled function against
default.xbe's bytes. A verification aid next to --compare, not a matching decomp.

game/code/ is C++20; the copy compiled here is lowered first (static_assert dropped,
enum class -> enum, alignas -> __declspec(align), constexpr -> const, nullptr -> 0).
A function VC7.1 still cannot compile (lambdas, auto, ...) has its body cut to a
declaration and is reported as "no-compile". Functions with a PLATFORM_ASM_ENTRY
(register arguments) and the LTCG calling conventions cannot match by construction.

Compared per instruction (objdump, Intel syntax): branch targets inside the function
are function-relative, calls are compared by callee name (symbol file / relocation),
and every constant inside the image (globals, literals, float constants) reads as
ADDR on both sides, so a score is about code generation, not about addresses.

usage: tools/vc71match/vc71match.py [FUNCTION...] [--flags "/O2 ..."] [--diff] [--csv]
  FUNCTION  only these kFunctions names (default: all)
  --diff    print the side-by-side differences of each function under 100%
  --csv     write tools/data/vc71match.csv (name,address,score,status)

Needs: wine, objdump, and the XDK's compiler in Tools/xdk5849 next to cod_bro/ (see README.md).
"""
import argparse
import csv
import difflib
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TOOL = Path(__file__).resolve().parent
DATA = ROOT / "tools" / "data"
XBE = ROOT.parent / "unpacked" / "default.xbe"
XDK = Path(os.environ.get("XDK5849", ROOT.parent.parent / "Tools" / "xdk5849" / "XDK" / "xbox"))
WORK = Path(os.environ.get("VC71MATCH_WORK", ROOT / "build" / "vc71match")).resolve()
# Our best guess at the title's options (no LTCG: /GL objects hold no machine code).
DEFAULT_FLAGS = "/nologo /c /FIvc71_prelude.h /O2 /Oy /G6 /arch:SSE /GR- /GF /Gy /W0"
SKIP_SOURCES = {"functions.cpp"}  # the kFunctions table (std::span)


# --- lowering ---------------------------------------------------------------------

def scan(text):
    """Yields (index, char) outside comments, strings, char literals and # lines."""
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#":
            while i < n and text[i] != "\n":
                i += 2 if text[i] == "\\" else 1
            continue
        if c in " \t":
            i += 1
            continue
        line_start = c == "\n"
        if text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            i = text.find("*/", i + 2) + 2
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            yield i, c
            i = j + 1
        else:
            yield i, c
            i += 1


def matching_paren(text, start):
    depth = 0
    for i, c in scan(text[start:]):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return start + i
    return -1


def lower(text):
    out, pos = [], 0
    for m in re.finditer(r"\bstatic_assert\s*\(", text):
        if m.start() < pos:
            continue
        end = matching_paren(text, m.end() - 1)
        semi = text.find(";", end)
        out.append(text[pos:m.start()])
        pos = semi + 1
    text = "".join(out) + text[pos:]
    text = re.sub(r"\benum\s+class\s+(\w+)\s*:\s*[\w:]+", r"enum \1", text)
    text = re.sub(r"\benum\s+class\b", "enum", text)
    text = re.sub(r"\balignas\s*\(([^)]*)\)", r"__declspec(align(\1))", text)
    text = re.sub(r"\bconstexpr\b", "const", text)
    text = re.sub(r"\bnullptr\b", "0", text)
    text = re.sub(r"\bnoexcept\b", "", text)
    text = re.sub(r"\[\[\w+\]\]", "", text)
    text = cut_calls(text, "PLATFORM_ASM_ENTRY")
    text = re.sub(r"\b(XB_(?:CDECL|STDCALL|FASTCALL|THISCALL))\s*\(\s*\*", r"(\1 *", text)
    text = brace_casts(text)
    # using F = R CC(args);  ->  typedef R CC F(args);   using T = U;  ->  typedef U T;
    text = re.sub(r"(?m)^(\s*)using (\w+) = ([^(;]*?)\(", r"\1typedef \3 \2(", text)
    text = re.sub(r"(?m)^(\s*)using (\w+) = ([^;]*);", r"\1typedef \3 \2;", text)
    return return_braces(text)


def split_params(params):
    out, depth, cur = [], 0, ""
    for c in params:
        depth += {"(": 1, "<": 1, ")": -1, ">": -1}.get(c, 0)
        if c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    return out + ([cur.strip()] if cur.strip() else [])


TITLE_CALL = re.compile(r"title_function<((?:(?!>\s*\().)*?)>\s*\(\s*(0x[0-9a-fA-F]+)\s*\)")


def title_calls(text):
    """title_function<F>(0xADDR) -> vc71_fn_ADDR, an extern "C" F declared before the
    function using it: a direct call (relocation) instead of one through a register."""
    for start, brace, end in reversed(blocks(text)):
        body = text[brace:end + 1]
        decls = {}

        def direct(m):
            sig, address = m[1].strip(), int(m[2], 16)
            if "(" not in sig and not re.search(r"(?m)^(?:using\s+%s\b|typedef\b[^;]*\b%s\b)" % (sig, sig), text[:start]):
                return m[0]  # a type named inside a function: leave the call as it is
            name = f"vc71_fn_{address:08x}_{zlib.crc32(sig.encode()) % 10000:04d}"
            decls[name] = f'typedef {sig.replace("(", " vc71_t_" + name + "(", 1)};' if "(" in sig \
                else f"typedef {sig} vc71_t_{name};"
            return name
        body = TITLE_CALL.sub(direct, body)
        # declared once, before the first function using it (blocks run backwards)
        for n in decls:
            later = re.search(r'extern "C" vc71_t_%s %s;\n' % (n, n), text[end:])
            if later:
                text = text[:end] + text[end:].replace(later[0], "").replace(decls[n] + "\n", "", 1)
        if decls:
            pre = "".join(f'{d}\nextern "C" vc71_t_{n} {n};\n' for n, d in decls.items())
            text = text[:start] + "\n" + pre + text[start:brace] + body + text[end + 1:]
    return text


def thiscall_members(text):
    """VC7.1 has __thiscall on member functions only: a free XB_THISCALL definition
    becomes a member of a struct of its own, its first parameter read from `this`."""
    for start, brace, end in reversed(blocks(text)):
        header = re.sub(r"//[^\n]*|/\*.*?\*/", "", text[start:brace], flags=re.S)
        m = re.match(r"(\s*(?:#[^\n]*\n\s*)*)(.*?)\bXB_THISCALL\s+(\w+)\s*\((.*)\)\s*(const\s*)?$", header, re.S)
        if not m:
            continue
        ret = re.sub(r'\b(inline|static)\b|extern\s+"C"', "", m[2]).strip()
        params = split_params(m[4])
        if not params:
            continue
        first, rest = params[0], ", ".join(params[1:])
        pm = re.match(r"(.*?[\s*&])(\w+)\s*$", first)
        if pm and not re.fullmatch(r"(const|unsigned|signed|int|char|float|double|long|short)", pm[2]):
            local = f"{pm[1].strip()} {pm[2]} = ({pm[1].strip()})this;"
        else:
            local = ""
        holder = "vc71_this_" + m[3]
        new = (f"{m[1]}struct {holder} {{ {ret} {m[3]}({rest}); }};\n"
               f"{ret} {holder}::{m[3]}({rest}) {{ {local}" + text[brace + 1:end + 1])
        text = text[:start] + new + text[end + 1:]
    return text


def brace_casts(text):
    """uint32_t{x}  ->  uint32_t(x)."""
    while True:
        m = re.search(r"\b(u?int(?:8|16|32|64|ptr)_t|size_t|float|double|bool|char|int|unsigned)\{", text)
        if not m:
            return text
        depth, i = 0, m.end() - 1
        for i, c in scan(text[m.end() - 1:]):
            depth += {"{": 1, "}": -1}.get(c, 0)
            if depth == 0:
                break
        close = m.end() - 1 + i
        text = text[:m.end() - 1] + "(" + text[m.end():close] + ")" + text[close + 1:]


def cut_calls(text, macro):
    out, pos = [], 0
    for m in re.finditer(r"\b%s\s*\(" % macro, text):
        if m.start() < pos or text[:m.start()].rstrip().endswith("define"):
            continue
        out.append(text[pos:m.start()])
        pos = matching_paren(text, m.end() - 1) + 1
    return "".join(out) + text[pos:]


def return_braces(text):
    """return {...};  ->  { T r = {...}; return r; }  (VC7.1 has no list-initialized returns)."""
    for start, brace, end in reversed(blocks(text)):
        header = re.sub(r"//[^\n]*|/\*.*?\*/|^\s*#[^\n]*", "", text[start:brace], flags=re.S | re.M)
        m = re.match(r"\s*(?:template\s*<[^>]*>\s*)?((?:[\w:<>*&\s]|\bconst\b)+?)\b\w+(?:::\w+)*\s*\(", header)
        if not m:
            continue
        ret = re.sub(r"\b(inline|static|extern|XB_\w+|PLATFORM_\w+)\b", "", m[1]).strip()
        body = text[brace:end + 1]
        if "return {" not in body or not ret or ret == "void":
            continue
        body = re.sub(r"return (\{.*?\});", lambda r: "{ %s vc71_r = %s; return vc71_r; }" % (ret, r[1]), body, flags=re.S)
        text = text[:brace] + body + text[end + 1:]
    return text


def blocks(text):
    """Namespace-level function definitions: (header_start, open_brace, close_brace)."""
    found, stack, last_end = [], [], 0
    for i, c in scan(text):
        if c == "{":
            header = text[last_end:i]
            words = re.sub(r"//[^\n]*|/\*.*?\*/|^\s*#[^\n]*", "", header, flags=re.S | re.M)
            if all(kind == "namespace" for kind, _, _ in stack):
                if re.search(r"\bnamespace(\s+\w+)?\s*$", words) or re.search(r'extern\s+"C"\s*$', words):
                    kind = "namespace"
                elif ")" in words and not re.search(r"\b(struct|class|union|enum)\b[^(]*$", words.split("(")[0]) \
                        and not re.search(r"=\s*$", words):
                    kind = "function"
                else:
                    kind = "other"
            else:
                kind = "inner"
            stack.append((kind, last_end, i))
            last_end = i + 1
        elif c == "}" and stack:
            kind, start, brace = stack.pop()
            if kind == "function":
                found.append((start, brace, i))
            last_end = i + 1
        elif c == ";":
            last_end = i + 1
    return found


def cut_functions(text, lines):
    """Cuts the bodies of the functions holding `lines` (1-based) to declarations."""
    starts = [0]
    for l in text.split("\n"):
        starts.append(starts[-1] + len(l) + 1)
    hit = {}
    for start, brace, end in blocks(text):
        for line in lines:
            if start <= starts[line - 1] <= end:
                hit[start] = (brace, end, line)
    names = {}
    for start in sorted(hit, reverse=True):
        brace, end, line = hit[start]
        found = re.findall(r"(\w+)\s*\(", re.sub(r"//[^\n]*|/\*.*?\*/", "", text[start:brace], flags=re.S))
        names[found[-1] if found else "?"] = line
        if "vc71_this_" in text[start:brace]:  # out-of-class: no redeclaration allowed
            text = text[:start] + "\n" * text.count("\n", start, end) + text[end + 1:]
        else:
            text = text[:brace].rstrip() + ";" + text[end + 1:]
    return text, names


# --- compiling ----------------------------------------------------------------------

def winpath(path):
    return "Z:" + str(path).replace("/", "\\")


def wine_env():
    env = dict(os.environ, WINEDEBUG="-all", WINEDLLOVERRIDES="mscoree,mshtml=",
               WINEPREFIX=str(WORK / "wineprefix"))
    env["INCLUDE"] = ";".join(winpath(p) for p in (TOOL / "include", WORK / "src", XDK / "include"))
    return env


def compile_all(flags):
    src = WORK / "src"
    shutil.rmtree(src, ignore_errors=True)
    src.mkdir(parents=True)
    for path in list((ROOT / "game" / "code").glob("*.[ch]*")) + [ROOT / "platform" / "compiler.h"]:
        text = lower(path.read_text())
        if path.suffix == ".cpp":
            text = thiscall_members(title_calls(text))
        if path.name == "compiler.h":
            # VC7.1 has no __thiscall on free functions; __fastcall also takes ECX
            # first (a second integer argument would go in EDX: flagged below).
            text = text.replace("#define XB_THISCALL __thiscall", "#define XB_THISCALL __fastcall")
        (src / path.name).write_text(text)
    cut, objs = {}, []
    for cpp in sorted(src.glob("*.cpp")):
        if cpp.name in SKIP_SOURCES:
            continue
        obj = WORK / (cpp.stem + ".obj")
        for _ in range(200):
            r = subprocess.run(["wine", str(XDK / "bin" / "vc71" / "CL.Exe"), *flags.split(),
                                "/Fo" + winpath(obj), winpath(cpp)],
                               cwd=src, env=wine_env(), capture_output=True, text=True, errors="replace")
            log = r.stdout.replace("\r", "")
            errors = re.findall(r"^(.*?)\((\d+)\) : (?:fatal )?error (C\d+): (.*)$", log, re.M)
            if r.returncode == 0 and obj.exists():
                objs.append(obj)
                break
            ours = [(int(l), code, msg) for f, l, code, msg in errors if Path(f.replace("\\", "/")).name == cpp.name]
            if not ours:
                print(f"{cpp.name}: does not compile outside a function body:", file=sys.stderr)
                print("\n".join(log.splitlines()[:15]), file=sys.stderr)
                break
            text, names = cut_functions(cpp.read_text(), sorted({l for l, _, _ in ours}))
            if not names:
                print(f"{cpp.name}({ours[0][0]}): {ours[0][1]} {ours[0][2]} (not in a function)", file=sys.stderr)
                break
            first = {}
            for l, code, msg in ours:
                first.setdefault(l, f"{code}: {msg}")
            for name, line in names.items():
                cut[name] = next((first[l] for l in sorted(first) if l >= line), "")
            cpp.write_text(text)
    return objs, cut


# --- comparing ----------------------------------------------------------------------

def load_xbe():
    d = XBE.read_bytes()
    base, = struct.unpack_from("<I", d, 0x104)
    size, = struct.unpack_from("<I", d, 0x10C)
    count, headers = struct.unpack_from("<II", d, 0x11C)
    secs = []
    for i in range(count):
        _, va, vs, raw, rs, _ = struct.unpack_from("<IIIIII", d, headers - base + i * 56)
        secs.append((va, rs, raw))
    return d, secs, (base, base + size)


def xbe_bytes(xbe, address, length):
    d, secs, _ = xbe
    for va, rs, raw in secs:
        if va <= address < va + rs:
            return d[raw + address - va: raw + min(address - va + length, rs)]
    return b""


def symbols():
    out = []
    for line in (DATA / "default_xbe_symbols.txt").read_text().splitlines():
        if line and not line.startswith("#"):
            a, name = line.split(None, 1)
            out.append((int(a, 16), name.strip()))
    out.sort()
    return out


def kfunctions():
    text = (ROOT / "game" / "code" / "functions.cpp").read_text()
    return [(m[0], int(m[1], 16), m[2]) for m in
            re.findall(r'\{"(\w+)", (0x[0-9a-fA-F]+), \w+, code\(&(\w+)\)', text)]


RELOC = re.compile(r"^\s*([0-9a-f]+): (\S+)\s+(\S+)$")


def plain_name(symbol):
    m = re.match(r"\?(\w+)@", symbol)
    name = m[1] if m else symbol.lstrip("_@").split("@")[0]
    t = re.match(r"vc71_fn_([0-9a-f]{8})_", name)
    return f"vc71_fn_{t[1]}" if t else name


def obj_functions(objs):
    """name -> list of (offset, text, reloc target or None)."""
    funcs = {}
    for obj in objs:
        out = subprocess.run(["objdump", "-d", "-r", "-M", "intel", "--no-show-raw-insn", str(obj)],
                             capture_output=True, text=True).stdout
        current = None
        for line in out.splitlines():
            m = re.match(r"^[0-9a-f]+ <(.+)>:$", line)
            if m:
                current = funcs.setdefault(plain_name(m[1]), [])
                continue
            if current is None:
                continue
            r = RELOC.match(line)
            if r and current:
                off, text, _ = current[-1]
                current[-1] = (off, text, plain_name(r[3]))
                continue
            m = re.match(r"^\s*([0-9a-f]+):\t(.*)$", line)
            if m:
                current.append((int(m[1], 16), m[2].strip(), None))
    return funcs


def xbe_function(xbe, address, length):
    code = xbe_bytes(xbe, address, length)
    pad = code.find(b"\xcc\xcc")  # the gap before an unlisted next function
    code = (code[:pad] if pad > 0 else code).rstrip(b"\xcc")
    tmp = WORK / "xbe.bin"
    tmp.write_bytes(code)
    out = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386", "-M", "intel",
                          "--no-show-raw-insn", str(tmp)], capture_output=True, text=True).stdout
    insns = [(int(m[1], 16), m[2].strip(), None) for m in re.finditer(r"^\s*([0-9a-f]+):\t(.*)$", out, re.M)]
    while insns and re.match(r"(nop|xchg\s+ax,ax|lea\s+esi,\[esi(\+eiz\*1)?\+0x0\])", insns[-1][1]):
        insns.pop()
    return insns


def normalize(insns, address, by_address, image):
    """Instruction lines comparable between the object file and the XBE."""
    lines = []
    size = insns[-1][0] if insns else 0
    for off, text, target in insns:
        text = re.sub(r"\s+", " ", text.split("#")[0].strip())
        text = re.sub(r"^(\w+) (?:DWORD|WORD|BYTE) PTR ds:", r"\1 ", text)
        m = re.match(r"^(call|j\w+) (?:0x)?([0-9a-f]+)(?: <.*>)?$", text)
        if m:
            dest = int(m[2], 16)
            if target is not None:  # relocated call in the object file
                t = re.match(r"vc71_fn_([0-9a-f]{8})$", target)
                text = f"{m[1]} {by_address.get(int(t[1], 16), target) if t else target}"
            elif address is not None and (m[1] == "call" or dest > size):
                dest = (address + dest) & 0xFFFFFFFF
                text = f"{m[1]} {by_address.get(dest, hex(dest))}"
            else:
                text = f"{m[1]} .+{dest:#x}"
        else:
            def mask(h):
                v = int(h[0], 16)
                return "ADDR" if image[0] <= v < image[1] or (target and v == 0) else h[0]
            text = re.sub(r"0x[0-9a-f]+", mask, text)
            text = text.replace("ds:", "")
        lines.append(text)
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("names", nargs="*")
    ap.add_argument("--flags", default=DEFAULT_FLAGS)
    ap.add_argument("--diff", action="store_true")
    ap.add_argument("--csv", action="store_true")
    args = ap.parse_args()
    if not (XDK / "bin" / "vc71" / "CL.Exe").exists():
        sys.exit(f"no compiler at {XDK}/bin/vc71 (set XDK5849)")
    WORK.mkdir(parents=True, exist_ok=True)

    subprocess.run(["wineserver", "-p60"], env=wine_env())  # stays up between the compiles
    objs, cut = compile_all(args.flags)
    compiled = obj_functions(objs)
    xbe = load_xbe()
    syms = symbols()
    by_address = {a: n for a, n in syms}
    starts = [a for a, _ in syms]

    rows, total = [], []
    for name, address, code in kfunctions():
        if args.names and name not in args.names:
            continue
        if code.endswith("_entry") or code != name and code not in compiled:
            rows.append((name, address, "", "asm-entry"))
            continue
        if code in cut or name in cut:
            rows.append((name, address, "", "no-compile " + cut.get(code, cut.get(name, ""))))
            continue
        if code not in compiled:
            rows.append((name, address, "", "not-in-obj"))
            continue
        nxt = next((a for a in starts if a > address), address + 0x1000)
        body = compiled[code]
        if len(body) == 1 and body[0][2] in compiled:  # a wrapper that jumps to a template
            body = compiled[body[0][2]]
        ours = normalize(body, None, by_address, xbe[2])
        theirs = normalize(xbe_function(xbe, address, nxt - address), address, by_address, xbe[2])
        score = difflib.SequenceMatcher(None, theirs, ours, autojunk=False).ratio() * 100
        total.append(score)
        rows.append((name, address, f"{score:.1f}", "ok" if score == 100 else f"{len(theirs)}/{len(ours)} insns"))
        if args.diff and score < 100:
            print(f"--- {name} {address:#010x} (xbe | vc71)")
            for op, a0, a1, b0, b1 in difflib.SequenceMatcher(None, theirs, ours, autojunk=False).get_opcodes():
                for k in range(max(a1 - a0, b1 - b0)):
                    left = theirs[a0 + k] if a0 + k < a1 else ""
                    right = ours[b0 + k] if b0 + k < b1 else ""
                    print(f"  {'  ' if op == 'equal' else '!='} {left:40.40} | {right}")

    for name, address, score, status in rows:
        print(f"{name:40} {address:#010x} {score:>6} {status}")
    if total:
        exact = sum(1 for s in total if s == 100)
        print(f"\n{len(total)} compared: {exact} exact, mean {sum(total) / len(total):.1f}%; "
              f"{sum(1 for r in rows if r[3].startswith('no-compile'))} no-compile, "
              f"{sum(1 for r in rows if r[3] == 'asm-entry')} asm entries")
    if args.csv:
        with open(DATA / "vc71match.csv", "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["name", "address", "score", "status"])
            for name, address, score, status in rows:
                w.writerow([name, f"{address:#010x}", score, status])


if __name__ == "__main__":
    main()
