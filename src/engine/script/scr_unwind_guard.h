#pragma once

// Script strings held by our C across a script wait, released when the script thread is ended during that wait.
//
// An ended thread throws from its wait (g_scr_runtime.cpp wait_done) and lands in Scr_ThreadEntry's catch (...); the
// unwind on the way (title_RtlUnwind) calls the handler of every record on the FS:[0] chain newer than the catch. The
// title's own frames are on that chain (their FuncInfo unwind maps release their locals, like the original's
// `lea ecx, [ebp-x]; jmp ScriptString_Release` funclets); our C frames are not, so without this their locals were
// never released: a 32-byte block of the small heap 0x0095f4a4 per ended thread, until "Broc Pool empty" (issue #32).
//
// ScrStringGuard links a record into the chain for its scope: on an unwind it releases its strings; on a normal
// exit its destructor unlinks it. Guarded strings start empty (`ScriptString x{};`) and are released with scr_drop,
// which also empties them, so the unwind never releases one twice.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// ScriptString_Release, then the string is empty (the release keeps the pointer, as the original does).
inline uint32_t scr_drop(ScriptString* string) {
    ScriptStringRep* const rep = ScriptString_Release(string);
    string->rep = nullptr;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rep));
}

struct ScrGuardEntry {
    ScriptString* first;
    uint32_t count;
};

// The record's layout starts like EXCEPTION_REGISTRATION_RECORD {next, handler}; the handler finds the entries
// behind it.
struct ScrGuardRecord {
    uint32_t next;
    uint32_t handler;
    const ScrGuardEntry* entries;
    uint32_t count;
};

// The handler (cdecl, an SEH handler's arguments): on the unwinding pass, every guarded string released and
// emptied; always "continue search".
uint32_t XB_CDECL scr_guard_handler(const uint32_t* exception_record, ScrGuardRecord* frame, void* context,
                                    void* dispatcher_context);
void scr_guard_link(ScrGuardRecord* record);
void scr_guard_unlink(ScrGuardRecord* record);

template <size_t N>
class ScrStringGuard {
public:
    template <typename... T>
    explicit ScrStringGuard(T&&... strings) : entries_{entry(strings)...} {
        record_.entries = entries_;
        record_.count = N;
        scr_guard_link(&record_);
    }
    ~ScrStringGuard() { scr_guard_unlink(&record_); }
    ScrStringGuard(const ScrStringGuard&) = delete;
    ScrStringGuard& operator=(const ScrStringGuard&) = delete;

private:
    static ScrGuardEntry entry(ScriptString* string) { return {string, 1}; }
    template <size_t M>
    static ScrGuardEntry entry(ScriptString (&strings)[M]) {
        return {strings, static_cast<uint32_t>(M)};
    }
    ScrGuardRecord record_{};
    ScrGuardEntry entries_[N];
};
template <typename... T>
ScrStringGuard(T&&...) -> ScrStringGuard<sizeof...(T)>;

}  // namespace game
