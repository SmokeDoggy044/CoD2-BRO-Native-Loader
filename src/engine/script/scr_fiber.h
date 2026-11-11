#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "compiler.h"

// Script threads on their own stacks (our own engine's script-thread runtime, 2026-11-07).
//
// The original runs every script thread on the scheduler's stack, 0x2000 below Scr_ResumeThread, and
// switches by copying: a wait (Scr_SuspendWaitTime/Frames/Till, 0x0011d950/990/9d0) pushes FLAGS (16-bit),
// all registers and FS:[0], hands its ESP to Scr_SaveThreadLocals (slot 13), which copied [ESP, thread +0x28)
// into 0xfc-byte chunks, and jumps back to the scheduler through the context at 0x0092709c
// (Scr_WaitRecord15/16/17 -> Scr_RestoreContext). Scr_ResumeThread copied the chunks back to the same
// addresses and popped that frame. So every C frame between the scheduler and a waiting title frame was
// copied too, and C that calls the scheduler had to sit at the original's exact stack depth.
//
// Here each thread gets a stack of its own (a fiber) for its whole life: the wait's frame stays where it
// was, Scr_SaveThreadLocals only records its ESP, and Scr_ResumeThread switches ESP to the fiber. The
// waits, wait records and Scr_RestoreContext stay as they are (they only see the context at 0x0092709c,
// which our switch writes in the same layout). Per fiber we also keep the gap-entry floor (FS:[0x14] and
// t_host_floor, title_data.h), which describes one stack; FS:[0] travels in the wait's frame as before.
// The level code's contract (docs/formats/level_boundary.md): stack-switching threads, SEH through FS:[0].
namespace game {

// The running fiber's stack range, for the crash handler's backtrace (false on the scheduler's stack).
bool scr_fiber_stack(uint32_t* limit, uint32_t* base);

// Host and thread words a script thread switch leaves describing the suspended frames, which the scheduler
// overwrites when it switches back (run() below): the fiber's wait ESP, this thread's gap-entry floor, FS:[0]
// and FS:[0x14], the stack guard's table. The compare harness skips them after a test that ended at the
// switch (loader::compare::Config::switch_ranges): they follow the C frames' depth.
void scr_fiber_switch_ranges(std::vector<std::pair<uint32_t, uint32_t>>& out);

// Scr_SaveThreadLocals' new body: the current thread's wait ESP (`frame`) is kept with its fiber.
void scr_fiber_note_wait(uint8_t* thread, uint32_t frame);

// Scr_DestroyThreadContents: the thread record goes away, its fiber back to the pool.
void scr_fiber_release(uint8_t* thread);

// An in-process title restart (own engine host 5b): every script thread stack goes; the next title makes new ones.
void scr_fiber_reset();

// 0x002fddb0 / 0x002fde40 / 0x00300850 (import slots 15-17; never return): the current thread waits on a new
// record - a time (g_scrWaitTime bits, vtable 0x00831824), a frame count (0x00831808), or a notify: on
// `wait_object` (0x0083181c), else on entity handle `object` (a stale handle ends the thread) with a timeout
// (ScrWaitTimeoutRecord_Init), plain (0x00831800) or with argument words (Scr_InitWaitRecord4) - then switches
// back to the scheduler (Scr_RestoreContext).
[[noreturn]] void XB_CDECL Scr_WaitRecord15(uint32_t seconds_bits);
[[noreturn]] void XB_CDECL Scr_WaitRecord16(uint32_t frames);
[[noreturn]] void XB_CDECL Scr_WaitRecord17(uint32_t object, uint32_t wait_object, uint32_t notify, uint32_t a,
                                            uint32_t b, uint32_t c, uint32_t flag, uint32_t timeout_bits);

}  // namespace game

// 0x0011d950 / 0x0011d990 / 0x0011d9d0 (no arguments; asm): the current script thread waits on the time, frame
// count or notify in the title's wait words (0x00922124, 0x00922128, 0x00922130..), returning when it resumes.
extern "C" void Scr_SuspendWaitTime(void);
extern "C" void Scr_SuspendWaitFrames(void);
extern "C" void Scr_SuspendWaitTill(void);
// 0x001fa400 (context object in ECX; asm): runs the new script thread's context (vtable slot 1) inside catch (...).
extern "C" void Scr_ThreadEntry(void);
// 0x002f5660 (asm, never returns): resumes the context at `context` (the scheduler's: 0x0092709c).
extern "C" [[noreturn]] void XB_CDECL Scr_RestoreContext(uint32_t context);

// 0x002fe100 (thread in EAX, scheduler in ESI; Scr_RunThreads x3): runs a new thread from its start
// (Scr_ThreadEntry 0x001fa400 with ECX = its context object +0xc) or resumes a woken one (+0x10 bits 1 and
// 2) until it waits or ends (+0x10 |= 8 when it ends); g_scrThreadAborting (block +0x24) = bit 6 first.
extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_ResumeThread(uint8_t* thread);
extern "C" void Scr_ResumeThread_entry(void);
