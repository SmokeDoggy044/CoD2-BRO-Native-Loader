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
3. Play or run unattended with `--compare` (`--level N` gets there fastest). Sampled
   calls are tested: the first 16 of each function, then every 64th.
4. Read `tools/data/functions.csv` (counts, merged over runs; `functions_mp.csv` for
   multiplayer) and the `compare` lines in the log (the first 5 issues per function,
   with the differing registers or bytes).
5. A function with many tests, no mismatches and no drift becomes
   `Status::Replaced`: from then on it runs instead of the original, in every build.
   Drift on a function that does float math can be accepted after reading it.

Conventions in `game/code/`: the title's globals are reached with `title_global<T>(address)`
(title_data.h; such functions stay campaign-only until their multiplayer addresses are
known); functions taking arguments in EAX/EDX/ECX use `XB_REGS3` (platform/compiler.h);
the library is built with `-msse -mfpmath=sse` so float math rounds like the title's SSE
code (keep the original's operation order; `0 - x` is not `-x` for x = 0). Before adding a
function, grep `loader/` for its address: a function the loader already patches (e.g. the
frame pacing's nglSetVBlankMode) cannot be compared.

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
slower while many tested functions are called often. Tune `first_calls`/`every`
in `compare::Config` if needed.

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
