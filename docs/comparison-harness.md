# Comparison harness

Checks automatically, while the game runs, that a decompiled function behaves like
the original. Code: `loader/compare.h`, `loader/compare_linux.cpp` (Linux only;
`compare_win32.cpp` only installs replaced functions). Test: `tests/compare_test.cpp`.

## Using it

1. Decompile a function into `game/code/` (portable C++, the original's calling
   convention: `XB_THISCALL`, `XB_STDCALL`, ...; layout `static_assert`s under
   `#if UINTPTR_MAX == 0xFFFFFFFF`).
2. Add it to `kFunctions` in `game/code/functions.cpp`: name, default.xbe address,
   codmp_xboxf.xbe address (0 until matched), stack argument bytes, result kind
   (`None`, `Al`, `Eax`, `EdxEax`, `St0`) and `Status::Compare`.
3. Play or run unattended with `--compare` (`--level N` gets there fastest; the PC
   features are off, see below). Sampled
   calls are tested: the first 16 of each function, then every 64th. A function with
   100 matched tests over all runs (`functions.csv`) and no mismatch or drift is
   *settled*: not hooked in later runs, and no longer tested in the run that reaches
   it (`Config::enough_matches`). A mismatch or drift on record keeps it tested.
4. Read `tools/data/functions.csv` (counts, merged over runs; `functions_mp.csv` for
   multiplayer) and the `compare` lines in the log (the first 5 issues per function,
   with the differing registers or bytes).
5. When a module is done, test it all once more: `--compare-all` tests the settled
   functions too (final check; counts add to the same CSV). A function with many
   tests, no mismatches and no drift then becomes
   `Status::Replaced`: from then on it runs instead of the original, in every build.
   Drift on a function that does float math can be accepted after reading it.

