#pragma once

// Page-level virtual memory: reserve address space, then commit, decommit and
// protect pages inside it. mmap/mprotect on POSIX, VirtualAlloc on Windows.

#include <cstddef>
#include <cstdint>

namespace platform::vmem {

constexpr size_t kPageSize = 4096;

enum class Protection { None, ReadOnly, ReadWrite, ReadExecute, ReadWriteExecute };

// Reserves `size` bytes of address space with no access, at exactly `address`, or
// anywhere if `address` is 0. Returns null if that fails (e.g. the range is in use).
void* reserve(uintptr_t address, size_t size);

// Makes pages of a reservation accessible. Pages that were not committed before
// read as zero.
bool commit(void* address, size_t size, Protection protection);

// Returns pages to the reserved state: no access, contents discarded.
bool decommit(void* address, size_t size);

bool protect(void* address, size_t size, Protection protection);

// Frees a whole reservation (as returned by reserve).
bool release(void* address, size_t size);

}  // namespace platform::vmem
