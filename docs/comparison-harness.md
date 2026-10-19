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
4. Read `tools/data/functions.csv` (counts, merged over runs - also over runs at the
   same time: each flush merges under a lock (`functions.csv.lock`); `functions_mp.csv` for
   multiplayer) and the `compare` lines in the log (the first 5 issues per function,
   with the differing registers or bytes; inconclusive tests and mismatches/drift have
   separate caps of 5, so front-end inconclusive calls do not hide the first mismatches).
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

- The game forks a **coordinator** - a copy of its memory at the call - and goes on
  at once with the original, through a trampoline, so it always continues with the
  original's results. Up to `Config::parallel` tests (default: half the CPUs) run at
  once; a sampled call that finds them all busy is not tested.
- The coordinator forks a child, which forks a grandchild before anything runs, so
  that both start from the same copy of memory: the child runs the **original**, the
  grandchild the **decompiled** version.
- Each test process first makes every host mapping **non-executable** except the
  title's code, the harness's section (`xbcmp_text`), `libcod_bro_game.so` and the C
  runtime (libc, libm, libstdc++, libgcc_s, ld.so). Any call into other host code -
  a kernel export, an SDK replacement, the window, GPU or sound - faults, and the
  test ends as *inconclusive* with the caller named. So a test process can never
  touch the game's devices, sockets or files.
- The dead stack below the caller's stack pointer (up to 16 KB, within the thread's
  stack) is zeroed as the function starts (`xbcmp_resume`), on both sides. Code that
  sends uninitialized locals - shader constant blocks whose unused lanes the title
  never sets (cdWorld_Draw, cdPropeller_Draw) - then sends the same bytes, and the
  decompiled version writes zeros there. Without it those calls mismatch on stack
  garbage (checked: cdWorld_Draw 839 of 1,344 tests).
- Then it clears the soft-dirty bits (`/proc/self/clear_refs`) and runs the function
  with the caller's registers and a return address of `xbcmp_landing`, which saves
  the registers and x87/SSE state (FXSAVE). Pages written are the soft-dirty ones
  found with the PAGEMAP_SCAN ioctl on `/proc/self/pagemap` (private anonymous
  mappings; per-page pagemap reads where the kernel lacks it).
- Both report over pipes; the coordinator asks both for every page either one wrote,
  compares, and sends the verdict (one pipe write) to the game's collector thread,
  which records it, logs and flushes the CSV every 64 tests. A reaper thread waits
  for the coordinators to exit. `compare::drain()` waits for the tests in flight;
  `flush()` (exit, reboot) drains first.