Conventions in `game/code/`: the title's globals are reached with `title_global<T>(address)`
(title_data.h; such functions stay campaign-only until their multiplayer addresses are
known); functions taking arguments in EAX/EDX/ECX use `XB_REGS3` (platform/compiler.h);
the library is built with `-msse -mfpmath=sse` so float math rounds like the title's SSE
code (keep the original's operation order; `0 - x` is not `-x` for x = 0). MXCSR's
exception flags are compared too: where the original computes whole packed vectors
(DIVPS) and uses one lane, compute the other lanes as well and keep them with
`platform_keep` (compiler.h) - a 0/0 in an unused lane sets the invalid flag. No range-for
over a braced list (`for (int i : {1, 2})`): the Debug build calls
`std::initializer_list` members out of line, the loader's copies win, and every test
ends inconclusive ("called host code"); use a static array. `--compare`
implies `--pc-patches off`: the loader's PC changes to the game's code (60 fps pacing,
mouse look, aim assist off, widescreen menu/objectives hooks) stay out, so compared
functions run as on the Xbox (30 fps) and patched ones such as nglSetVBlankMode can be
tested. The presentation is the Xbox's as well: settings.ini is ignored and, unless
given explicitly, 4:3 (no EEPROM widescreen flag, which changes the game's own code
paths), the game's fonts (no sharper text), render scale 1, no anisotropic filtering,
brightness 1 (user, 2026-10-18: test with nothing but what the test needs). Before
that date every run had the widescreen flag and sharper text on; the counts were
re-checked with `--compare-all` in the corrected setup. Only what the game needs on a PC stays (privileged code, RDTSC, SDK replacements,
the MP heap lock, `--level`). `--compare --pc-patches on` plays with them; then grep
`loader/` before adding a function - a patched one is not compared. A function the PC
features patch cannot become `Replaced` until that patch calls the decompiled version.

## How a test runs

The function's entry jumps to a stub (`push index; jmp xbcmp_entry`) that saves all
registers and calls the dispatcher. For a tested call:

- The loader forks a child, which forks a grandchild before anything runs, so that
  both start from the same copy of memory: the child runs the **original**, the
  grandchild the **decompiled** version. The parent waits (one test at a time) and
  then runs the original itself, through a trampoline, so the game always goes on
  with the original's results.
- Each test process first makes every host mapping **non-executable** except the
  title's code, the harness's section (`xbcmp_text`), `libcod_bro_game.so` and the C
  runtime (libc, libm, libstdc++, libgcc_s, ld.so). Any call into other host code -
  a kernel export, an SDK replacement, the window, GPU or sound - faults, and the
  test ends as *inconclusive* with the caller named. So a test process can never
  touch the parent's devices, sockets or files.
- Then it clears the soft-dirty bits (`/proc/self/clear_refs`) and runs the function
  with the caller's registers and a return address of `xbcmp_landing`, which saves
  the registers and x87/SSE state (FXSAVE). Pages written are the soft-dirty ones
  in `/proc/self/pagemap` (private anonymous mappings).
- Both report over pipes; the parent asks both for every page either one wrote and
  compares.
- Calls the tested function makes into other hooked functions run their originals:
  each stub checks `xbcmp_in_test` (set in test processes) and jumps straight to the
  trampoline, since the dispatcher is not executable there. Decompiled code reaches
  title functions not decompiled yet with `title_function<F>(address)`.
- After the mprotect a test process runs only `xbcmp_text` code, with raw system
  calls: no C runtime, no compiler-added calls (`-fno-stack-protector`,
  `-fno-tree-loop-distribute-patterns` for that file). Check after changes:
  `objdump -d -j xbcmp_text build/linux-loader/bin/cod_bro_loader | grep call`
  must show only calls inside the section.

## What is compared

- Registers: the result (by kind; `Al` = low byte only), EBX/ESI/EDI/EBP, ESP after
  the return (a wrong calling convention or pop size), EFLAGS.DF, x87 control word,
  MXCSR, x87 stack depth and its values.
- Memory: every byte of every page either side wrote, except the stack below the
  caller's stack pointer plus the return address and arguments (callee scratch),
  and the calling thread's C runtime block (glibc's thread descriptor, which holds
  the thread id, and its rseq area, where the kernel writes the current CPU).

## Outcomes

- **matched**: identical.
- **drift**: only aligned 32-bit values differ, and each pair reads as two normal
  floats within 1e-5 of each other (x87 vs SSE rounding, another operation order).
  Plain integers can look like that too (a counter next to text: one run had it),
  so drift is never counted as matched.
- **mismatched**: anything else differs, or only the new version crashed.
- **inconclusive**: either side reached host code, did not finish in 2 s (a wait
  for a thread that does not exist in the test process), wrote over 128 MB, or the
  original crashed.

## Cost

About 10 ms per test (two forks of the whole loader; Debug build). Mission 1 with
the first three functions: about 2,200 tests in 110 s; the game stays playable but
slower while many tested functions are called often. Settling keeps that cost on the
new functions: mission 1 with 104 functions (62 settled at start) ran 186 tests in
60 s instead of about 75 per second. Tune `first_calls`/`every`/`enough_matches` in
`compare::Config` if needed.

## Limits

- Functions whose first 5 bytes hold a call, a jump or an instruction the decoder
  does not know are not hooked (logged). Conditional branches there are moved
  (re-encoded as near branches in the trampoline) unless they jump back into those
  5 bytes. Code elsewhere that jumps back into those bytes would break the
  trampoline; MSVC prologues do not.
- A decompiled function taken by address in the loader executable would get its
  PLT entry, which is host code to the harness: take addresses inside the library
  (`game::functions()` does).
- Calls into kernel exports and SDK replacements end a test, so functions that need
  them (files, locks, D3D) stay inconclusive. Possible later: pure stand-ins for
  common ones inside the test processes (critical sections without contention,
  Rtl string functions).
- Later, optional: record a function's inputs during play and replay them offline
  as unit tests.
