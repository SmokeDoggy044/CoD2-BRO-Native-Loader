#include "stack_guard.h"

#ifndef NDEBUG
#include <cstdio>
#include <set>
#include <utility>

#include "title_data.h"
#endif

namespace game {

#ifdef NDEBUG

void stack_guard_enter(uint32_t) {}
void stack_guard_leave(uint32_t) {}
void stack_guard_restore(uint32_t, uint32_t) {}

#else

namespace {

constexpr uint32_t kCurrentThread = 0x008dda2c;  // the running script thread, 0 in the scheduler
constexpr uint32_t kRestoreCount = 0x0098ad18;   // ++ in every Scr_RestoreThreadStack

// One live title_call_at* (or helper) call. The helpers all start `push ebp; mov ebp,esp; push esi;
// push edi; push ebx; push esp; call game_floor_enter`, so floor = ebp - 12: [floor+12] is the C
// caller's EBP, [floor+16] the return address into the C caller, [floor+20] `top` (the title
// callee's position), [floor+24] the title callee.
struct Entry {
    uint32_t floor, ctop, ret, top, function, thread, restores;
    uint32_t chain[4];  // return addresses further up the C frame chain
};

constexpr int kMax = 256;
PLATFORM_TLS_FIXED thread_local Entry t_live[kMax];
PLATFORM_TLS_FIXED thread_local int t_depth;
uint32_t g_lowest_margin = 0xffffffffu;

uint32_t word(uint32_t address) { return *reinterpret_cast<const uint32_t*>(uintptr_t{address}); }

// The C frames' top: the debug build keeps frame pointers, so follow the EBP chain up from the C caller
// while it stays below the title callee's position minus a page (above that is the gap and title code).
uint32_t c_frames_top(uint32_t floor, uint32_t top, uint32_t* chain) {
    uint32_t ctop = floor + 32, frame = word(floor + 12);
    // Overflow-free bounds (an asm entry's EBP can hold any value, 0xfffffffx included) and word aligned.
    for (int i = 0; i < 64 && frame > floor && frame < top && top - frame > 0x1000 && !(frame & 3); ++i) {
        ctop = frame + 8;
        if (i < 4) chain[i] = word(frame + 4);
        frame = word(frame);
    }
    return ctop;
}

void report(const char* what, const Entry& e, uint32_t extra) {
    static std::set<std::pair<int, uint32_t>> told;
    const int kind = what[0];
    if (!told.insert({kind, e.ret}).second) return;
    std::fprintf(stderr,
                 "[stack_guard] %s: C caller ret %08x calling title %08x at %08x, C frames %08x..%08x, thread %08x,"
                 " detail %08x, chain %08x,%08x,%08x,%08x (marker stack_guard_enter %08x)\n",
                 what, e.ret, e.function, e.top, e.floor, e.ctop, e.thread, extra, e.chain[0], e.chain[1], e.chain[2],
                 e.chain[3],
                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&stack_guard_enter)));
}

void note_margin(const Entry& outer, uint32_t lowest_title_address) {
    if (outer.thread || lowest_title_address <= outer.ctop) return;
    const uint32_t margin = lowest_title_address - outer.ctop;
    if (margin >= g_lowest_margin) return;
    g_lowest_margin = margin;
    std::fprintf(stderr, "[stack_guard] new lowest margin %08x: title at %08x above the C frames of ret %08x (title %08x)"
                 " (marker stack_guard_enter %08x)\n", margin, lowest_title_address, outer.ret, outer.function,
                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&stack_guard_enter)));
}

}  // namespace

void stack_guard_enter(uint32_t floor) {
    Entry e{};
    e.floor = floor;
    e.ret = word(floor + 16);
    e.top = word(floor + 20);
    e.function = word(floor + 24);
    e.ctop = c_frames_top(floor, e.top, e.chain);
    e.thread = title_global<uint32_t>(kCurrentThread);
    e.restores = title_global<uint32_t>(kRestoreCount);
    for (int i = 0; i < t_depth; ++i) {
        const Entry& outer = t_live[i];
        if (e.top > outer.floor && e.top <= outer.ctop) report("title callee placed on a live C frame", outer, e.top);
        note_margin(outer, e.top);
    }
    if (t_depth < kMax) t_live[t_depth++] = e;
}

void stack_guard_leave(uint32_t floor) {
    int i = t_depth - 1;
    while (i >= 0 && t_live[i].floor != floor) --i;
    if (i < 0) return;
    const Entry e = t_live[i];
    if (e.thread && e.restores != title_global<uint32_t>(kRestoreCount))
        report("live across a script thread switch", e, title_global<uint32_t>(kRestoreCount) - e.restores);
    for (int j = i; j + 1 < t_depth; ++j) t_live[j] = t_live[j + 1];
    --t_depth;
    // The popped slot is dead (only t_live[0..t_depth) is ever read; enter overwrites the whole slot), but its
    // words are C frame addresses and host return addresses: left there, a compare test of a caller of a gap
    // entry saw them differ by its C frame depth in host TLS (docs/comparison-harness.md, Stack guard table).
    t_live[t_depth] = Entry{};
}

void stack_guard_restore(uint32_t dst, uint32_t size) {
    for (int i = 0; i < t_depth; ++i) {
        const Entry& e = t_live[i];
        if (dst < e.ctop && dst + size > e.floor) report("restore over a live C frame", e, dst);
        note_margin(e, dst);
    }
}

#endif

}  // namespace game
