#pragma once

// Page-level virtual memory: reserve address space, then commit, decommit and
// protect pages inside it. mmap/mprotect on POSIX, VirtualAlloc on Windows.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

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

// One run of pages with the same state, for a picture of the process's address space (--mem-stats).
struct Region {
    enum class State { Free, Reserved, Committed };
    enum class Kind { Private, Mapped, Image };  // anonymous memory, a mapped file/section, a loaded module
    uintptr_t base = 0;
    uint64_t size = 0;
    State state = State::Free;
    Kind kind = Kind::Private;
    uintptr_t allocation_base = 0;  // the reservation it belongs to (Windows); POSIX: base
    std::string name;               // module or file, when known
};

// Calls `visit` for every region of the address space below 4 GB, in address order, free gaps included.
// Not for the game thread: on POSIX it reads /proc/self/maps.
void walk(const std::function<void(const Region&)>& visit);

}  // namespace platform::vmem
