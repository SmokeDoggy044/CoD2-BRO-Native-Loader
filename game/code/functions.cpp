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
    {"ScriptStringRep_Init", 0x0011d540, 0, code(&ScriptStringRep_Init), 12, Result::Eax, Status::Compare},
    {"ScriptStringRep_Append", 0x0011d580, 0, code(&ScriptStringRep_Append), 8, Result::None, Status::Compare},
    {"ScriptString_FromRep", 0x0011d5d0, 0, code(&ScriptString_FromRep), 4, Result::Eax, Status::Compare},
    {"ScriptString_Copy", 0x0011d5e0, 0, code(&ScriptString_Copy), 4, Result::Eax, Status::Compare},
    {"ScriptString_Find", 0x0011d6a0, 0, code(&ScriptString_Find), 8, Result::Eax, Status::Compare},
    {"ScriptString_FindChar", 0x0011d700, 0, code(&ScriptString_FindChar), 8, Result::Eax, Status::Compare},
    {"ScriptString_FindLast", 0x0011d730, 0, code(&ScriptString_FindLast), 4, Result::Eax, Status::Compare},
    {"ScriptString_SetChar", 0x0011d7a0, 0, code(&ScriptString_SetChar), 8, Result::None, Status::Compare},
    {"ScriptString_Equals", 0x0011d7c0, 0, code(&ScriptString_Equals), 8, Result::Al, Status::Compare},
    {"ScriptString_EqualsString", 0x0011d820, 0, code(&ScriptString_EqualsString), 8, Result::Al, Status::Compare},
    {"ScriptKeyTable_Find", 0x0011db70, 0, code(&ScriptKeyTable_Find), 4, Result::Eax, Status::Compare},
};

}  // namespace

std::span<const Function> functions() { return kFunctions; }

}  // namespace game
