# Planned: comparison harness

Automatic check that a decompiled function behaves like the original. Only needed
if/when we start large-scale decompilation (route C); route B's handful of
decompiled functions can be tested in-game. Roughly 500-800 lines in `loader/`.

**How it works.** When the game calls a function marked `compare`, the hook
`fork()`s. The child runs the *original* on a copy-on-write copy of memory and sends
back its return value, registers and changed pages through a pipe, then exits. The
parent runs the *new* version and compares. Changed pages are found with Linux
soft-dirty tracking (`/proc/self/clear_refs` + `/proc/self/pagemap`), so the harness
never needs to know what a function reads.

**Compared:** return value (EAX, EDX:EAX, or ST0 for floats); EBX/ESI/EDI/EBP
preserved; ESP after return (catches wrong calling conventions / pop sizes); every
memory byte either side changed.

**Known problems and handling:**
- Side effects outside memory (file I/O, waits, timers) inside the function: the
  kernel layer knows it is in the test child and marks the test *inconclusive*
  instead of failed. File reads matter most: the child shares file positions.
- Float differences (original uses x87): compare bytes exactly first, then retry
  mismatches as float/double with a small tolerance; report as "float drift".
- Other threads changing memory while the new version runs: confirm mismatches
  over several calls before trusting them.
- Cost: sample (first N calls, then 1 in K), only for `compare` functions.
- LTCG/custom register conventions: reuse the per-function register layout that
  the hooks need anyway.

**Workflow.** `tools/data/functions.csv` tracks each function:

    address   name                status    calls  mismatches
    0012a6c0  Cvar_Set            compare   312    0

Decompile a batch and mark it `compare` → play normally → functions with many
matching calls become `replaced`; mismatches are reported with the differing bytes.
Functions never called stay unverified (shows coverage gaps).

**Later, optional:** record a function's inputs during play and replay them offline
as unit tests, running the original under an emulator library (e.g. Unicorn) to
capture exactly what memory it reads.
