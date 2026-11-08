#pragma once

// Helpers shared by the push-c levelscript files (bro_scr_ai_look, bro_scr_ai_moves, ...): the
// compiled scripts' inlined key-table access and interface-block calls. Not title functions.

#include <cstdint>
#include <cstring>

#include "compiler.h"
#include "script_string.h"
#include "title_data.h"

namespace game::bro_ai {

template <typename T>
inline T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
template <typename T>
inline T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
inline uint32_t bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
// A float word read in place (no float return: i386 returns floats on the x87, which would quiet
// a signalling NaN).
inline const float& as_float(const uint32_t& word) { return *reinterpret_cast<const float*>(&word); }

// An engine call through the interface block (pScr_* slot).
template <typename F>
inline F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

inline uint8_t* anim_object() { return title_global<uint8_t*>(0x00922efc); }  // the anim scripts' object
inline uint32_t undefined_int() { return title_global<uint32_t>(0x00922ec0); }       // g_scrUndefinedInt
inline uint32_t undefined_handle() { return title_global<uint32_t>(0x00922eb4); }    // g_scrUndefinedHandle
inline uint32_t undefined_float() { return title_global<uint32_t>(0x00922eac); }     // g_scrUndefinedFloat
inline uint32_t undefined_lane() { return title_global<uint32_t>(0x00922eb8); }      // g_scrUndefinedVecLane

inline uint32_t entity_handle_is_valid(uint32_t handle) {
    return slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(handle);  // Scr_EntityHandleIsValid
}
inline int32_t get_time() { return slot<int32_t XB_CDECL()>(0x009223c4)(); }  // GScr_GetTime
inline int32_t random_int(int32_t max) { return slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(max); }
inline void queue_notify(uint32_t ent, uint32_t hash) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, hash);  // Scr_QueueNotify
}

// The entity's script key table (ScrCall_GScr_GetEntScriptObject), the shared empty one without.
inline ScriptKeyTable* ent_script_object(uint32_t ent) {
    ScriptKeyTable* table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    return table ? table : pointer<ScriptKeyTable>(0x00922e94);
}

// The value under `key` in the entity's table, added as `missing` first (ScriptKeyTable_Set).
inline uint32_t* key_slot(uint32_t ent, uint32_t key, uint32_t missing) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) value = ScriptKeyTable_Set(table, key, missing);
    return value;
}
inline uint32_t* int_slot(uint32_t ent, uint32_t key) { return key_slot(ent, key, undefined_int()); }
inline uint32_t* handle_slot(uint32_t ent, uint32_t key) { return key_slot(ent, key, undefined_handle()); }

// An entity-handle value, added as 0 (ScriptKeyTable_SetValue).
inline uint32_t* ent_slot(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = ent_script_object(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, key)) return value;
    const uint32_t none = 0;
    using SetValue = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*);
    return title_function<SetValue>(0x00458e10)(table, key, &none);  // ScriptKeyTable_SetValue
}

// A script float stored as an int: the undefined int for the undefined float, else truncated
// (CVTTSS2SI; NaN gives 0x80000000).
inline uint32_t float_to_script_int(float value) {
    if (value == title_global<float>(0x00922eac)) return undefined_int();
    return static_cast<uint32_t>(static_cast<int32_t>(value));
}

}  // namespace game::bro_ai
