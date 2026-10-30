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
// Pass-through calls log their result (EAX) after the call; `ret:al` logs only AL (a C++
// bool), `ret:void` nothing (void, or a structure returned through a hidden pointer).
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
    // title_rdtsc (the RDTSC trampolines' clock, a 64-bit result): EDX:EAX = 0 on both sides
    // (the other recorded calls set only EAX).
    Clock,
    // KeQuerySystemTime(time): *time = 0 on both sides (GetSystemTimeAsFileTime -> CRT _clock: the file
    // request and sound bank timers, CL_Frame).
    SystemTime,
    // Kernel file exports that test processes run for real (pass-through, docs/comparison-harness.md
    // "File calls"): each side runs the loader's own export on its own copy of the process, so its
    // handles, file positions, events and APCs stay consistent with its title memory. Both sides'
    // calls are logged (arguments, the path, then the result) and must agree. Anything that would
    // change a file ends the test as inconclusive instead.
    Pass,               // runs as it is
    PassCreateFile,     // NtCreateFile: only FILE_OPEN without write access; logs the path (arg 2)
    PassOpenFile,       // NtOpenFile: only without write access; logs the path (arg 2)
    PassAttributes,     // NtQueryFullAttributesFile: logs the path (arg 0)
    PassDirectory,      // NtQueryDirectoryFile: logs the file mask (ANSI_STRING*, arg 8)
    PassSetInformation, // NtSetInformationFile: FilePositionInformation (14) only
    // The DirectSound replacements (audio/test_mode.h: no output in test processes). Like Pass, but
    // only 16 KB below the call are zeroed (their host frames are small; decompiled callers that
    // run title code 128 KB below their own frame, CL_Frame, would lose it to 128 KB).
    PassSound,
};

// Whether a recorded call is a pass-through (runs the real export in the test process).
constexpr bool is_pass(SdkStandIn s) { return s >= SdkStandIn::Pass; }

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
    // Around the game's fork of each test: host state that pass-through calls use (the
    // DirectSound replacements) is locked before, so that the copy holds it between changes,
    // and released after, in the game and in the copy.
    void (*before_fork)() = nullptr;
    void (*after_fork)() = nullptr;
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

// A title thread's registers as pushad after pushfd leaves them (lowest address first).
struct TitleRegisters {
    uint32_t eflags, edi, esi, ebp, esp_ignored, ebx, edx, ecx, eax;
};

enum class ForceResult { Started, Busy, Failed };

// Nonzero in --compare's test processes (host code other than the harness and the game
// library is not executable there): title-side thunks check it before calling the loader.
const uint8_t* in_test_flag();

// In a test process: a pass-through SDK call (the DirectSound replacements) found a host lock
// that another thread held at the fork; ends the test as inconclusive, naming `which`
// (audio::TestLock). Never returns.
[[noreturn]] void host_lock_held(uint32_t which);

// Tests a call the title does not make (shutdown and free paths the campaign never
// reaches): the hooked function `name` runs, original and decompiled, on a copy of the
// game as if the title code that holds `registers` had called it with its return address
// slot at `esp`; the game itself goes on without the call. `args`: comma-separated
// "eax=E" ... "ebp=E" (not esp) and stack words in order; E is a number, "[E]" (the title
// word there), "&N" (the address of stack word N), "E+N" or "E-N". `when` (empty: always): an E that must be nonzero (and readable) now,
// else Busy. Busy: no free test slot or `when` is 0 (try again later). Failed: why in
// `error` (not hooked - settled, not Compare or no --compare -, a bad argument); Started: the
// values used, in `error` too.
ForceResult force_call(const char* name, const char* args, const char* when, const TitleRegisters& registers,
                       uint32_t esp, std::string& error);

// Waits until the tests in flight have reported (at most timeout_ms + 2 s). Tests run
// beside the game: a call's verdict arrives a few ms after the call.
void drain();

// Waits for the tests in flight (drain), then adds the counts since the last flush to
// config.results. Any thread, any time; call it before the process exits or re-execs.
void flush();

}  // namespace loader::compare
