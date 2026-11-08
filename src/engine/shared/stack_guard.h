#pragma once

#include <cstdint>

// Debug-build check of C frames that sit below title code (gap entries: title_call_at* and the
// other helpers that call game_floor_enter/leave run a title callee at the original's position
// while the caller's C frame stays live below it). BRO's script threads run on the same stack and
// are saved/restored by copying (Scr_RestoreThreadStack), so such a frame is at risk in two ways:
// - "live across a script thread switch": entered inside a script thread and left after a thread
//   restore happened in between - the thread waited inside the call while its C frame, outside the
//   copied range, stayed in memory others reuse (the Actor_InitStateMachine crash, 11-04);
// - "restore over a live C frame": a thread restore whose destination overlaps a live C frame.
// It also reports each new lowest margin between title code (a restore's destination or a title
// callee's position) and an outer C frame entered outside any script thread (VM_Call's, around the
// whole game frame). Reports go to stderr once per C caller; tools/stack_guard_report.py resolves
// the addresses. Release builds (NDEBUG) compile it to nothing.
namespace game {

void stack_guard_enter(uint32_t floor);
void stack_guard_leave(uint32_t floor);
void stack_guard_restore(uint32_t dst, uint32_t size);

}  // namespace game
