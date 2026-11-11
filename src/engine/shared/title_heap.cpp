#include "title_heap.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "title_crt.h"
#include "title_data.h"
#include "title_xapi.h"

namespace game {

namespace {

// Chunks: a 16-byte header, then the block. Header words: the previous chunk's size (only meaningful while that
// chunk is free), this chunk's size | kInUse | kPrevInUse, the size asked for, kMagic while in use. Sizes are
// multiples of 16 (32 at least: a free chunk keeps its list links in the block). Each segment ends in a 16-byte
// in-use sentinel of size 0, so coalescing never runs off a segment.
constexpr uint32_t kHeader = 16, kMinChunk = 32, kInUse = 1, kPrevInUse = 2, kMagic = 0x50414548;  // "HEAP"
constexpr uint32_t kSegment = 0x100000;  // 1 MB of virtual memory at a time
constexpr uint32_t kMaxRequest = 0x7fffffe0;
constexpr uint32_t kExactBins = 128;     // sizes < 2048: one bin per 16 bytes
constexpr uint32_t kBins = kExactBins + 21;

uint32_t& word(uint32_t address) { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); }
uint32_t& prev_size(uint32_t c) { return word(c); }
uint32_t& head(uint32_t c) { return word(c + 4); }
uint32_t& asked(uint32_t c) { return word(c + 8); }
uint32_t& magic(uint32_t c) { return word(c + 12); }
uint32_t& next_link(uint32_t c) { return word(c + 16); }
uint32_t& prev_link(uint32_t c) { return word(c + 20); }
uint32_t chunk_size(uint32_t c) { return head(c) & ~15u; }

uint32_t bin_of(uint32_t size) {
    if (size < kExactBins * 16) return size >> 4;
    uint32_t log = 0;
    while ((size >> log) > 1) ++log;
    const uint32_t bin = kExactBins + (log - 11);
    return bin < kBins ? bin : kBins - 1;
}

struct Heap {
    std::mutex lock;
    uint32_t bins[kBins] = {};
    uint32_t in_use = 0, blocks = 0, reserved = 0;
    uint32_t process_heap = 0;  // the handle RtlCreateHeap gives out (a zeroed block)

    void unlink(uint32_t c) {
        const uint32_t n = next_link(c), p = prev_link(c);
        if (p) next_link(p) = n;
        else bins[bin_of(chunk_size(c))] = n;
        if (n) prev_link(n) = p;
    }
    // A free chunk into its bin (at the front), its size into the next chunk's header.
    void insert(uint32_t c) {
        const uint32_t size = chunk_size(c), bin = bin_of(size);
        magic(c) = 0;
        next_link(c) = bins[bin];
        prev_link(c) = 0;
        if (bins[bin]) prev_link(bins[bin]) = c;
        bins[bin] = c;
        prev_size(c + size) = size;
        head(c + size) &= ~kPrevInUse;
    }
    bool grow(uint32_t need) {
        const uint32_t size = (need + kHeader + kSegment - 1) / kSegment * kSegment;
        // NtAllocateVirtualMemory (kernel thunk slot 0x007b314c), reserved and committed - where the title's Rtl
        // heap took its memory too, so the contiguous memory the engine allocates keeps its addresses.
        uint32_t base = 0, region = size;
        using Allocate = int32_t XB_STDCALL(uint32_t*, uint32_t, uint32_t*, uint32_t, uint32_t);
        if (title_function<Allocate>(title_global<uint32_t>(0x007b314c))(&base, 0, &region, 0x3000, 4) < 0 || !base)
            return false;  // MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
        reserved += size;
        const uint32_t end = base + size - kHeader;
        head(base) = (size - kHeader) | kPrevInUse;
        head(end) = kInUse;
        magic(end) = kMagic;
        insert(base);
        return true;
    }
    uint32_t find(uint32_t need) {
        for (uint32_t bin = bin_of(need); bin < kBins; ++bin)
            for (uint32_t c = bins[bin]; c; c = next_link(c))
                if (chunk_size(c) >= need) return c;
        return 0;
    }
    // Marks `c` (unlinked) in use with `need` bytes; the rest, when big enough, goes back as a free chunk.
    void take(uint32_t c, uint32_t need) {
        const uint32_t size = chunk_size(c);
        uint32_t keep = size;
        if (size - need >= kMinChunk) {
            keep = need;
            head(c + need) = (size - need) | kPrevInUse;
            insert(c + need);
        } else {
            head(c + size) |= kPrevInUse;
        }
        head(c) = keep | kInUse | (head(c) & kPrevInUse);
        magic(c) = kMagic;
    }
    static uint32_t chunk_for(uint32_t size) { return size <= kMinChunk - kHeader ? kMinChunk : (size + kHeader + 15) & ~15u; }

