#pragma once

// Comparison harness: puts the decompiled functions (game::functions()) in place of
// the originals, or tests them against the originals while the game runs. A tested
// call runs both versions, each in its own copy of the process, and compares their
// registers and every byte of memory they wrote; the game then goes on with the
// original. Design: docs/comparison-harness.md. Testing is Linux-only.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "functions.h"

namespace loader::compare {

// An SDK replacement that test processes record instead of calling: the call returns
// at once (EAX = 0, its stack arguments popped) and its arguments go into a log that
// both sides must agree on - so functions that draw (D3D state setters, draws) are
// testable. Only for functions whose results the title does not read back.
// `args`: one word per argument, in order: where it is (`ecx`, `edx` or `s` for the
// next stack slot), optionally `:N` = a pointer whose N bytes are recorded instead of
// its value, `:aK*N` = the same with the value of argument K times N bytes, or `:aK*aM`
// = argument K times argument M bytes; `x` = a stack slot that is not logged (an output
// pointer, maybe into the caller's stack frame, which differs between the two sides).
// What a recorded call does besides being logged: most return 0 and change nothing.
// The reference-count stand-ins work on the resource's count (the low 16 bits of its
// first word, in title memory, or of a pool surface) as render does, a surface's first
// and last references forwarded to its texture, so that code reading counts or buffers
// back stays testable; a resource they cannot read ends the test as inconclusive.
enum class SdkStandIn : uint8_t {
    None,
    AddRef,        // D3DResource_AddRef(resource): count + 1, returns it
    Release,       // D3DResource_Release(resource): count - 1 (0: destroyed), returns it
    ReturnAddRef,  // returns *value (a resource or null) after AddRef on it
    PushBegin,     // D3DDevice_BeginPush_4(dwords): returns space in the test process
    PushEnd,       // D3DDevice_EndPush(end): logs the dwords written since PushBegin
    LevelCount,    // D3DBaseTexture_GetLevelCount(texture): the mip count in its Format
    // D3DTexture_GetSurfaceLevel2(texture, level) / D3DCubeTexture_GetCubeMapSurface2(
    // texture, face, level): a surface from a pool in the test process's own area,
    // filled as render's create_level_surface does (count 1, a reference on the texture).
    SurfaceLevel,
    // D3DTexture_LockRect(texture, level, locked, rect, flags) / D3DSurface_LockRect(
    // surface, locked, rect, flags): {pitch, bits} into *locked as render's lock() does.
    TextureLock,
    SurfaceLock,
    BufferLock,  // D3DVertexBuffer_Lock2 / D3DPalette_Lock2(buffer, flags): Data | 0x80000000
    // D3DSurface_GetDesc(surface, desc): D3DSURFACE_DESC into *desc as render's describe() does.
    SurfaceDesc,
};

struct SdkCall {
    const char* name;
    uint32_t host_address;  // the replacement's entry: where a test process faults
    const char* args;
    SdkStandIn stand_in = SdkStandIn::None;
    const uint32_t* value = nullptr;  // ReturnAddRef: host memory read at the call
};

struct Config {
    bool enabled = false;     // test Compare functions (otherwise their originals run untouched)
    bool multiplayer = false;  // use the functions' codmp_xboxf.xbe addresses
    // The title's code, which stays executable in the test processes.
    uint32_t image_begin = 0;
    uint32_t image_end = 0;
    // Host libraries the test processes may run (substrings of their file names): the
    // decompiled code's and the C runtime's. Any other host code a test reaches -
    // kernel exports, SDK replacements, the window, GPU, sound - ends the test as
    // inconclusive, so that a test process never touches the parent's devices or files.
    std::vector<std::string> libraries = {"libcod_bro_game", "libc.so", "libm.so", "libstdc++", "libgcc_s",
                                          "ld-linux"};
    // Host code pages the test processes may run as well (begin, end): generated
    // trampolines whose calls into the host are recorded SDK calls (the RDTSC ones).
    std::vector<std::pair<uint32_t, uint32_t>> host_code;
    // Lowest address of the calling thread's stack, or 0 if unknown. The stack below
    // the caller's frame is scratch space and is not compared.
    uint32_t (*stack_limit)() = nullptr;
    // A title address with its symbol, for reports (null: hex only).
    std::string (*describe)(uint32_t address) = nullptr;
    // CSV with per-function counts, merged into on flush(); empty = none.
    std::filesystem::path results;
    uint32_t timeout_ms = 2000;  // per test; a test still running then is inconclusive
    // Which calls are tested: the first `first_calls`, then every `every`-th.
    uint32_t first_calls = 16;
    uint32_t every = 64;
    // A function with this many matched tests (results, over all runs) and no mismatch
    // or drift is settled: not tested any more until a run with 0 (--compare-all), the
    // final check of a module. Settled ones are not hooked at all.
    uint32_t enough_matches = 100;
    // Tests running at once (0: half the CPUs). A sampled call that finds them all busy
    // is not tested.
    uint32_t parallel = 0;
    // SDK replacements recorded in test processes (any other ends a test as inconclusive).
    std::vector<SdkCall> sdk_calls;
};

// Hooks `functions`: Replaced ones always, Compare ones only when testing is enabled.
// Call once, before the title runs. `functions` must outlive the process.
void install(std::span<const game::Function> functions, const Config& config);

struct Counts {
    uint64_t calls = 0;
    uint64_t tested = 0;
    uint64_t matched = 0;       // identical registers and memory
    uint64_t drift = 0;         // only floats differ, and only in their last bits
    uint64_t mismatched = 0;    // anything else differs, or only the new version crashed
    uint64_t inconclusive = 0;  // host code reached, timeout, the original crashed, ...
};

// Counts of the tested function named `name` since install (zero if not tested).
Counts counts(const char* name);

// Waits until the tests in flight have reported (at most timeout_ms + 2 s). Tests run
// beside the game: a call's verdict arrives a few ms after the call.
void drain();

// Waits for the tests in flight (drain), then adds the counts since the last flush to
// config.results. Any thread, any time; call it before the process exits or re-execs.
void flush();

}  // namespace loader::compare
