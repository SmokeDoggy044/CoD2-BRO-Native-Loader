#include "scr_unwind_guard.h"

#include <cstdio>

namespace game {

namespace {
constexpr uint32_t kExceptionUnwinding = 0x02;
constexpr uint32_t kContinueSearch = 1;
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
}  // namespace

uint32_t XB_CDECL scr_guard_handler(const uint32_t* exception_record, ScrGuardRecord* frame, void*, void*) {
    if (exception_record[1] & kExceptionUnwinding)
        for (uint32_t e = 0; e < frame->count; ++e)
            for (uint32_t i = 0; i < frame->entries[e].count; ++i) scr_drop(&frame->entries[e].first[i]);
    return kContinueSearch;
}

void scr_guard_link(ScrGuardRecord* record) {
    record->handler = address_of(reinterpret_cast<const void*>(&scr_guard_handler));
    record->next = platform_seh_head();
    platform_set_seh_head(address_of(record));
}

void scr_guard_unlink(ScrGuardRecord* record) {
    if (platform_seh_head() == address_of(record)) {
        platform_set_seh_head(record->next);
        return;
    }
    // Not the newest record: a frame below it left its own linked (a bug there); leave the chain as it is.
    static bool reported = false;
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "scr_guard: record %08x is not the chain head %08x on exit\n", address_of(record),
                     platform_seh_head());
    }
}

}  // namespace game
