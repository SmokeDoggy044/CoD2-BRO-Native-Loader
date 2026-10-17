#!/usr/bin/env python3
"""Propose Quake 3 / RTCW names for unnamed functions in default.xbe.

Inputs: tools/data/default_xbe_matchdata.tsv (tools/ghidra/ExportMatchData.java) and
one or more source indexes (source_index.py). Four passes, each feeding the next:

  strings  an XBE function references the same distinctive strings as one source
           function (weights: 1 / functions sharing the string, on both sides)
  handler  a code pointer registered under the same name as in the source
           (Cmd_AddCommand("toggle", Cvar_Toggle_f), {"name", function} tables);
           pointers without a Ghidra function are reported too (create them first)
  args     an XBE function is called with the same string arguments as one source
           function is (Cvar_Get("cl_run", ...) at many call sites)
  graph    inside a matched pair, the unknown callee between two known callees
           (or at the start/end) is paired with the unknown source call in the
           same gap, when the gap holds exactly one on each side; votes over all
           pairs, repeated until nothing new is found

Output (stdout): CSV address,name,source_file,pass,score,runner_up,evidence.
Proposals only: nothing is renamed. Check them before applying (see README.md).

Usage: match.py MATCHDATA.tsv INDEX.json [INDEX.json...] > proposals.csv
"""
import collections
import csv
import json
import sys

LIBC = {
    "memset", "memcpy", "memmove", "memcmp", "strcpy", "strncpy", "strcat", "strlen",
    "strcmp", "strncmp", "strchr", "strrchr", "strstr", "sprintf", "vsprintf",
    "sscanf", "atoi", "atof", "tolower", "toupper", "qsort", "malloc", "free",
    "sqrt", "atan2", "sin", "cos", "tan", "asin", "acos", "floor", "ceil", "fabs",
    "rand", "srand", "va_start", "va_end",
}
REGISTER_CALLS = {"Cmd_AddCommand", "trap_AddCommand"}
SKIP_CALLS = {"va_start", "va_end", "assert", "sizeof"}


def load_xbe(path, pairs=None):
    funcs = {}
    for line in open(path, encoding="utf-8"):
        if line.startswith("#"):
            continue
        if line.startswith("@pair\t"):
            _, _data, string, target, _k = line.rstrip("\n").split("\t")
            if pairs is not None:
                pairs.append((string, target))
            continue
        addr, section, size, name, strings, callees = line.rstrip("\n").split("\t")
        calls = []
        for entry in callees.split("\x1d") if callees else []:
            target, _, args = entry.partition("=")
            calls.append((target, [a for a in args.split("\x1e") if a] if args else []))
        funcs[addr] = {
            "section": section, "size": int(size), "name": name,
            "strings": [s for s in strings.split("\x1f") if s], "calls": calls,
        }
    return funcs


def in_text(addr):
    return 0x00011000 <= int(addr, 16) < 0x00379e7c  # default.xbe .text


def is_unnamed(name):
    return name.startswith("FUN_") or name.startswith("thunk_FUN_")


