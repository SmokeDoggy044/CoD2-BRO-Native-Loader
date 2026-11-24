#pragma once

// The game's stacks (title threads, script fibers) at the same addresses in every run. The original game reads
// stale stack words in places (a field written as 16 bits keeps the upper half the stack held); when such a word
// holds a stack address, a stack the OS placed elsewhere each run (address randomization) made the game state differ
// from run to run (mission 3: an actor's path points, 11-22). The Xbox's stacks sat at fixed places.
//
// One range is reserved at a preferred address (taken when free) and handed out in order; a released stack goes
// back to a list per size and the next request of that size takes the last one released. Requests in the same order
// get the same addresses. Without room (range taken or full) a stack goes where the OS puts it (logged once).

#include <cstddef>
#include <cstdint>

namespace platform::stack_arena {

// Address space for one stack (reserved, not committed: commit with vmem::commit). nullptr if there is none at all.
void* reserve(size_t bytes);
// Gives a stack from reserve() back (decommitted; reused by the next request of the same size).
void release(void* address, size_t bytes);
// Whether the address lies in the arena (replay hashes treat such values as pointers).
bool contains(uint32_t address);

}  // namespace platform::stack_arena