    uint32_t alloc(uint32_t size, bool zero) {
        if (size > kMaxRequest) return 0;
        const uint32_t need = chunk_for(size);
        uint32_t c = find(need);
        if (!c) {
            if (!grow(need)) return 0;
            c = find(need);
        }
        unlink(c);
        take(c, need);
        asked(c) = size;
        in_use += size;
        ++blocks;
        if (zero) std::memset(reinterpret_cast<void*>(uintptr_t{c + kHeader}), 0, size);
        return c + kHeader;
    }
    static bool ours(uint32_t block) {
        if (!block || (block & 15)) return false;
        const uint32_t c = block - kHeader;
        return magic(c) == kMagic && (head(c) & kInUse) && asked(c) <= chunk_size(c) - kHeader;
    }
    // Frees chunk `c` and merges it with free neighbours.
    void release(uint32_t c) {
        uint32_t size = chunk_size(c);
        head(c) &= ~kInUse;
        if (!(head(c) & kPrevInUse)) {
            const uint32_t p = c - prev_size(c);
            unlink(p);
            size += chunk_size(p);
            c = p;
        }
        const uint32_t n = c + size;
        if (!(head(n) & kInUse)) {
            unlink(n);
            size += chunk_size(n);
        }
        head(c) = size | (head(c) & kPrevInUse);
        insert(c);
    }
    bool free(uint32_t block) {
        if (!ours(block)) return false;
        const uint32_t c = block - kHeader;
        in_use -= asked(c);
        --blocks;
        release(c);
        return true;
    }
    // In place when the chunk (with a free chunk after it) holds the new size; else a new block unless
    // HEAP_REALLOC_IN_PLACE_ONLY (0x10). HEAP_ZERO_MEMORY (8) zeroes the added bytes.
    uint32_t realloc(uint32_t block, uint32_t size, uint32_t flags) {
        if (!ours(block) || size > kMaxRequest) return 0;
        const uint32_t c = block - kHeader, old = asked(c), need = chunk_for(size);
        uint32_t have = chunk_size(c);
        const uint32_t n = c + have;
        if (need > have && !(head(n) & kInUse) && have + chunk_size(n) >= need) {
            unlink(n);
            have += chunk_size(n);
            head(c) = have | kInUse | (head(c) & kPrevInUse);
            head(c + have) |= kPrevInUse;
        }
        if (need <= have) {
            if (have - need >= kMinChunk) {  // the tail back, merged with what follows
                head(c) = need | kInUse | (head(c) & kPrevInUse);
                head(c + need) = (have - need) | kInUse | kPrevInUse;
                release(c + need);
            }
            asked(c) = size;
            in_use += size - old;
            if ((flags & 8) && size > old) std::memset(reinterpret_cast<void*>(uintptr_t{block + old}), 0, size - old);
            return block;
        }
        if (flags & 0x10) return 0;
        const uint32_t moved = alloc(size, false);
        if (!moved) return 0;
        std::memcpy(reinterpret_cast<void*>(uintptr_t{moved}), reinterpret_cast<const void*>(uintptr_t{block}), old);
        if (flags & 8) std::memset(reinterpret_cast<void*>(uintptr_t{moved + old}), 0, size - old);
        free(block);
        return moved;
    }
};

Heap& heap() {
    static Heap instance;
    return instance;
}

void report_bad_block(const char* what, uint32_t block) {
    static uint32_t reported = 0;
    if (reported++ < 10) std::fprintf(stderr, "title heap: %s of %08x, not a block of ours (ignored)\n", what, block);
}

uint32_t process_heap() { return title_global<uint32_t>(0x0098f660); }
uint32_t active_heap() { return title_global<uint32_t>(0x0098f8c0); }  // __active_heap (1 = the system heap)
uint32_t new_mode() { return title_global<uint32_t>(0x00926d10); }     // _newmode
// _callnewh (0x001f699a, still the title's): only reached when memory runs out.
bool call_new_handler(uint32_t size) { return title_function<uint32_t XB_CDECL(uint32_t)>(0x001f699a)(size) != 0; }

}  // namespace

