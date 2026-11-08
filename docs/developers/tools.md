# Tools we use

- **[Ghidra](https://ghidra-sre.org/)**, the NSA's open-source reverse-engineering
  suite, for disassembling and exploring the game. The AI works in it through
  an MCP server for Ghidra, decompiling functions and running analysis scripts
  directly.
- **Our Ghidra scripts** (`tools/ghidra/`): apply names from our name lists, export
  the symbol file the rest of the tools use, name settings variables after the
  settings they hold, and export per-function data for matching.
- **`tools/fnpack.py`**: one compact text per function from the XBE and the symbol
  file: callers, callees, globals and constants, strings, which registers and stack
  slots it takes, and the traps to watch for (float instructions, code the harness
  cannot hook), with the disassembly named inline.
- **`tools/namepack.py`**: naming packets. It ranks unnamed functions by how much
  there is to go on (strings, named callers and callees, Q3 matches, known struct
  fields) and prints each one's context with its decompile in one text, from an
  export by `tools/ghidra/ExportNaming.java`. Its `fields` report lists every offset
  of a struct that the code uses, with the functions that use it.
- **`tools/data/structs.txt`**: the struct layouts found so far (entities, cvars,
  actors, ...). `tools/ghidra/ApplyStructs.java` creates them in Ghidra and types the
  globals and parameters listed there, so the decompiler shows field names.
- **`tools/scaffold.py`**: writes a batch's declarations, stubs (with the asm entry
  for register arguments and the callees as ready calls) and registrations from the
  same analysis; an `#error` in each stub marks the body still to write.
- **`tools/engine_src.py`**: where each file of `src/engine/` is, and which module
  folder a new file goes in; the other tools use it.
- **`tools/compare_batch.py`**: builds the loader, runs the comparison in several
  missions at once and prints one line per function.
- **The Quake III Arena and Return to Castle Wolfenstein source code** (GPL), the
  engine's ancestors, plus **q3match** (`tools/q3match/`), which matches the game's
  functions to them by their strings and calls.
- **`tools/vc71match/`**: compiles `src/engine/` with the title's own compiler (Visual
  C++ 7.1 from the Xbox SDK, under Wine) and compares the code it generates with the
  original's.
- **The comparison harness** (`--compare`), built into the loader. For each tested
  call it copies the running game, runs the original and our version side by side,
  and compares registers and every byte of memory they write. Calls into graphics
  code are recorded and compared too, so drawing code can be tested. Results go to
  `tools/data/functions.csv`. See [comparison-harness.md](comparison-harness.md).
- **`tools/progress.py`**, which tracks every function's state (unnamed, named,
  decompiled, tested, rewritten) by part of the game, and lists what's left. With
  `--readme` it updates the README's summary and [docs/progress.md](../progress.md).
- **`tools/levelreach.py`** and **`tools/untested_by_level.py`**: which missions can
  reach a function, and which functions nothing reaches at all.
- **`tools/smoke_test.sh`**, a quick unattended play-through run after every batch
  to make sure the game still works.
- **[Claude Code](https://claude.com/claude-code)** with Claude, Anthropic's AI
  model, which does the naming and decompiling (see
  [Made with AI](../../README.md#made-with-ai)).
- GCC and CMake for building, and objdump for checking the compiled code.
