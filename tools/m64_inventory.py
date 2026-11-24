#!/usr/bin/env python3
"""Inventory of what stands between the engine and an x86-64 build.

Reads one or more `ninja -k 0` logs of a 64-bit probe build (build/m64: the full 32-bit source set compiled with
-m64, see build/m64/m64-probe.cmake), groups every error by cause, and greps the sources for the same constructs so
causes hidden behind earlier errors are counted too.

    tools/m64_inventory.py build/m64/build.log [build/m64/shim/build.log ...] [--top 10] [--json out.json]

Each log is reported on its own (pass A = the tree as it is; pass B = the same with the asm-entry macros force-
defined for x86-64, which uncovers what the PLATFORM_HIDDEN cascade hides). The static scan runs once.
Standard library only.
"""

import argparse
import collections
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIRS = ("src", "tests")
SRC_EXT = (".cpp", ".h", ".c", ".inc", ".hpp")
LEVEL_DIR = "src/levels/translated/"

# ---------------------------------------------------------------------------------------------------------------
# Static source facts used by the log classifier.


def walk_sources():
    for top in SRC_DIRS:
        for base, _dirs, files in os.walk(os.path.join(ROOT, top)):
            for name in files:
                if name.endswith(SRC_EXT):
                    path = os.path.join(base, name)
                    yield os.path.relpath(path, ROOT), path


_line_cache = {}


def file_lines(path):
    if path not in _line_cache:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                _line_cache[path] = f.read().split("\n")
        except OSError:
            _line_cache[path] = []
    return _line_cache[path]


def source_line(path, line):
    lines = file_lines(path)
    return lines[line - 1] if 0 < line <= len(lines) else ""


def define_blocks(lines):
    """(name, body, first line index) for each #define, continuation lines joined."""
    i = 0
    while i < len(lines):
        m = re.match(r"\s*#\s*define\s+(\w+)", lines[i])
        if not m:
            i += 1
            continue
        start, body = i, lines[i]
        while body.endswith("\\") and i + 1 < len(lines):
            i += 1
            body = body[:-1] + " " + lines[i]
        yield m.group(1), body, start, i
        i += 1


