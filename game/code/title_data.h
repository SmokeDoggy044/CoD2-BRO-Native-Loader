#pragma once

// The title's own globals, reached at their default.xbe addresses while the route-B
// loader maps the original image there. Decompiled functions that use one stay
// campaign-only (multiplayer address 0) until the globals are ours (route C).

#include <cstdint>

#include "compiler.h"

namespace game {

template <typename T>
T& title_global(uint32_t address) {
    return *reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A title function not decompiled yet, called at its default.xbe address. F carries
// the calling convention (XB_CDECL etc.).
template <typename F>
F* title_function(uint32_t address) {
    return reinterpret_cast<F*>(static_cast<uintptr_t>(address));
}

// Register inputs for title_call_at_regs.
struct TitleRegs {
    uint32_t eax, ebx, ecx, edx, esi, edi;
};

}  // namespace game

// Calls `function` with ESP at `top` minus its stack arguments (args[0] pushed last) and
// the registers in `regs`; returns its EAX. For callers that run each callee where the
// original's call puts it (an asm entry moves the C frame below a gap): title code stores
// addresses of its stack locals in globals (CoD BRO FX_AddParticleComponents: 0x00971800).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_at_regs(uint32_t top, uint32_t function,
                                                            const game::TitleRegs* regs, uint32_t count,
                                                            const uint32_t* args);

// While title_call_at* runs a callee at the original's position, the host frames below
// that position (the caller's C frame under its entry's gap) stay live: the helpers keep
// their lowest address as this thread's floor (game_floor_enter/leave), and an asm entry
// moves its C frame below its gap with GAME_GAP_ESP, which also stays below the floor -
// else an entry reached that way (our FX_FlushSpawnQueue calling our FX_SpawnQueuedEffect
// at the original's ESP) put its C frame on its caller's. Keeps EAX, ECX and EDX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL game_floor_enter(uint32_t floor);
extern "C" PLATFORM_HIDDEN void XB_CDECL game_floor_leave(uint32_t previous);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL game_body_esp(uint32_t proposed);
// Its scratch (the saved registers, game_body_esp's call) sits 64 KB below the gap, where only
// our C runs: not just below the entry, where some C rebuilds the original's frame and reads words
// the original leaves unset (NAL_Packed16EntropyIKSpin_Dequantize copied our saved registers out),
// not inside the gap, where deep title code reads stale words (the SV_Trace family's 0x804fff2c
// mismatches rose eightfold at half the gap), and past a caller's live C frame (nested case).
// The body's ESP is not the entry's minus the gap (aligned down, or below the floor): the macro
// pushes the ESP it started with, so the epilogue undoes it with `popl %esp` once the words
// pushed after it are gone (an `addl $gap, %esp` popped a wrong return address or registers:
// EDI after R_AddEntityModel_entry, in R_RenderView's nested calls).
#define GAME_GAP_ESP(gap)                                                                 \
    "subl $(" gap "+0x10000), %esp\n pushl %eax\n pushl %ecx\n pushl %edx\n"              \
    "leal 12+0x10000(%esp), %eax\n pushl %eax\n" PLATFORM_ASM_CALL(game_body_esp)         \
    "movl %esp, %ecx\n movl %eax, %esp\n leal 16+0x10000+" gap "(%ecx), %edx\n pushl %edx\n"      \
    "movl 4(%ecx), %edx\n movl 12(%ecx), %eax\n movl 8(%ecx), %ecx\n"
