#include "functions.h"

#include "script_string.h"

namespace game {

namespace {

template <typename F>
const void* code(F* function) {
    return reinterpret_cast<const void*>(function);
}

// Sorted by campaign address.
const Function kFunctions[] = {
    {"ScriptString_Copy", 0x0011d5e0, 0, code(&ScriptString_Copy), 4, Result::Eax, Status::Compare},
    {"ScriptString_Equals", 0x0011d7c0, 0, code(&ScriptString_Equals), 8, Result::Al, Status::Compare},
    {"ScriptKeyTable_Find", 0x0011db70, 0, code(&ScriptKeyTable_Find), 4, Result::Eax, Status::Compare},
};

}  // namespace

std::span<const Function> functions() { return kFunctions; }

}  // namespace game
