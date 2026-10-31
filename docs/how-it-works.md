# How the native loader works

The Xbox has an x86 CPU, so the game's own code needs no translation: the loader maps
the original `default.xbe` into memory and the PC's CPU runs it directly. What we
wrote replaces everything around it: the Xbox kernel (files, memory, threads), the SDK
libraries the game uses, and a few small patches to the game's code (timing, mouse
look, widescreen, an aim-assist switch). Parts of the game have been decompiled into
C++ (`game/code/`), checked against the original function by function, and run in
place of the original code: the renderer and the sound system so far.

What it is **not**:

- **Not a recompilation.** No machine code is translated ahead of time (as static
  recompilers such as N64Recomp do for consoles with a different CPU). The original
  code runs unmodified, apart from the patches and the parts replaced by rewritten
  code.
- **Not a CPU emulator.** Only the Xbox's system software is reimplemented, the way
  Wine reimplements Windows; Cxbx-Reloaded works on the same model.

The native loader is the proof of concept for a full native build: rewrite the game
one part at a time until none of the original code is left, then build `cod_bro` from
source for Linux and Windows, needing only the game's assets. The rewriting happens in
the [decompilation repository](https://github.com/SmokeDoggy044/CoD2-BRO-Decomp).

## The parts

- **Memory:** the XBE goes to its own base address (0x10000), the Xbox's
  contiguous memory to 0x80000000, as the game expects. On Windows,
  `cod_bro_loader.exe` is a small stub linked at 0x10000 that keeps those ranges
  free and loads the loader proper from `cod_bro_loader_core.dll`.
- **Kernel:** the game's kernel import table is filled with our implementations
  (`xbox/`, `loader/kernel_thunks.cpp`).
- **Per-thread state:** Xbox code finds its thread and processor data through the
  FS segment. On Linux FS points at a fake Xbox KPCR for each thread; on Windows FS
  stays the thread's own TEB, and the few game instructions that read Xbox-only
  fields through it are patched to read TEB slots holding the same values.
- **SDK libraries:** the game's statically linked copies of Direct3D, DirectSound,
  the input library and Xbox Live are redirected to our code by name, using a symbol
  file exported from Ghidra (`render/`, `audio/`, `input/`, `online/`).
- **Rewritten game code:** each decompiled function is registered in
  `game/code/functions.cpp` with its address in the campaign and in multiplayer
  (a separate executable; 0 while not yet known there) and its status. A `Replaced`
  function runs instead of the original: the original's entry jumps to the C++
  version, which has the same calling convention, so the game's remaining original
  code calls it as before. A `Compare` function only runs under the comparison
  harness. The code is built as its own library (`libcod_bro_game.so` on Linux,
  linked into the core DLL on Windows).
- **Rendering:** the renderer runs as rewritten code that draws through our own
  graphics interface (`render/gfx_device.h`) with a Direct3D 9 back end, implemented
  by DXVK on Vulkan. On Windows the bundled DXVK (`dxvk/d3d9.dll`) is tried first and
  the `d3d9.dll` that comes with Windows is the fallback (`--d3d9 auto|dxvk|system`,
  Graphics in the launcher); no DirectX runtime or D3DX is needed. See
  [own-renderer.md](own-renderer.md) and [rendering.md](rendering.md).
- **Sound:** the game's sound code runs as rewritten code on a sound engine of our own
  on OpenAL Soft (`audio/`).

## The rewritten renderer

| | Functions |
|---|---:|
| The renderer's functions | 1,086 |
| Rewritten in C++ and passed the comparison with the original | 1,030 |
| Set aside by decision (dead code, debug tools, start-up and exit code, Xbox-only device setup) | 48 |
| Rewritten, waiting for a conclusive test | 7 |

**Rewritten** means the function has C++ source and behaved exactly like the
original in the [comparison harness](comparison-harness.md): while the game
runs, sampled calls are run twice from the same starting point, once through the
original and once through the rewrite, and the registers and every byte of memory
they write are compared, including the calls into Direct3D. Functions the campaign
never reaches were compared offline on random inputs instead. **Set aside**
functions are listed with the reason in `tools/data/set_aside.csv`.

The rest of the game's decompiled code (collision, streaming, animation, the client
game, sound, the menus, ...) is in `game/code/` too, but it only runs under the
comparison harness (`--compare`) until its part of the game is finished and switched
over. Overall progress, part by part, is tracked in the decompilation repository.