uint32_t title_heap_alloc(uint32_t size, bool zero) {
    Heap& h = heap();
    std::lock_guard<std::mutex> guard(h.lock);
    return h.alloc(size, zero);
}

void title_heap_free(uint32_t block) {
    if (!block) return;
    Heap& h = heap();
    std::lock_guard<std::mutex> guard(h.lock);
    if (!h.free(block)) report_bad_block("free", block);
}

uint32_t title_heap_size(uint32_t block) {
    Heap& h = heap();
    std::lock_guard<std::mutex> guard(h.lock);
    return Heap::ours(block) ? asked(block - kHeader) : 0xffffffff;
}

void title_heap_stats(uint32_t& in_use, uint32_t& blocks, uint32_t& reserved) {
    Heap& h = heap();
    std::lock_guard<std::mutex> guard(h.lock);
    in_use = h.in_use, blocks = h.blocks, reserved = h.reserved;
}

// ---- XAPILIB ----

uint32_t XB_STDCALL RtlAllocateHeap(uint32_t, uint32_t flags, uint32_t size) { return title_heap_alloc(size, flags & 8); }

uint8_t XB_STDCALL RtlFreeHeap(uint32_t, uint32_t, uint32_t block) {
    if (!block) return 1;
    Heap& h = heap();
    std::lock_guard<std::mutex> guard(h.lock);
    if (h.free(block)) return 1;
    report_bad_block("RtlFreeHeap", block);
    return 0;
}

uint32_t XB_STDCALL RtlReAllocateHeap(uint32_t, uint32_t flags, uint32_t block, uint32_t size) {
    if (!block) return 0;
    Heap& h = heap();
    std::lock_guard<std::mutex> guard(h.lock);
    const uint32_t result = h.realloc(block, size, flags);
    if (!result && !Heap::ours(block)) report_bad_block("RtlReAllocateHeap", block);
    return result;
}

uint32_t XB_STDCALL RtlSizeHeap(uint32_t, uint32_t, uint32_t block) { return title_heap_size(block); }

// A heap handle is a zeroed block of the size of the title's HEAP header (nothing reads it; real memory in case).
uint32_t XB_STDCALL RtlCreateHeap(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    return title_heap_alloc(0x5c0, true);
}

// Blocks of a destroyed heap stay allocated (only Xbox Live code, dropped, makes its own heap).
uint32_t XB_STDCALL RtlDestroyHeap(uint32_t heap_handle) {
    title_heap_free(heap_handle);
    return 0;
}

uint32_t XB_STDCALL GetProcessHeap() { return process_heap(); }

uint32_t XB_STDCALL HeapCreate(uint32_t options, uint32_t initial, uint32_t maximum) {
    const uint32_t handle = RtlCreateHeap(options, 0, maximum, initial, 0, 0);
    if (!handle) SetLastError(8);  // ERROR_NOT_ENOUGH_MEMORY
    return handle;
}

uint32_t XB_STDCALL HeapDestroy(uint32_t heap_handle) {
    if (!RtlDestroyHeap(heap_handle)) return 1;
    SetLastError(6);  // ERROR_INVALID_HANDLE
    return 0;
}

uint32_t XB_STDCALL HeapFree(uint32_t heap_handle, uint32_t flags, uint32_t block) {
    return RtlFreeHeap(heap_handle, flags, block);
}

// LMEM_ZEROINIT (0x40) -> HEAP_ZERO_MEMORY.
uint32_t XB_STDCALL LocalAlloc(uint32_t flags, uint32_t size) {
    return RtlAllocateHeap(process_heap(), (flags >> 3) & 8, size);
}

uint32_t XB_STDCALL LocalFree(uint32_t block) { return RtlFreeHeap(process_heap(), 0, block) ? 0 : block; }

