#!/usr/bin/env python3
"""Index the functions of a C source tree (Quake 3 / RTCW GPL source) for matching.

For every function definition: file, name, the string literals it uses (in order),
the functions it calls (in order, each with its string-literal arguments) and its
numeric constants; plus {"string", identifier} pairs from file-scope tables. A small hand-written
scanner, not a C parser: function bodies are found as '{' at brace depth 0 after
a ')' (macros that expand to definitions are missed, which is fine for matching).

Usage: source_index.py SRC_DIR [SRC_DIR...] > index.json
"""
import json
import os
import re
import sys

KEYWORDS = {
    "if", "while", "for", "switch", "return", "sizeof", "do", "else", "case",
    "defined", "__declspec", "__asm", "_asm",
}
SKIP_DIRS = {"bspc", "jpeg-6", "ft2", "macosx", "unix", "win32", "null", "q3_ui",
             "extractfuncs", "splines", "botai", "lcc", "q3asm", "q3map", "q3radiant"}

TOKEN = re.compile(
    r'"(?:\\.|[^"\\])*"'            # string literal
    r"|'(?:\\.|[^'\\])*'"           # char literal
    r"|0[xX][0-9a-fA-F]+[uUlL]*"    # hex number
    r"|\d+\.\d*(?:[eE][-+]?\d+)?[fF]?|\.\d+(?:[eE][-+]?\d+)?[fF]?|\d+[eE][-+]?\d+[fF]?"
    r"|\d+[uUlL]*"                  # numbers
    r"|[A-Za-z_]\w*"                # identifier
    r"|\S")                         # punctuation


def strip(text):
    """Remove comments and preprocessor lines, keep string literals."""
    out = []
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#":
            # preprocessor line, with continuations
            while i < n:
                j = text.find("\n", i)
                if j < 0:
                    i = n
                    break
                if text[j - 1] == "\\":
                    i = j + 1
                    continue
                i = j
                break
            continue
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
            line_start = False
            continue
        out.append(c)
        if c == "\n":
            line_start = True
        elif not c.isspace():
            line_start = False
        i += 1
    return "".join(out)


def unescape(lit):
    s = lit[1:-1]
    # keep the escapes the Ghidra export writes (\n \t \r \\), turn \" into "
    return s.replace('\\"', '"')


def number_value(tok):
    t = tok.rstrip("uUlLfF") if not tok.lower().startswith("0x") else tok.rstrip("uUlL")
    try:
        if t.lower().startswith("0x"):
            return int(t, 16)
        if any(ch in t for ch in ".eE"):
            return float(t)
        return int(t)
    except ValueError:
        return None


def call_strings(body, open_paren):
    """String literals and identifiers ("&name") that are direct arguments of the call
    whose '(' is at open_paren."""
    out = []
    depth = 0
    prev_string = False
    for t in body[open_paren:]:
        if t == "(":
            depth += 1
        elif t == ")":
            depth -= 1
            if depth == 0:
                break
        if depth == 1 and t.startswith('"'):
            if prev_string:
                out[-1] += unescape(t)
            else:
                out.append(unescape(t))
            prev_string = True
        else:
            if depth == 1 and re.match(r"[A-Za-z_]\w*$", t) and t not in KEYWORDS:
                out.append("&" + t)  # identifier argument (maybe a function pointer)
            prev_string = False
    return out


def index_file(path, rel, pairs):
    with open(path, encoding="latin-1") as f:
        tokens = TOKEN.findall(strip(f.read()))
    funcs = []
    depth = 0
    i = 0
    n = len(tokens)
    while i < n:
        t = tokens[i]
        if t == "{" and depth == 0:
            # look back for "name ( ... )" right before the brace
            j = i - 1
            if j >= 0 and tokens[j] == ")":
                par = 0
                k = j
                while k >= 0:
                    if tokens[k] == ")":
                        par += 1
                    elif tokens[k] == "(":
                        par -= 1
                        if par == 0:
                            break
                    k -= 1
                name = tokens[k - 1] if k > 0 else ""
                if re.match(r"[A-Za-z_]\w*$", name) and name not in KEYWORDS:
                    # body
                    body_start = i + 1
                    d = 1
                    m = body_start
                    while m < n and d:
                        if tokens[m] == "{":
                            d += 1
                        elif tokens[m] == "}":
                            d -= 1
                        m += 1
                    body = tokens[body_start:m - 1]
                    strings, calls, numbers = [], [], []
                    for q, b in enumerate(body):
                        if b.startswith('"'):
                            # adjacent literals concatenate
                            if q > 0 and body[q - 1].startswith('"') and strings:
                                strings[-1] += unescape(b)
                            else:
                                strings.append(unescape(b))
                        elif re.match(r"[A-Za-z_]\w*$", b) and q + 1 < len(body) and body[q + 1] == "(" \
                                and b not in KEYWORDS:
                            calls.append([b, call_strings(body, q + 1)])
                        elif b[0].isdigit() or (b[0] == "." and len(b) > 1):
                            v = number_value(b)
                            if v is not None:
                                numbers.append(v)
                    funcs.append({"file": rel, "name": name, "strings": strings,
                                  "calls": calls, "numbers": numbers, "tokens": len(body)})
                    i = m
                    continue
        if t == "{":
            depth += 1
        elif t == "}":
            depth = max(0, depth - 1)
        elif depth > 0 and t.startswith('"') and i + 2 < n and tokens[i + 1] == "," \
                and re.match(r"[A-Za-z_]\w*$", tokens[i + 2]):
            # {"name", Function} in a file-scope table
            pairs.append({"file": rel, "string": unescape(t), "target": tokens[i + 2]})
        i += 1
    return funcs


def main():
    result = []
    pairs = []
    for root_dir in sys.argv[1:]:
        base = os.path.basename(os.path.normpath(os.path.dirname(root_dir.rstrip("/")))) or root_dir
        for dirpath, dirnames, filenames in os.walk(root_dir):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
            for fn in sorted(filenames):
                if fn.endswith((".c", ".cpp")):
                    path = os.path.join(dirpath, fn)
                    rel = base + "/" + os.path.relpath(path, root_dir)
                    result.extend(index_file(path, rel, pairs))
    json.dump({"functions": result, "pairs": pairs}, sys.stdout, indent=0)


if __name__ == "__main__":
    main()
