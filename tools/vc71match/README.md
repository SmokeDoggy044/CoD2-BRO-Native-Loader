# vc71match: static match check

Compiles `game/code/` with the title's own compiler and diffs every `kFunctions` entry
against default.xbe, instruction by instruction. No game run. It checks code
generation next to `--compare` (behaviour); the decomp stays functional and is not
held to byte matching.

## Compiler

The title's XBEs (2005-09-28) link the XDK 1.0.5849 libraries, `D3D8LTCG` among them,
so the title was built with Visual C++ 7.1 (13.10.3077) and `/GL` + `/LTCG`. The XDK
installer has the compiler in `XDK/xbox/bin/vc71/`; extract it without running the
installer:

    7z x -o../Tools/xdk5849 XDKSetup5849.17.exe 'XDK/xbox/bin/*' 'XDK/xbox/include/*' 'XDK/xbox/lib/*'

(`../Tools/xdk5849` is relative to `cod_bro/`; set `XDK5849` to the `XDK/xbox` folder
elsewhere.) It runs under Wine; the prefix lives in `build/vc71match/wineprefix`.

## Run

    python3 -I tools/vc71match/vc71match.py [FUNCTION...] [--diff] [--csv] [--flags "..."]

One line per function: score (0-100, difflib ratio of the normalized instructions),
`ok` at 100, else the instruction counts (XBE/ours). `--diff` prints each non-exact
function side by side; `--csv` writes `tools/data/vc71match.csv`. A full run takes
about 2.5 minutes (each compile round starts Wine). `VC71MATCH_WORK` moves the work
folder, so flag sets can run in parallel.

Default flags: `/O2 /Oy /G6 /arch:SSE /GR- /GF /Gy`. Of `/O1`, `/Ox`, `/G7`,
`/arch:SSE2` and no `/arch`, none scored higher (2026-10-19: 58 exact of 346).

## What the copy compiled looks like

`game/code/` is C++20; a lowered copy goes to `build/vc71match/src/`:

- `static_assert` and `PLATFORM_ASM_ENTRY` dropped; `enum class` -> `enum`,
  `alignas` -> `__declspec(align)`, `constexpr` -> `const`, `nullptr` -> `0`,
  `using X = ...` -> `typedef`, `T{x}` -> `T(x)`, `return {...}` -> a named local.
- A free `XB_THISCALL` definition becomes a member function of a struct of its own
  (VC7.1 has `__thiscall` on members only), its first parameter read from `this`.
  Declarations and calls through function types use `__fastcall` instead: ECX is the
  same, a second integer argument goes in EDX (shows as a difference).
- `title_function<F>(0xADDR)` becomes a direct call to an `extern "C"` declaration,
  so the call compares by callee name.
- A function VC7.1 still rejects (range-for, lambdas, `auto`) has its body cut and is
  reported `no-compile` with the first error.

## Reading the scores

Compared: mnemonics and operands; jumps inside a function as function-relative
offsets; calls and tail jumps by callee name (symbol file on the XBE side,
relocations on ours); every constant inside the image reads `ADDR` on both sides.
So a wrong global address is not caught here (`--compare` catches it).

Expected misses, not decomp bugs:

- LTCG: the title's inlining across files and its register arguments (EAX/EDX/ECX)
  cannot come from one-file compiles; `asm-entry` functions are not compared.
- Thiscall-typed calls through function pointers (EDX, above).
- Instruction scheduling: an equal instruction one slot earlier counts as a change.

A low score next to a clean `--compare` record is worth a read, not a rewrite.