// Attributes: bit 31 physical memory (alignment nibble at bit 24: 1 = 4 KB pages below 16 MB, else 1 << n,
// 0 -> 4 KB; protection from the title's table at 0x008cfe60 by bits 28-29; bit 30 zeroed), else the process
// heap (bit 30 zeroed).
uint32_t XB_STDCALL XMemAlloc(uint32_t size, uint32_t attributes) {
    if (!(attributes & 0x80000000u)) return LocalAlloc((attributes >> 24) & 0x40, size);
    if (!(attributes & 0x0f000000u)) attributes = (attributes & 0xfcffffffu) | 0x0c000000u;
    const uint32_t alignment = (attributes >> 24) & 0xf;
    const uint32_t protect = title_global<uint32_t>(0x008cfe60 + 4 * ((attributes >> 28) & 3));
    const uint32_t block = alignment == 1 ? XPhysicalAllocEx(size, 0, 0xffffff, 0x1000, protect)
                                          : XPhysicalAlloc(size, 0xffffffff, 1u << alignment, protect);
    if (block && (attributes & 0x40000000u)) std::memset(reinterpret_cast<void*>(uintptr_t{block}), 0, size);
    return block;
}

void XB_STDCALL XMemFree(uint32_t block, uint32_t attributes) {
    if (attributes & 0x80000000u) XPhysicalFree(block);
    else LocalFree(block);
}

// ---- LIBCMT ----
// The small-block heap (__active_heap 3) is not ported; its paths take the system heap's way.

uint32_t XB_CDECL title_crt_heap_alloc(uint32_t size) {
    if (!size) size = 1;
    if (active_heap() != 1) size = (size + 15) & ~15u;
    return RtlAllocateHeap(process_heap(), 0, size);
}

uint32_t XB_CDECL title_crt_nh_malloc(uint32_t size, uint32_t new_handler) {
    if (size > 0xffffffe0u) return 0;
    for (;;) {
        const uint32_t block = title_crt_heap_alloc(size);
        if (block || !new_handler) return block;
        if (!call_new_handler(size)) return 0;
    }
}

uint32_t XB_CDECL title_crt_malloc(uint32_t size) { return title_crt_nh_malloc(size, new_mode()); }

void XB_CDECL title_crt_free(uint32_t block) {
    if (block) HeapFree(process_heap(), 0, block);
}

uint32_t XB_CDECL title_crt_calloc(uint32_t count, uint32_t size) {
    uint32_t total = count * size;
    if (!total) total = 1;
    for (;;) {
        uint32_t block = 0;
        if (total <= 0xffffffe0u) {
            if (active_heap() == 3) total = (total + 15) & ~15u;
            block = RtlAllocateHeap(process_heap(), 8, total);
            if (block) return block;
        }
        if (!new_mode() || !call_new_handler(total)) return block;
    }
}

uint32_t XB_CDECL title_crt_realloc(uint32_t block, uint32_t size) {
    if (!block) return title_crt_malloc(size);
    if (!size) {
        title_crt_free(block);
        return 0;
    }
    for (;;) {
        uint32_t moved = 0;
        if (size <= 0xffffffe0u) moved = RtlReAllocateHeap(process_heap(), 0, block, size);
        if (moved || !new_mode()) return moved;
        if (!call_new_handler(size)) return 0;
    }
}

uint32_t XB_CDECL title_crt_msize(uint32_t block) { return RtlSizeHeap(process_heap(), 0, block); }

// The block from malloc is stored in the word before the returned address (rounded down to 4 bytes).
uint32_t XB_CDECL title_crt_aligned_offset_malloc(uint32_t size, uint32_t alignment, uint32_t offset) {
    if (alignment & (alignment - 1)) {
        *title_errno() = 22;  // EINVAL
        return 0;
    }
    if (offset >= size && offset) {
        *title_errno() = 22;
        return 0;
    }
    const uint32_t mask = (alignment > 4 ? alignment : 4) - 1, gap = (0u - offset) & 3;
    const uint32_t block = title_crt_malloc(size + gap + mask + 4);
    if (!block) return 0;
    const uint32_t result = ((block + gap + mask + 4 + offset) & ~mask) - offset;
    word(result - gap - 4) = block;
    return result;
}

uint32_t XB_CDECL title_crt_aligned_malloc(uint32_t size, uint32_t alignment) {
    return title_crt_aligned_offset_malloc(size, alignment, 0);
}

void XB_CDECL title_crt_aligned_free(uint32_t block) {
    if (block) title_crt_free(word((block & ~3u) - 4));
}

}  // namespace game