- Lifetimes: test processes die with their coordinator (PDEATHSIG). Coordinators
  cannot use PDEATHSIG, which fires when the forking *thread* ends (title threads
  may end before their test does): they watch a pipe whose write end only the game
  holds and exit at its EOF. The collector kills a coordinator still silent after
  `timeout_ms` + 3 s. Coordinators run with the default crash signals (not the
  game's handlers, which log and flush) and do not log.
- Calls the tested function makes into other hooked functions run their originals:
  each stub checks `xbcmp_in_test` (set in test processes) and jumps straight to the
  trampoline, since the dispatcher is not executable there. Decompiled code reaches
  title functions not decompiled yet with `title_function<F>(address)`.
- After the mprotect a test process runs only `xbcmp_text` code, with raw system
  calls: no C runtime, no compiler-added calls (`-fno-stack-protector`,
  `-fno-tree-loop-distribute-patterns` for that file). Check after changes:
  `objdump -d -j xbcmp_text build/linux-loader/bin/cod_bro_loader | grep call`
  must show only calls inside the section.

## Register arguments

Arguments in ESI, EDI or EBX, or floats in XMM registers, have no C spelling. Such a
function gets an entry in assembly (`PLATFORM_ASM_ENTRY(name, body)`, platform/compiler.h)
that pushes them, copies any stack arguments below them (`pushl 4*N(%esp)` N times for N
of them), calls the C version (`PLATFORM_ASM_CALL`: an `extern "C" PLATFORM_HIDDEN`
function, so the call in the shared library needs no PLT), pops and returns as the
original does; `functions.cpp` registers the entry. The harness hands it the caller's
registers, XMM included (FXRSTOR in `xbcmp_resume`). Example: cdSimpleAlpha_PulseColor
(EAX out, ECX colour, XMM1 speed, five stack floats) in game/code/shaders.cpp. Checked
with a planted bug in its XMM1 argument (caught in the helper and its caller).

## Recorded SDK calls

Code that draws calls the D3D replacements (state setters, draws), which are host
code. For the ones listed in `kRecordedD3dCalls` (loader/patches.cpp: name and argument
layout) a test process does not end the test there: the fault on the replacement's
entry is caught in the signal handler, which logs the call - its index and arguments,
for a pointer argument the bytes it points to (a fixed size, or another argument times
a size: constant counts, stream counts) - and returns to the caller as the
replacement would (EAX = 0, stack arguments popped). Both sides must make the same
calls with the same arguments in the same order; the first difference is reported
("SDK call 3 D3DDevice_SetTexture: arguments differ at word 2"). Only functions the
title reads nothing back from are listed (no getters, no Lock), and the replacement's
own writes (D3D's render-state block in title memory) are skipped on both sides alike.
The arguments are compared as values; pointers to data on the stack are recorded by
content, since the two sides' stack frames differ. `tests/compare_test.cpp` checks a
right and a wrong recorded call.

Stand-ins: a few recorded calls also do what the replacement would, so that code reading
the result back stays testable (`compare::SdkStandIn`, `kRecordedD3dCalls`):
D3DResource_AddRef/Release change the resource's reference count (the low 16 bits of its
first word, title memory) and return it, D3DDevice_GetBackBuffer2 returns the back buffer
(read from render's host global) after an AddRef. Cases they do not cover (a count reaching
0, a parent surface) end the test as inconclusive. This made nglGetBackBufferTexture's
callers (R_ClearAlpha, nglPushScene's nglSetDefaultSceneParams) testable.

## Callers that rebuild the original's frame

A decompiled caller runs its callees deeper on the stack than the original, and some title
code depends on its stack position: it stores addresses of stack buffers (R_AddWorldSurfaces'
link buffer in 0x0095e2d0) or copies locals it never set (stale stack). R_RenderView shows
the remedy: its asm entry rebuilds the original's frame (the same aligned ESP, locals passed
by pointer at the original's offsets) with 128 KB kept free below it, and every call goes
through `title_call_at` with ESP where the original's call has it (frame minus the words it
still had pushed). After calls into our own functions the 16 KB below are zeroed again (the
title's later reads of uninitialized locals then see what a fresh test has). Stale-stack
reads inside title callees can still differ (R_AddEntityModel's subtree): bisect by calling
original addresses instead of ours for groups of callees.

Debugging variables: `XBCMP_ONLY=name,name` hooks only those functions and keeps testing
them past 100 matches (settled ones too); `XBCMP_EVERY=N` tests every N-th call instead of
every 64th (reaches calls the sampling skips); `XBCMP_SHOW=N` lists N runs of differences
per report (default 6; above 6 also the entry ESP and the stack limit). Reports are capped at
PIPE_BUF, so static debug buffers in host memory crowd them out: keep debug data on the stack.

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

The numbers in this paragraph are from the first version (the game waited about 10 ms
per test, two forks of the whole loader; Debug build). Mission 1 with
the first three functions: about 2,200 tests in 110 s; the game stays playable but
slower while many tested functions are called often. Settling keeps that cost on the
new functions: mission 1 with 104 functions (62 settled at start) ran 186 tests in
60 s instead of about 75 per second. Tune `first_calls`/`every`/`enough_matches` in
`compare::Config` if needed.

Each flush logs a `timing` line: mean and max ms per phase of the completed tests,
pages scanned and written, and the game's VmSize/VmRSS/VmPTE. Mission 4, Debug build,
`--compare-all`, 130 s (2026-10-18; the function itself took 0.01 ms):

| phase | ms before | ms now | what |
|---|---|---|---|
| fork | 1.8 | 1.9 | the parent's fork (1.9 GB mapped, 280 MB resident, 1 MB page tables) |
| start | 3.8 | 3.8 | until both test processes run: includes the child's fork of the second |
| maps | 1.0 | 0.7 | each test process reads and parses /proc/self/maps |
| protect+clear | 1.1 | 0.8 | mprotect of host code + clear_refs over the whole process |
| collect | 0.9 | 0.25 | written pages out of ~93,000 scanned (PAGEMAP_SCAN now, pagemap read before) |
| reap | 4.2 | 0.01 | waiting for the test processes to exit (now a reaper thread does) |
| reports, pages, compare | 0.2 | 0.15 | |
| total | 11.1 | 5.7 | 10,880 tests in the run before, 20,304 now |

Then tests moved beside the game (coordinator + collector, 4 at once on 8 CPUs): the
game waits only for its fork of the coordinator, 2.1 ms per test ("stall"). The
coordinator's own work rose to 8.4 ms (4 tests sharing the CPUs with the game), and
"latency" (call to verdict) is about 12 ms. Same run, 130 s: 29,531 tests; 163,291
sampled calls found all slots busy. Frame rate, mission 4, 100 s (`--diag`), mean from
30 s on:

| mode | game waits for tests | tests beside the game |
|---|---|---|
| `--compare-all` | 4.0 fps, 13,583 tests | 14.5 fps, 21,926 tests |
| `--compare` (most functions settled) | 30 fps, 20 tests | 30 fps, 19 tests |

What is left for the game is the fork itself: with every function tested
(`--compare-all`) it still waits about half the time. Lower `first_calls`/`every`
for hot functions or fewer slots would trade tests for frame rate.

PAGEMAP_SCAN (Linux 6.7+; the pagemap read stays as the fallback) was checked against the
pagemap read in every test of three runs: they differed twice, both times by the page of
the calling thread's rseq area, which the kernel wrote between the two reads (it is not
compared anyway).

## Limits

- Functions whose first 5 bytes hold a call, a jump or an instruction the decoder
  does not know are not hooked (logged). Conditional branches there are moved
  (re-encoded as near branches in the trampoline) unless they jump back into those
  5 bytes. Code elsewhere that jumps back into those bytes would break the
  trampoline; MSVC prologues do not.
- A decompiled function taken by address in the loader executable would get its
  PLT entry, which is host code to the harness: take addresses inside the library
  (`game::functions()` does).
- Calls into kernel exports and SDK replacements not recorded (above) end a test, so
  functions that need them (files, locks, D3D getters and Lock) stay inconclusive. Possible later: pure stand-ins for
  common ones inside the test processes (critical sections without contention,
  Rtl string functions).
- Later, optional: record a function's inputs during play and replay them offline
  as unit tests.