ARCH32 = re.compile(r"__i386__|_M_IX86\b")
ARCH64 = re.compile(r"__x86_64__|_M_X64\b|_M_AMD64\b")
DEF_NAME = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?(?:(?:inline|static|extern|constexpr|\"C\")\s+)*"
    r"[A-Za-z_][\w:<>,\s\*&]*?[\s\*&](\w+)\s*\(")


def collect_static_facts():
    """Names the 64-bit build loses: PLATFORM_HIDDEN declarations, names defined only under __i386__, and macros
    that wrap PLATFORM_ASM_ENTRY."""
    hidden, i386_only, i386_else, defines, entries = set(), set(), set(), {}, set()
    for rel, path in walk_sources():
        if rel.startswith(LEVEL_DIR):
            continue
        lines = file_lines(path)
        text = "\n".join(lines)
        for m in re.finditer(r"PLATFORM_HIDDEN\b([^;{]*?)\b(\w+)\s*\(", text):
            hidden.add(m.group(2))
        for m in re.finditer(r"\bPLATFORM_ASM_ENTRY\s*\(\s*(\w+)\s*,", text):
            entries.add(m.group(1))
        for name, body, _s, _e in define_blocks(lines):
            defines.setdefault(name, []).append(body)
        # #if blocks: frame = [is_i386_only, in_else]
        stack = []
        for line in lines:
            s = line.strip()
            if re.match(r"#\s*if", s):
                stack.append([bool(ARCH32.search(s)) and not ARCH64.search(s), False])
                continue
            if re.match(r"#\s*el", s) and stack:
                stack[-1][1] = True
                continue
            if re.match(r"#\s*endif", s) and stack:
                stack.pop()
                continue
            if any(f[0] and not f[1] for f in stack):
                target = i386_only
            elif any(f[0] and f[1] for f in stack):
                target = i386_else
            else:
                continue
            m = re.match(r"\s*#\s*define\s+(\w+)", line) or DEF_NAME.match(line)
            if m:
                target.add(m.group(1))
    # Wrapper macros of PLATFORM_ASM_ENTRY (to a fixed point).
    wrappers = {"PLATFORM_ASM_ENTRY"}
    changed = True
    while changed:
        changed = False
        for name, bodies in defines.items():
            if name in wrappers:
                continue
            if any(re.search(r"\b(%s)\s*\(" % "|".join(sorted(wrappers)), b.split(None, 2)[-1]) for b in bodies):
                wrappers.add(name)
                changed = True
    # Macros that expand to PLATFORM_HIDDEN / PLATFORM_ASM_* (XL_FUNCTION, XL_DECLARE, entry wrappers): a line using
    # one fails like the glue macro itself.
    glue = {"PLATFORM_HIDDEN", "PLATFORM_ASM_ENTRY", "PLATFORM_ASM_SYMBOL", "PLATFORM_ASM_CALL"}
    changed = True
    while changed:
        changed = False
        for name, bodies in defines.items():
            if name in glue:
                continue
            if any(re.search(r"\b(%s)\b" % "|".join(sorted(glue)), b.split(None, 2)[-1]) for b in bodies):
                glue.add(name)
                changed = True
    return {
        "hidden": hidden,
        "i386_only": i386_only - i386_else - {"volatile", "asm", "__asm__"},
        "wrappers": wrappers,
        "glue": glue,
        "entries": entries,
    }


# ---------------------------------------------------------------------------------------------------------------
# Build log parsing.

DIAG = re.compile(r"^(?P<path>[^\s:][^:]*):(?P<line>\d+):(?:(?P<col>\d+):)? "
                  r"(?P<kind>fatal error|error|warning|Error|Warning): (?P<msg>.*)$")
STATUS = re.compile(r"^\[\d+/\d+\] (?:Building \w+ object|Linking \w+ [\w ]+?) (?P<out>\S+)")
FAILED = re.compile(r"^FAILED: (?:\[code=\d+\] )?(?P<out>\S+)")
FLAG = re.compile(r"\[(-W[\w=+-]+|-fpermissive)\]\s*$")


def norm_path(path, build_dir):
    if not os.path.isabs(path):
        path = os.path.join(build_dir, path)
    path = os.path.normpath(path)
    rel = os.path.relpath(path, ROOT)
    return rel if not rel.startswith("..") else path


def parse_log(log_path, build_dir):
    diags, failed, link_msgs = [], [], collections.defaultdict(list)
    current = None
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            m = FAILED.match(line)
            if m:
                current = m.group("out")
                failed.append(current)
                continue
            m = STATUS.match(line)
            if m:
                current = m.group("out")
                continue
            m = DIAG.match(line)
            if m:
                path = m.group("path")
                if path.startswith("/usr/bin/ld") or "collect2" in path:
                    link_msgs[current].append(line)
                    continue
                kind = m.group("kind").lower()
                if m.group("kind") == "Error":
                    kind = "asm-error"
                elif m.group("kind") == "Warning":
                    kind = "asm-warning"
                diags.append({
                    "kind": kind, "path": norm_path(path, build_dir), "line": int(m.group("line")),
                    "msg": m.group("msg"), "tu": current,
                })
                continue
            if current and ("ld.bfd:" in line or "ld:" in line[:20] or "undefined reference" in line
                            or line.startswith("collect2:")):
                link_msgs[current].append(line)
    return diags, failed, link_msgs


def read_ninja(build_dir):
    """objects {out: target}, links {out: (target, kind)}"""
    with open(os.path.join(build_dir, "build.ninja"), encoding="utf-8", errors="replace") as f:
        text = f.read().replace("$\n", "")
    objects, links, sources = {}, {}, {}
    for m in re.finditer(r"^build ([^:]+?): (\w+?)__(\w+?)_(?:Release|Debug|RelWithDebInfo|MinSizeRel)\b",
                         text, re.M):
        out, rule, target = m.group(1), m.group(2), m.group(3)
        target = re.sub(r"_unscanned$", "", target)
        if rule.endswith("_COMPILER"):
            objects[out.replace("$ ", " ")] = target
            src = text[m.end():].split(None, 1)[0]
            sources[out.replace("$ ", " ")] = norm_path(src, build_dir)
        elif "LINKER" in rule:
            links[out] = (target, rule)
    return objects, links, sources


# ---------------------------------------------------------------------------------------------------------------
# Classification.

CAUSES = [
    ("asm-glue-undefined", "glue macros undefined on x86-64 (PLATFORM_HIDDEN / _ASM_ENTRY / _ASM_SYMBOL / _ASM_CALL "
                           "and macros built on them, e.g. XL_FUNCTION)"),
    ("hidden-cascade", "cascade: call of a PLATFORM_HIDDEN-declared function (its declaration was dropped)"),
    ("i386-only-helper", "helper defined only under __i386__ (x87 / FS-segment helpers, xlat runtime)"),
    ("i386-os-abi", "32-bit OS ABI names (ucontext REG_Exx, i386 syscall numbers)"),
    ("asm-body-32bit", "32-bit asm rejected by the 64-bit assembler (asm entries, inline asm)"),
    ("inline-asm-constraint", "inline asm constraint/operand invalid in 64-bit"),
    ("calling-convention", "calling-convention attributes ignored/invalid"),
    ("ptr-to-u32", "pointer -> 32-bit integer cast (loses precision)"),
    ("int-to-ptr", "integer -> pointer of different size"),
    ("static-assert-layout", "static_assert on struct size/offset or pointer size"),
    ("width-mismatch", "uint32_t vs size_t/uintptr_t/long type mismatch"),
    ("third-party", "third-party code"),
    ("level-generated", "generated level code (src/levels/translated)"),
    ("undeclared-other", "other undeclared identifier (likely cascade)"),
    ("other", "other"),
]
CAUSE_TITLE = dict(CAUSES)

UNDECLARED = re.compile(r"[‘'](\w+)[’'] (?:was not declared in this scope|has not been declared|"
                        r"is not a member of|does not name a type)")


def classify(d, facts):
    msg, path = d["msg"], d["path"]
    text = source_line(os.path.join(ROOT, path), d["line"]) if not os.path.isabs(path) else ""
    if "/_deps/" in path or path.startswith("third_party/") or "/third_party/" in path:
        return "third-party"
    if d["kind"] == "asm-error":
        return "asm-body-32bit"
    if re.search(r"PLATFORM_(HIDDEN|ASM_ENTRY|ASM_SYMBOL|ASM_CALL)", msg):
        return "asm-glue-undefined"
    if re.search(r"\b(%s)\b" % "|".join(sorted(facts["glue"])), text) and not UNDECLARED.search(msg):
        return "asm-glue-undefined"
    m = UNDECLARED.search(msg)
    if m:
        name = m.group(1)
        if name in facts["hidden"] or name.startswith("xl_fn_"):  # xl_fn_*: declared by XL_DECLARE (PLATFORM_HIDDEN)
            return "hidden-cascade"
        if name in facts["i386_only"]:
            return "i386-only-helper"
        if re.match(r"REG_E[A-Z]{2}$|SYS_\w+$", name):
            return "i386-os-abi"
    if "static assertion failed" in msg or "static_assert" in msg:
        return "static-assert-layout"
    if "loses precision" in msg:
        return "ptr-to-u32"
    if "pointer from integer of different size" in msg or "int-to-pointer-cast" in msg:
        return "int-to-ptr"
    if "to integer of different size" in msg or "pointer-to-int-cast" in msg:
        return "ptr-to-u32"
    if re.search(r"\b(thiscall|stdcall|fastcall|regparm|cdecl)\b", msg) and ("attribute" in msg):
        return "calling-convention"
    if re.search(r"impossible constraint|invalid 'asm'|invalid ‘asm’|asm operand|inconsistent operand constraints|"
                 r"unsupported size for integer register|operand number out of range|can.t find a register", msg):
        return "inline-asm-constraint"
    if re.search(r"long unsigned int|long int\b|size_t", msg) and re.search(
            r"cannot convert|invalid conversion|narrowing|no matching|ambiguous|invalid static_cast", msg):
        return "width-mismatch"
    if path.startswith(LEVEL_DIR):
        return "level-generated"
    if m:
        return "undeclared-other"
    return "other"


def norm_msg(msg):
    msg = re.sub(r"; did you mean [‘'][^’']*[’']\?", "", msg)
    return re.sub(r"[‘'][^’']*[’']", "'X'", msg)


ASM_SUB = [
    (r"invalid instruction suffix for .(push|pop)", "pushl/popl (32-bit push/pop)"),
    (r"not supported in 64-bit mode", "instruction not in 64-bit mode (pushal/popal/pushfl...)"),
    (r"operand type mismatch for .(call|jmp)", "indirect call/jmp through a 32-bit register/word"),
    (r"operand type mismatch", "operand type mismatch (other)"),
    (r"register type mismatch|bad register name|bad memory operand", "32-bit register as address"),
    (r"invalid instruction suffix", "invalid instruction suffix (other)"),
    (r"symbol .* is already defined", "symbol already defined"),
    (r".", "other assembler error"),
]


def asm_sub(msg):
    for pat, label in ASM_SUB:
        if re.search(pat, msg):
            return label
    return "other"


def top_dir(path, depth=3):
    parts = path.split("/")
    return "/".join(parts[:depth]) if len(parts) > depth else os.path.dirname(path)


# ---------------------------------------------------------------------------------------------------------------
# Static scan.

STATIC = [
    ("asm-entry (PLATFORM_ASM_ENTRY direct)", r"\bPLATFORM_ASM_ENTRY\s*\("),
    ("asm-entry (via wrapper macros)", None),  # filled from facts
    ("PLATFORM_HIDDEN declarations", r"\bPLATFORM_HIDDEN\b"),
    ("PLATFORM_ASM_CALL / _SYMBOL in asm text", r"\bPLATFORM_ASM_(?:CALL|SYMBOL)\s*\("),
    ("__asm__ statements (all)", r"\b(?:__asm__|asm)\s*(?:volatile\s*|__volatile__\s*)?\("),
    ("asm lines: 32-bit stack ops (pushl/popl/pushal/%esp/%ebp)",
     r"\"[^\"]*(?:\b(?:pushl|popl|pushal|popal|pushfl|popfl|pushad|popad)\b|%%?e(?:sp|bp)\b)"),
    ("asm lines: other 32-bit register names", r"\"[^\"]*%%?e(?:ax|bx|cx|dx|si|di)\b"),
    ("asm lines: FS-segment access (%fs:)", r"%%?fs:"),
    ("XB_THISCALL", r"\bXB_THISCALL\b"),
    ("XB_STDCALL", r"\bXB_STDCALL\b"),
    ("XB_FASTCALL", r"\bXB_FASTCALL\b"),
    ("XB_CDECL", r"\bXB_CDECL\b"),
    ("XB_REGS1/2/3", r"\bXB_REGS[123]\b"),
    ("title_function<> (call original x86 at an address)", r"\btitle_function\s*<"),
    ("target_function<> (address or own C)", r"\btarget_function\s*<"),
    ("own_function<>", r"\bown_function\s*<"),
    ("own_address() (our fn pointer -> uint32)", r"\bown_address\s*\("),
    ("title_call_at* / *_call_regs helpers", r"\b(?:title_call_at\w*|\w+_call_regs)\s*\("),
    ("register-frame calls (f.call / fr.call / c.call / at.call)", r"\b(?:f|fr|c|at)\.call\s*(?:<[^>]*>)?\s*\("),
    ("title_global<>", r"\btitle_global\s*<"),
    ("platform_x87_* helper calls", r"\bplatform_x87_\w+\s*\("),
    ("long double", r"\blong double\b"),
    ("ptr->u32: static_cast<u32>(reinterpret_cast<uintptr_t>(p))",
     r"static_cast<\s*u?int32_t\s*>\s*\(\s*reinterpret_cast<\s*u?intptr_t\s*>"),
    ("ptr->u32: reinterpret_cast<uint32_t>", r"reinterpret_cast<\s*u?int32_t\s*>"),
    ("ptr->u32: C-style (uint32_t)(uintptr_t)", r"\(\s*u?int32_t\s*\)\s*\(?\s*\(?\s*u?intptr_t\s*\)"),
    ("u32->ptr: uintptr_t{...} (widening, OK below 4 GB)", r"\buintptr_t\s*\{"),
    ("u32->ptr: static_cast<uintptr_t>(...)", r"static_cast<\s*uintptr_t\s*>\s*\("),
    ("title_global<T*> (pointer-typed read of a 4-byte title slot)", r"\btitle_global\s*<[^<>]*\*\s*>"),
    ("field<T*> / at<T*> (pointer-typed field of title memory)", r"\b(?:field|at)\s*<[^<>]*\*\s*>"),
    ("reinterpret_cast<T**> (pointer read through a cast)", r"reinterpret_cast<[^<>]*\*\s*\*\s*>"),
    ("slot<> (call through a title function-pointer slot)", r"\bslot\s*<"),
    ("title_function<>(0xADDR) literal address", r"\btitle_function\s*<[^;]*?>\s*\(\s*0x[0-9a-fA-F]+\s*\)"),
    ("C++ references to PLATFORM_ASM_ENTRY symbols (hook rows, own_address)", "ENTRY_REFS"),
    ("static_assert(sizeof ...)", r"static_assert\s*\(\s*sizeof"),
    ("static_assert(offsetof ...)", r"static_assert\s*\(\s*offsetof"),
    ("sizeof(T*) / sizeof(void*)", r"sizeof\s*\(\s*(?:const\s+)?\w+\s*\*\s*\)"),
    ("__builtin_return/frame_address", r"__builtin_(?:return|frame)_address"),
    ("ptr->u32 of an address-of (&local/&global)",
     r"static_cast<\s*u?int32_t\s*>\s*\(\s*reinterpret_cast<\s*u?intptr_t\s*>\s*\(\s*&"),
    ("ucontext REG_Exx / i386 SYS_ numbers", r"\bREG_E(?:AX|BX|CX|DX|SI|DI|SP|BP|IP|FL)\b|\bSYS_\w+(?:32|64)\b|\bSYS_socketcall\b"),
]


IDENT = re.compile(r"\b[A-Za-z_]\w*_entry\b")


def static_scan(facts):
    pats = []
    wrappers = sorted(facts["wrappers"] - {"PLATFORM_ASM_ENTRY"})
    for label, pat in STATIC:
        if pat is None:
            pat = r"^\s*(?:%s)\s*\(" % "|".join(wrappers) if wrappers else r"(?!)"
        elif pat == "ENTRY_REFS":  # counted by set membership below (an alternation of ~1,400 names is slow)
            pat = r"(?!)"
        pats.append((label, re.compile(pat)))
    counts = {label: collections.Counter() for label, _ in pats}  # label -> {"engine": n, "levels": n}
    per_file = {label: collections.Counter() for label, _ in pats}
    for rel, path in walk_sources():
        bucket = "levels" if rel.startswith(LEVEL_DIR) else "engine"
        lines = file_lines(path)
        skip = set()
        for _n, _b, s, e in define_blocks(lines):  # macro definitions are not uses
            skip.update(range(s, e + 1))
        for i, line in enumerate(lines):
            if i in skip or line.lstrip().startswith("//"):
                continue
            is_decl = "PLATFORM_ASM_ENTRY" in line or line.lstrip().startswith('extern "C" void')
            for label, rx in pats:
                if label.startswith("C++ references"):
                    n = 0 if is_decl else sum(1 for w in IDENT.findall(line) if w in facts["entries"])
                    if n:
                        counts[label][bucket] += n
                        per_file[label][rel] += n
                    continue
                n = len(rx.findall(line)) if not label.startswith("asm lines") else (1 if rx.search(line) else 0)
                if n:
                    counts[label][bucket] += n
                    per_file[label][rel] += n
    return counts, per_file


def asserted_pointer_members():
    """Types named in static_assert(sizeof/offsetof(T ...)) - layouts shared with title memory - and how many
    pointer members their definitions have (each grows to 8 bytes in a 64-bit build)."""
    asserted, defs = set(), collections.defaultdict(list)
    member = re.compile(r"\s+(?:const\s+)?[A-Za-z_][\w:<>]*\s*\*+\s*(?:const\s+)?\w+(?:\[[^]]*\])?\s*(?:=[^;]*)?;"
                        r"|\s+[\w:]+\s*\(\s*\*\s*\w+\s*\)")
    for rel, path in walk_sources():
        if rel.startswith(LEVEL_DIR) or not rel.startswith("src/"):
            continue
        text = "\n".join(file_lines(path))
        for m in re.finditer(r"static_assert\s*\(\s*(?:sizeof|offsetof)\s*\(\s*([\w:]+)", text):
            asserted.add(m.group(1).split("::")[-1])
        for m in re.finditer(r"\n(?:struct|class|union)\s+(\w+)[^;{]*\{(.*?)\n\};", text, re.S):
            n = sum(1 for line in m.group(2).split("\n") if member.match(line))
            defs[m.group(1)].append((n, rel))
    ptr = {}
    for name, entries in defs.items():
        n = sum(e[0] for e in entries)
        if name in asserted and n:
            ptr[name] = (n, entries[0][1])
    return len(asserted), ptr


# ---------------------------------------------------------------------------------------------------------------
# Reporting.


def table(rows, headers):
    widths = [max(len(str(r[i])) for r in rows + [headers]) for i in range(len(headers))]
    out = ["  ".join(str(h).ljust(w) for h, w in zip(headers, widths)),
           "  ".join("-" * w for w in widths)]
    for r in rows:
        out.append("  ".join((str(c).rjust(w) if isinstance(c, int) else str(c).ljust(w)) for c, w in zip(r, widths)))
    return "\n".join(out)


WARN_64 = ("-Wint-to-pointer-cast", "-Wpointer-to-int-cast", "-Wformat", "-Wformat=", "-Wnarrowing", "-Wattributes",
           "-Wshift-count-overflow", "-Woverflow", "-Wsign-compare", "-Wcast-function-type")


def report_log(log, facts, top, results):
    build_dir = os.path.dirname(os.path.abspath(log))
    diags, failed, link_msgs = parse_log(log, build_dir)
    objects, links, sources = read_ninja(build_dir)
    for d in diags:  # top-level asm (asm entries): the assembler names its temporary .s file - use the TU's source
        if d["kind"] == "asm-error" and d["path"].endswith(".s"):
            d["path"] = sources.get(d["tu"], d["path"]) + " (top-level asm)"
        d["target"] = objects.get(d["tu"], "?")
    print("=" * 100)
    print(f"LOG {os.path.relpath(log, ROOT)}  (build dir {os.path.relpath(build_dir, ROOT)})")

    # Per-target compile / link status.
    failed_set = set(failed)
    per_target = collections.defaultdict(lambda: [0, 0, 0])  # ok, failed, not built
    for obj, target in objects.items():
        if obj in failed_set:
            per_target[target][1] += 1
        elif os.path.exists(os.path.join(build_dir, obj)):
            per_target[target][0] += 1
        else:
            per_target[target][2] += 1
    rows = []
    for target in sorted(per_target, key=lambda t: (-per_target[t][1], t)):
        ok, bad, nb = per_target[target]
        outs = [o for o, (t, _r) in links.items() if t == target]
        link = "-"
        for o in outs:
            if o in failed_set:
                why = link_msgs.get(o, [])
                wrong = any("wrong format" in w for w in why)
                undef = sum("undefined reference" in w for w in why)
                link = "FAILED (32-bit system lib picked: probe artifact)" if wrong else f"FAILED ({undef} undefined refs)"
            elif os.path.exists(os.path.join(build_dir, o)):
                link = "linked"
            else:
                link = "not reached"
        rows.append((target, ok, bad, nb, link))
    print("\nTargets (objects compiled / failed / not built, link step):")
    print(table(rows, ("target", "ok", "failed", "not built", "link")))

    errors = [d for d in diags if d["kind"] in ("error", "fatal error", "asm-error")]
    warnings = [d for d in diags if d["kind"] in ("warning", "asm-warning")]
    for d in errors:
        d["cause"] = classify(d, facts)
    uniq = {}
    for d in errors:
        uniq.setdefault((d["path"], d["line"], norm_msg(d["msg"])), d)

    by_cause = collections.Counter(d["cause"] for d in errors)
    by_cause_u = collections.Counter(d["cause"] for d in uniq.values())
    tus_by_cause = collections.defaultdict(set)
    for d in errors:
        tus_by_cause[d["cause"]].add(d["tu"])
    print(f"\nErrors: {len(errors)} printed, {len(uniq)} unique sites (file:line:message), "
          f"{len(failed_set & set(objects))} failed translation units")
    lv = collections.Counter(d["cause"] for d in uniq.values() if d["path"].startswith(LEVEL_DIR))
    tg = collections.defaultdict(collections.Counter)
    for d in uniq.values():
        tg[d["cause"]][d["target"]] += 1
    rows = [(key, by_cause[key], by_cause_u[key], by_cause_u[key] - lv[key], lv[key], len(tus_by_cause[key]),
             ", ".join(f"{t} {n}" for t, n in tg[key].most_common(3))) for key, _t in CAUSES if by_cause[key]]
    print(table(rows, ("cause", "printed", "unique", "engine", "levels", "TUs", "unique sites by target")))
    for key, title in CAUSES:
        if by_cause[key]:
            print(f"  {key}: {title}")

    asm = collections.Counter(asm_sub(d["msg"]) for d in uniq.values() if d["cause"] == "asm-body-32bit")
    if asm:
        print("\n  asm-body-32bit by assembler message (unique sites):")
        for label, n in asm.most_common():
            print(f"    {n:6d}  {label}")
    for cause in ("static-assert-layout", "ptr-to-u32", "int-to-ptr", "width-mismatch", "inline-asm-constraint",
                  "calling-convention"):
        sites = [d for d in uniq.values() if d["cause"] == cause]
        if sites:
            print(f"\n  {cause} sites:")
            for d in sorted(sites, key=lambda d: (d["path"], d["line"]))[:40]:
                text = source_line(os.path.join(ROOT, d["path"]), d["line"]).strip()
                print(f"    {d['path']}:{d['line']}: {text[:110]}")
    other = collections.Counter(norm_msg(d["msg"]) for d in uniq.values()
                                if d["cause"] in ("other", "undeclared-other"))
    if other:
        print("\n  most common other / undeclared-other messages (unique sites):")
        for msg, n in other.most_common(12):
            print(f"    {n:6d}  {msg[:120]}")

    print("\nErrors by directory (unique sites):")
    by_dir = collections.Counter(top_dir(d["path"].replace(" (top-level asm)", "")) for d in uniq.values())
    print(table([(k, v) for k, v in by_dir.most_common(top * 2)], ("directory", "unique")))
    print(f"\nTop {top} files (unique error sites, with the leading causes):")
    by_file = collections.Counter(d["path"] for d in uniq.values())
    rows = []
    for path, n in by_file.most_common(top):
        causes = collections.Counter(d["cause"] for d in uniq.values() if d["path"] == path)
        rows.append((path, n, ", ".join(f"{c} {k}" for k, c in causes.most_common(3))))
    print(table(rows, ("file", "unique", "causes")))
    eng = collections.Counter(d["path"] for d in uniq.values() if not d["path"].startswith(LEVEL_DIR))
    if len(eng) != len(by_file):
        print(f"\nTop {top} engine files (generated level code left out):")
        rows = []
        for path, n in eng.most_common(top):
            causes = collections.Counter(d["cause"] for d in uniq.values() if d["path"] == path)
            rows.append((path, n, ", ".join(f"{c} {k}" for k, c in causes.most_common(3))))
        print(table(rows, ("file", "unique", "causes")))

    wflags = collections.Counter()
    for d in warnings:
        m = FLAG.search(d["msg"])
        wflags[m.group(1) if m else "(no flag)"] += 1
    w64 = {k: v for k, v in wflags.items() if k.startswith(WARN_64)}
    print(f"\nWarnings: {len(warnings)} printed; 64-bit relevant: "
          + (", ".join(f"{k} {v}" for k, v in sorted(w64.items())) or "none")
          + ";  int-to-pointer-cast %d, pointer-to-int-cast %d" % (
              wflags.get("-Wint-to-pointer-cast", 0), wflags.get("-Wpointer-to-int-cast", 0)))
    results[log] = {
        "errors_printed": len(errors), "errors_unique": len(uniq),
        "by_cause_printed": dict(by_cause), "by_cause_unique": dict(by_cause_u),
        "targets": {t: v for t, v in per_target.items()}, "warnings": dict(wflags),
        "top_files": by_file.most_common(top),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", default=[os.path.join(ROOT, "build/m64/build.log")])
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument("--json", help="also write the numbers here")
    args = ap.parse_args()

    facts = collect_static_facts()
    results = {}
    for log in args.logs:
        report_log(os.path.abspath(log), facts, args.top, results)

    counts, per_file = static_scan(facts)
    print("=" * 100)
    print(f"STATIC SCAN of {', '.join(SRC_DIRS)} (engine = everything but {LEVEL_DIR}; comments skipped, "
          f"#define bodies not counted as uses)")
    print(f"  PLATFORM_HIDDEN names {len(facts['hidden'])}, names defined only under __i386__ "
          f"{len(facts['i386_only'])}, PLATFORM_ASM_ENTRY wrapper macros {len(facts['wrappers']) - 1}")
    rows = []
    for label, _p in STATIC:
        c = counts[label]
        files = len(per_file[label])
        top_f = ", ".join(f"{os.path.basename(f)} {n}" for f, n in per_file[label].most_common(3))
        rows.append((label, c["engine"], c["levels"], files, top_f))
    print(table(rows, ("construct", "engine", "levels", "files", "top files")))
    asserted, ptr_types = asserted_pointer_members()
    print(f"\n  size/offset-asserted (title-layout) types: {asserted}; with pointer members: {len(ptr_types)} types, "
          f"{sum(n for n, _f in ptr_types.values())} members (8 bytes in 64-bit); top: "
          + ", ".join(f"{t} {n}" for t, (n, _f) in sorted(ptr_types.items(), key=lambda kv: -kv[1][0])[:8]))
    results["asserted_pointer_members"] = {t: n for t, (n, _f) in ptr_types.items()}
    results["static"] = {label: dict(counts[label]) for label, _ in STATIC}
    results["static_files"] = {label: len(per_file[label]) for label, _ in STATIC}
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=1, default=list)


if __name__ == "__main__":
    sys.exit(main())
