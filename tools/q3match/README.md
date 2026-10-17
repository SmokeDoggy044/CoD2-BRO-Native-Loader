# Quake 3 / RTCW name matching

Proposes Quake 3 lineage names for unnamed `.text` functions in default.xbe. It only
writes a CSV: nothing is renamed in Ghidra until someone checks the proposals and
applies them.

## Run

1. Reference source (GPL-2.0), outside the repo:

       git clone --depth 1 https://github.com/id-Software/Quake-III-Arena.git ../reference/Quake-III-Arena
       git clone --depth 1 https://github.com/id-Software/RTCW-SP.git ../reference/RTCW-SP

2. Index it (a small scanner; skips bspc, jpeg, q3_ui, platform code):

       python3 -I tools/q3match/source_index.py REF/Quake-III-Arena/code > q3.json
       python3 -I tools/q3match/source_index.py REF/RTCW-SP/src > rtcw.json

3. Export the XBE side from Ghidra: `tools/ghidra/ExportMatchData.java` with the output
   path `tools/data/default_xbe_matchdata.tsv` (generated, not committed).

4. Match:

       python3 -I tools/q3match/match.py tools/data/default_xbe_matchdata.tsv q3.json rtcw.json \
           > tools/data/q3_name_proposals.csv

## Passes and how far to trust them

| pass | evidence | trust |
|---|---|---|
| `handler` | a code pointer registered under the same command name (`Cmd_AddCommand("toggle", Cvar_Toggle_f)`) or in a `{"name", function}` table | high |
| `strings` | the function uses the same distinctive strings as one source function | high when the strings are function-specific (error messages); check ones that only share cvar names |
| `args` | most call sites pass the same string arguments as calls to one source function (`Cvar_Get("cl_run", ...)`) | medium: strcmp-like functions get many votes; 75% of voting call sites must agree |
| `graph` | the single unknown callee between two known callees, voted over several matched callers | low: check every one |

Spot checks (2026-10-17): Cbuf_AddText 0x002aabb0 (text in EAX), Com_sprintf
0x002a9460 (vsnprintf + terminator, register arguments) and CG_Printf 0x00277660
(vsprintf only, the print is stripped) are right. Rejected and now filtered out: Q_stricmp for
0x00300b30 (a key-table store) and a graph guess that swapped COM_ParseExt
(really 0x002b0250) with SkipWhitespace.

## Applying

`tools/ghidra/ApplyNames.java CSV` creates missing functions and renames only
functions that still have a FUN_ name; then re-run `ExportSymbols.java` into
`tools/data/default_xbe_symbols.txt`. Applied 2026-10-17: every proposal except the
graph pass (CG_Printf kept, checked by hand), 96 names.

## Limits

The release build stripped most messages (only ~1,200 of 9,200 `.text` functions
reference a string), and Treyarch's engine has diverged from Q3 a lot, so call
sequences rarely line up: the graph pass only spreads a few steps. Expect about a
hundred names from this tool. The rest needs manual matching subsystem by subsystem,
starting from these anchors.