def main():
    xbe_pairs = []
    xbe = load_xbe(sys.argv[1], xbe_pairs)
    src = []
    src_pairs = []
    for path in sys.argv[2:]:
        index = json.load(open(path))
        src.extend(index["functions"])
        src_pairs.extend(index["pairs"])
    defined = {f["name"] for f in src}
    files_of = collections.defaultdict(set)
    for f in src:
        files_of[f["name"]].add(f["file"])

    candidates = {a for a, f in xbe.items() if f["section"] == ".text" and is_unnamed(f["name"])}
    found = {}       # address -> (name, pass, score, runner_up, evidence)
    taken = {}       # name -> address

    def accept(addr, name, how, score, runner_up, evidence):
        if addr in found or name in taken:
            return False
        found[addr] = (name, how, score, runner_up, evidence)
        taken[name] = addr
        return True

    # --- pass 1: shared strings -------------------------------------------------
    src_by_string = collections.defaultdict(set)
    for f in src:
        for s in set(f["strings"]):
            if len(s) >= 4:
                src_by_string[s].add(f["name"])
    xbe_by_string = collections.defaultdict(set)
    for a, f in xbe.items():
        for s in set(f["strings"]):
            xbe_by_string[s].add(a)
    proposals = []
    for a in candidates:
        score = collections.Counter()
        evidence = collections.defaultdict(list)
        for s in set(xbe[a]["strings"]):
            names = src_by_string.get(s)
            if not names:
                continue
            w = min(1.0, len(s) / 12) / len(names) / len(xbe_by_string[s])
            for n in names:
                score[n] += w
                evidence[n].append(s)
        if score:
            top = score.most_common(2)
            runner = top[1] if len(top) > 1 else ("", 0.0)
            proposals.append((top[0][1], a, top[0][0], runner, evidence[top[0][0]]))
    proposals.sort(reverse=True)
    for sc, a, n, runner, ev in proposals:
        if sc >= 0.9 and sc >= 2 * runner[1]:
            accept(a, n, "strings", sc, runner[0], " | ".join(ev[:4]))

    # --- pass 2: handlers registered under a name -------------------------------
    # Cmd_AddCommand("toggle", Cvar_Toggle_f) at a call site, or {"toggle", f} in a
    # table: the code pointer next to the same string is the same function.
    handler_by_string = collections.defaultdict(collections.Counter)
    handler_by_call = collections.defaultdict(collections.Counter)  # (callee, string)
    for f in src:
        for name, args in f["calls"]:
            strs = [a for a in args if not a.startswith("&")]
            ptrs = [a[1:] for a in args if a.startswith("&") and a[1:] in defined]
            if len(strs) == 1 and len(ptrs) == 1 and name in REGISTER_CALLS:
                handler_by_string[strs[0]][ptrs[0]] += 1
                handler_by_call[(name, strs[0])][ptrs[0]] += 1
    handler_by_table = collections.defaultdict(collections.Counter)
    for p in src_pairs:
        if p["target"] in defined:
            handler_by_table[p["string"]][p["target"]] += 1
    xbe_sites = []
    for f in xbe.values():
        if f["section"] != ".text":
            continue
        for target, args in f["calls"]:
            strs = [a for a in args if not a.startswith("&")]
            ptrs = [a[1:] for a in args if a.startswith("&")]
            if len(strs) == 1 and len(ptrs) == 1 and in_text(ptrs[0]) \
                    and (ptrs[0] in xbe or int(ptrs[0], 16) % 16 == 0):
                xbe_sites.append((strs[0], ptrs[0], target))
    xbe_sites.extend((string, ptr, "table") for string, ptr in xbe_pairs if in_text(ptr))
    votes = collections.defaultdict(collections.Counter)
    for string, ptr, target in xbe_sites:
        # when the registering function is known (Cmd_AddCommand), only its source
        # call sites count
        if target == "table":
            names = handler_by_table.get(string)
        else:
            callee = found[target][0] if target in found else None
            names = handler_by_call.get((callee, string)) if callee else None
            if names is None:
                names = handler_by_string.get(string)
        if names and len(names) == 1:
            votes[ptr][next(iter(names))] += 1
        elif names:
            # several source handlers for one string (game vs cgame): weak votes
            for n in names:
                votes[ptr][n] += 0.5 / len(names)
    for ptr, v in sorted(votes.items()):
        top = v.most_common(2)
        runner = top[1] if len(top) > 1 else ("", 0.0)
        if top[0][1] >= 1 and runner[1] == 0 and (ptr in candidates or ptr not in xbe):
            accept(ptr, top[0][0], "handler", top[0][1], "", "registered as " + ", ".join(
                sorted({s for s, p, _t in xbe_sites if p == ptr})[:4]))

    # --- pass 3: string arguments at call sites ---------------------------------
    callee_by_arg = collections.defaultdict(collections.Counter)
    for f in src:
        for name, args in f["calls"]:
            if name in defined:
                for s in args:
                    if not s.startswith("&"):
                        callee_by_arg[s][name] += 1
    votes = collections.defaultdict(collections.Counter)
    sites = collections.Counter()
    voting_sites = collections.Counter()               # sites with any vote
    site_hits = collections.defaultdict(collections.Counter)  # sites voting for a name
    for f in xbe.values():
        if f["section"] != ".text":
            continue  # level scripts pass their own strings to script builtins
        for target, args in f["calls"]:
            if target not in candidates:
                continue
            sites[target] += 1
            hit = set()
            for s in set(args):
                names = callee_by_arg.get(s)
                if not names or len(s) < 3 or s.startswith("&"):
                    continue
                total = sum(names.values())
                for n, c in names.items():
                    votes[target][n] += c / total / len(names)
                    hit.add(n)
            if hit:
                voting_sites[target] += 1
                for n in hit:
                    site_hits[target][n] += 1
    ranked = []
    for target, v in votes.items():
        top = v.most_common(2)
        runner = top[1] if len(top) > 1 else ("", 0.0)
        ranked.append((top[0][1], target, top[0][0], runner))
    ranked.sort(reverse=True)
    for sc, target, n, runner in ranked:
        # most call sites with known strings must agree (a strcmp-like function is
        # called with many strings that other functions also take)
        coverage = site_hits[target][n] / voting_sites[target]
        if sc >= 3 and sc >= 3 * runner[1] and coverage >= 0.75:
            accept(target, n, "args", sc, runner[0], "%d of %d call sites agree (%d calls)" % (
                site_hits[target][n], voting_sites[target], sites[target]))

    # --- pass 4: call-graph gaps --------------------------------------------------
    src_by_name = collections.defaultdict(list)
    for f in src:
        src_by_name[f["name"]].append(f)
    known_names = {a: v[0] for a, v in found.items()}
    for a, f in xbe.items():
        if not is_unnamed(f["name"]) and f["section"] == ".text":
            known_names[a] = f["name"]

    for _round in range(10):
        votes = collections.defaultdict(collections.Counter)
        why = collections.defaultdict(list)
        for a, (name, *_rest) in list(found.items()):
            if a not in xbe:
                continue  # handler without a Ghidra function
            xcalls = [t for t, _ in xbe[a]["calls"]]
            for sf in src_by_name[name]:
                scalls = [c for c, _ in sf["calls"] if (c in defined or c in LIBC) and c not in SKIP_CALLS]
                # anchors: known XBE callees whose name appears in the source sequence
                # (in order, greedy)
                anchors = [(-1, -1)]
                j = 0
                for i, t in enumerate(xcalls):
                    n = known_names.get(t)
                    if n is None:
                        continue
                    for k in range(j, len(scalls)):
                        if scalls[k] == n:
                            anchors.append((i, k))
                            j = k + 1
                            break
                anchors.append((len(xcalls), len(scalls)))
                for (i0, k0), (i1, k1) in zip(anchors, anchors[1:]):
                    # distinct callees in first-call order; a gap pairs up when both
                    # sides hold the same small number of them and none is known
                    xgap = list(dict.fromkeys(xcalls[i0 + 1:i1]))
                    sgap = list(dict.fromkeys(scalls[k0 + 1:k1]))
                    if not xgap or len(xgap) != len(sgap) or len(xgap) > 3:
                        continue
                    if any(t in known_names for t in xgap) or any(n in taken for n in sgap):
                        continue
                    for t, n in zip(xgap, sgap):
                        if t in candidates:
                            votes[t][n] += 1 if len(xgap) == 1 else 0.5
                            why[(t, n)].append(name)
        new = 0
        ranked = []
        for t, v in votes.items():
            top = v.most_common(2)
            runner = top[1] if len(top) > 1 else ("", 0)
            ranked.append((top[0][1], t, top[0][0], runner))
        ranked.sort(reverse=True)
        for sc, t, n, runner in ranked:
            if sc >= 2 and sc >= 2 * runner[1] + 1:
                how = "graph"
                if accept(t, n, how, sc, runner[0], "in " + ", ".join(sorted(set(why[(t, n)]))[:4])):
                    known_names[t] = n
                    new += 1
        if not new:
            break

    w = csv.writer(sys.stdout, lineterminator="\n")
    w.writerow(["address", "name", "source_file", "pass", "score", "runner_up", "evidence"])
    for a in sorted(found):
        name, how, sc, runner, ev = found[a]
        w.writerow([a, name, ";".join(sorted(files_of[name]))[:120], how, "%.2f" % sc, runner, ev])


if __name__ == "__main__":
    main()
