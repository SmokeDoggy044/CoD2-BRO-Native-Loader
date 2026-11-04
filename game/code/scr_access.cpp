#include "scr_access.h"

#include "title_data.h"

namespace game {

namespace {

// The field hook (0x008d20e8): node, field offset, value address.
using FieldHook = uint32_t XB_CDECL(uint8_t*, uint32_t, void*);

FieldHook* field_hook() { return title_global<FieldHook*>(0x008d20e8); }
FieldHook* hook_at(uint32_t address) { return title_global<FieldHook*>(address); }

void string_assign(void* dst, const void* src) {
    title_function<void* XB_THISCALL(void*, const void*)>(0x0011d600)(dst, src);  // ScriptString_Assign
}

void string_release(void* string) {
    title_function<void XB_THISCALL(void*)>(0x0011d640)(string);  // ScriptString_Release
}

// A path node by handle as the accessors look it up: block (handle >> 9) - 1 of g_pathNodeBlocks
// (+8), node handle & 0x1ff (0x80 bytes each, from the block's +0x10); null when invalid.
uint8_t* path_node(uint32_t handle) {
    const uint16_t low = static_cast<uint16_t>(handle);
    if (low == 0 || low == 0xffff) return nullptr;
    const uint8_t* blocks = title_global<const uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    const uint16_t block_index = static_cast<uint16_t>((low >> 9) - 1);
    const uint8_t* block = *reinterpret_cast<const uint8_t* const*>(blocks + 8 + block_index * 4);
    if (!block) return nullptr;
    uint8_t* node = *reinterpret_cast<uint8_t* const*>(block + 0x10) + (handle & 0x1ff) * 0x80;
    if (!node || handle == 0xffffffffu) return nullptr;
    return node;
}

template <uint32_t Offset, uint32_t Hook = 0x008d20e8>
void set_node_string(uint32_t handle, ScriptString* value) {
    if (uint8_t* node = path_node(handle)) {
        if (FieldHook* hook = hook_at(Hook)) {
            hook(node, Offset, value);
        } else {
            string_assign(node + Offset, value);
        }
    }
    string_release(value);
}

// With the hook the handle's argument slot holds the value it fills in.
template <uint32_t Offset, uint32_t Hook = 0x008d20e8>
ScriptString* get_node_string(ScriptString* out, uint32_t* handle) {
    uint8_t* node = path_node(*handle);
    if (!node) return ScriptString_FromRep(out, nullptr);
    if (FieldHook* hook = hook_at(Hook)) {
        ScriptString* value = reinterpret_cast<ScriptString*>(handle);
        ScriptString_FromRep(value, nullptr);
        hook(node, Offset, value);
        ScriptString_Copy(out, value);
        string_release(value);
        return out;
    }
    return ScriptString_Copy(out, reinterpret_cast<ScriptString*>(node + Offset));
}

// The default vector the getters hand out for an invalid handle (0x00922eb8).
void default_vector(uint32_t* out) {
    const uint32_t value = title_global<uint32_t>(0x00922eb8);
    out[0] = value;
    out[1] = value;
    out[2] = value;
}

void copy_vector(uint32_t* out, const uint32_t* in) {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

template <uint32_t Offset, uint32_t Hook = 0x008d20e8>
void set_node_vector(uint32_t handle, uint32_t* value) {
    uint8_t* node = path_node(handle);
    if (!node) return;
    if (FieldHook* hook = hook_at(Hook)) {
        hook(node, Offset, value);
        return;
    }
    copy_vector(reinterpret_cast<uint32_t*>(node + Offset), value);
}

template <uint32_t Offset, uint32_t Hook = 0x008d20e8>
uint32_t* get_node_vector(uint32_t* out, uint32_t handle) {
    uint8_t* node = path_node(handle);
    if (!node) {
        default_vector(out);
        return out;
    }
    if (FieldHook* hook = hook_at(Hook)) {
        uint32_t value[3];
        default_vector(value);
        hook(node, Offset, value);
        copy_vector(out, value);
        return out;
    }
    copy_vector(out, reinterpret_cast<const uint32_t*>(node + Offset));
    return out;
}

// Vehicle nodes: g_vehNodes (0x0095c228), one pointer per handle (read before the -1 check);
// no hook.
uint8_t* veh_node(uint32_t handle) {
    uint8_t* node = reinterpret_cast<uint8_t* const*>(uintptr_t{0x0095c228})[handle];
    if (!node || handle == 0xffffffffu) return nullptr;
    return node;
}

template <uint32_t Offset>
void set_veh_string(uint32_t handle, ScriptString* value) {
    if (uint8_t* node = veh_node(handle)) string_assign(node + Offset, value);
    string_release(value);
}

template <uint32_t Offset>
ScriptString* get_veh_string(ScriptString* out, uint32_t handle) {
    if (uint8_t* node = veh_node(handle)) return ScriptString_Copy(out, reinterpret_cast<ScriptString*>(node + Offset));
    return ScriptString_FromRep(out, nullptr);
}

template <uint32_t Offset>
void set_veh_vector(uint32_t handle, const uint32_t* value) {
    if (uint8_t* node = veh_node(handle)) copy_vector(reinterpret_cast<uint32_t*>(node + Offset), value);
}

template <uint32_t Offset>
uint32_t* get_veh_vector(uint32_t* out, uint32_t handle) {
    if (uint8_t* node = veh_node(handle))
        copy_vector(out, reinterpret_cast<const uint32_t*>(node + Offset));
    else
        default_vector(out);
    return out;
}

template <uint32_t Offset>
void set_veh_float(uint32_t handle, uint32_t bits) {
    if (uint8_t* node = veh_node(handle)) *reinterpret_cast<uint32_t*>(node + Offset) = bits;
}

template <uint32_t Offset>
float get_veh_float(uint32_t handle) {
    if (uint8_t* node = veh_node(handle)) return *reinterpret_cast<const float*>(node + Offset);
    return title_global<float>(0x0083eafc);  // 0
}

// Entities by handle: slot handle & 0xfff (< 0x540) of the handle table (0x0097a968: entity,
// generation) whose generation is handle >> 12. Setters also refuse handle 0. Each field has
// its own hook address (0x008d20e8 for most).
uint8_t* entity(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x0097a968 + index * 8);
}

uint8_t* entity_to_set(uint32_t handle) {
    uint8_t* ent = entity(handle);
    return ent && handle ? ent : nullptr;
}


template <uint32_t Offset, uint32_t Hook>
void set_ent_string(uint32_t handle, ScriptString* value) {
    if (uint8_t* ent = entity_to_set(handle)) {
        if (FieldHook* hook = hook_at(Hook))
            hook(ent, Offset, value);
        else
            string_assign(ent + Offset, value);
    }
    string_release(value);
}

template <uint32_t Offset, uint32_t Hook>
ScriptString* get_ent_string(ScriptString* out, uint32_t* handle) {
    uint8_t* ent = entity(*handle);
    if (!ent) return ScriptString_FromRep(out, nullptr);
    if (FieldHook* hook = hook_at(Hook)) {
        ScriptString* value = reinterpret_cast<ScriptString*>(handle);  // the handle's argument slot
        ScriptString_FromRep(value, nullptr);
        hook(ent, Offset, value);
        ScriptString_Copy(out, value);
        string_release(value);
        return out;
    }
    return ScriptString_Copy(out, reinterpret_cast<ScriptString*>(ent + Offset));
}

// Words copied as bits: one (int, float) or three (vector).
template <uint32_t Offset, uint32_t Hook, uint32_t Count>
void set_ent_word(uint32_t handle, uint32_t* value) {
    uint8_t* ent = entity_to_set(handle);
    if (!ent) return;
    if (FieldHook* hook = hook_at(Hook)) {
        hook(ent, Offset, value);
        return;
    }
    for (uint32_t i = 0; i < Count; ++i) reinterpret_cast<uint32_t*>(ent + Offset)[i] = value[i];
}

template <uint32_t Offset, uint32_t Hook>
uint32_t* get_ent_vector(uint32_t* out, uint32_t handle) {
    uint8_t* ent = entity(handle);
    if (!ent) {
        default_vector(out);
        return out;
    }
    if (FieldHook* hook = hook_at(Hook)) {
        uint32_t value[3];
        default_vector(value);
        hook(ent, Offset, value);
        copy_vector(out, value);
        return out;
    }
    copy_vector(out, reinterpret_cast<const uint32_t*>(ent + Offset));
    return out;
}

// With the hook the handle's argument slot holds the value it fills in.
template <uint32_t Offset, uint32_t Hook>
uint32_t get_ent_int(uint32_t handle) {
    uint8_t* ent = entity(handle);
    if (!ent) return 0;
    if (FieldHook* hook = hook_at(Hook)) {
        hook(ent, Offset, &handle);
        return handle;
    }
    return *reinterpret_cast<const uint32_t*>(ent + Offset);
}

template <uint32_t Offset, uint32_t Hook>
float get_ent_float(uint32_t handle) {
    uint8_t* ent = entity(handle);
    if (!ent) return title_global<float>(0x0083eafc);  // 0
    if (FieldHook* hook = hook_at(Hook)) {
        hook(ent, Offset, &handle);
        return *reinterpret_cast<const float*>(&handle);
    }
    return *reinterpret_cast<const float*>(ent + Offset);
}

}  // namespace

void XB_CDECL ScrPathNode_SetTargetname(uint32_t handle, ScriptString value) { set_node_string<0x30>(handle, &value); }
void XB_CDECL ScrPathNode_SetTarget(uint32_t handle, ScriptString value) { set_node_string<0x38>(handle, &value); }
void XB_CDECL ScrPathNode_SetAnimscript(uint32_t handle, ScriptString value) { set_node_string<0x3c>(handle, &value); }
void XB_CDECL ScrPathNode_SetScriptNoteworthy(uint32_t handle, ScriptString value) {
    set_node_string<0x34>(handle, &value);
}
void XB_CDECL ScrPathNode_SetType(uint32_t handle, ScriptString value) { set_node_string<0x28, 0x008d216c>(handle, &value); }

ScriptString* XB_CDECL ScrPathNode_GetTargetname(ScriptString* out, uint32_t handle) {
    return get_node_string<0x30>(out, &handle);
}
ScriptString* XB_CDECL ScrPathNode_GetTarget(ScriptString* out, uint32_t handle) {
    return get_node_string<0x38>(out, &handle);
}
ScriptString* XB_CDECL ScrPathNode_GetAnimscript(ScriptString* out, uint32_t handle) {
    return get_node_string<0x3c>(out, &handle);
}
ScriptString* XB_CDECL ScrPathNode_GetScriptNoteworthy(ScriptString* out, uint32_t handle) {
    return get_node_string<0x34>(out, &handle);
}
ScriptString* XB_CDECL ScrPathNode_GetType(ScriptString* out, uint32_t handle) {
    return get_node_string<0x28, 0x008d2170>(out, &handle);
}

void XB_CDECL ScrPathNode_SetOrigin(uint32_t handle, ScrVec3 value) { set_node_vector<0x44>(handle, value.v); }
uint32_t* XB_CDECL ScrPathNode_GetOrigin(uint32_t* out, uint32_t handle) { return get_node_vector<0x44>(out, handle); }
void XB_CDECL ScrPathNode_SetAngles(uint32_t handle, ScrVec3 value) { set_node_vector<0x50, 0x008d2164>(handle, value.v); }
uint32_t* XB_CDECL ScrPathNode_GetAngles(uint32_t* out, uint32_t handle) { return get_node_vector<0x50, 0x008d2168>(out, handle); }

void XB_CDECL ScrPathNode_SetRadius(uint32_t handle, uint32_t bits) {
    uint8_t* node = path_node(handle);
    if (!node) return;
    if (FieldHook* hook = field_hook()) {
        hook(node, 0x54, &bits);
        return;
    }
    *reinterpret_cast<uint32_t*>(node + 0x54) = bits;
}

float XB_CDECL ScrPathNode_GetRadius(uint32_t handle) {
    uint8_t* node = path_node(handle);
    if (!node) return title_global<float>(0x0083eafc);  // 0
    if (FieldHook* hook = field_hook()) {
        hook(node, 0x54, &handle);  // filled in over the handle's argument slot
        return *reinterpret_cast<const float*>(&handle);
    }
    return *reinterpret_cast<const float*>(node + 0x54);
}

void XB_CDECL ScrPathNode_SetSpawnflags(uint32_t handle, uint32_t flags) {
    uint8_t* node = path_node(handle);
    if (!node) return;
    if (FieldHook* hook = field_hook()) {
        hook(node, 0x2c, &flags);
        return;
    }
    *reinterpret_cast<uint16_t*>(node + 0x2c) = static_cast<uint16_t>(flags);
}

uint32_t XB_REGS1 XB_CDECL ScrPathNode_GetSpawnflags(uint32_t eax, uint32_t handle) {
    // AX is the result; the upper half is what EAX held: the caller's (handle 0 or 0xffff), the
    // node's address, or the hook's result. The lookup's zero cases leave 0.
    const uint16_t low = static_cast<uint16_t>(handle);
    if (low == 0 || low == 0xffff) return eax & 0xffff0000u;
    uint8_t* node = path_node(handle);
    if (!node) return 0;
    if (FieldHook* hook = field_hook()) {
        const uint32_t result = hook(node, 0x2c, &handle);
        return (result & 0xffff0000u) | (handle & 0xffff);
    }
    return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)) & 0xffff0000u) |
           *reinterpret_cast<const uint16_t*>(node + 0x2c);
}

void XB_CDECL ScrVehNode_SetTargetname(uint32_t handle, ScriptString value) { set_veh_string<0x0>(handle, &value); }
ScriptString* XB_CDECL ScrVehNode_GetTargetname(ScriptString* out, uint32_t handle) {
    return get_veh_string<0x0>(out, handle);
}
void XB_CDECL ScrVehNode_SetTarget(uint32_t handle, ScriptString value) { set_veh_string<0x4>(handle, &value); }
ScriptString* XB_CDECL ScrVehNode_GetTarget(ScriptString* out, uint32_t handle) { return get_veh_string<0x4>(out, handle); }
void XB_CDECL ScrVehNode_SetScriptNoteworthy(uint32_t handle, ScriptString value) {
    set_veh_string<0x10>(handle, &value);
}
ScriptString* XB_CDECL ScrVehNode_GetScriptNoteworthy(ScriptString* out, uint32_t handle) {
    return get_veh_string<0x10>(out, handle);
}
void XB_CDECL ScrVehNode_SetOrigin(uint32_t handle, ScrVec3 value) { set_veh_vector<0x14>(handle, value.v); }
uint32_t* XB_CDECL ScrVehNode_GetOrigin(uint32_t* out, uint32_t handle) { return get_veh_vector<0x14>(out, handle); }
void XB_CDECL ScrVehNode_SetAngles(uint32_t handle, ScrVec3 value) { set_veh_vector<0x2c>(handle, value.v); }
uint32_t* XB_CDECL ScrVehNode_GetAngles(uint32_t* out, uint32_t handle) { return get_veh_vector<0x2c>(out, handle); }
void XB_CDECL ScrVehNode_SetSpeed(uint32_t handle, uint32_t bits) { set_veh_float<0x8>(handle, bits); }
float XB_CDECL ScrVehNode_GetSpeed(uint32_t handle) { return get_veh_float<0x8>(handle); }
void XB_CDECL ScrVehNode_SetLookahead(uint32_t handle, uint32_t bits) { set_veh_float<0xc>(handle, bits); }
float XB_CDECL ScrVehNode_GetLookahead(uint32_t handle) { return get_veh_float<0xc>(handle); }

void XB_CDECL ScrEnt_SetClassname(uint32_t handle, ScriptString value) { set_ent_string<0x25c, 0x008d20ec>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetClassname(ScriptString* out, uint32_t handle) { return get_ent_string<0x25c, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetOrigin(uint32_t handle, ScrVec3 value) { set_ent_word<0x140, 0x008d20f0, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrEnt_GetOrigin(uint32_t* out, uint32_t handle) { return get_ent_vector<0x140, 0x008d20e8>(out, handle); }
void XB_CDECL ScrEnt_SetModel(uint32_t handle, ScriptString value) { set_ent_string<0x258, 0x008d20ec>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetModel(ScriptString* out, uint32_t handle) { return get_ent_string<0x258, 0x008d20fc>(out, &handle); }
void XB_CDECL ScrEnt_SetSpawnflags(uint32_t handle, uint32_t value) { set_ent_word<0x2a0, 0x008d20ec, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetSpawnflags(uint32_t handle) { return get_ent_int<0x2a0, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetClosespeed(uint32_t handle, uint32_t bits) { set_ent_word<0x300, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrEnt_GetClosespeed(uint32_t handle) { return get_ent_float<0x300, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetTarget(uint32_t handle, ScriptString value) { set_ent_string<0x26c, 0x008d2108>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetTarget(ScriptString* out, uint32_t handle) { return get_ent_string<0x26c, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetTargetname(uint32_t handle, ScriptString value) { set_ent_string<0x264, 0x008d210c>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetTargetname(ScriptString* out, uint32_t handle) { return get_ent_string<0x264, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetTeamname(uint32_t handle, ScriptString value) { set_ent_string<0x2f8, 0x008d20e8>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetTeamname(ScriptString* out, uint32_t handle) { return get_ent_string<0x2f8, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetWait(uint32_t handle, uint32_t bits) { set_ent_word<0x384, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrEnt_GetWait(uint32_t handle) { return get_ent_float<0x384, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetRandom(uint32_t handle, uint32_t bits) { set_ent_word<0x388, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrEnt_GetRandom(uint32_t handle) { return get_ent_float<0x388, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetHealth(uint32_t handle, uint32_t value) { set_ent_word<0x350, 0x008d20f4, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetHealth(uint32_t handle) { return get_ent_int<0x350, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetDmg(uint32_t handle, uint32_t value) { set_ent_word<0x358, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetDmg(uint32_t handle) { return get_ent_int<0x358, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetAngles(uint32_t handle, ScrVec3 value) { set_ent_word<0x150, 0x008d20f8, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrEnt_GetAngles(uint32_t* out, uint32_t handle) { return get_ent_vector<0x150, 0x008d20e8>(out, handle); }
void XB_CDECL ScrEnt_SetRotate(uint32_t handle, ScrVec3 value) { set_ent_word<0x390, 0x008d20e8, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrEnt_GetRotate(uint32_t* out, uint32_t handle) { return get_ent_vector<0x390, 0x008d20e8>(out, handle); }
void XB_CDECL ScrEnt_SetDegrees(uint32_t handle, uint32_t bits) { set_ent_word<0x2f4, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrEnt_GetDegrees(uint32_t handle) { return get_ent_float<0x2f4, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetKey(uint32_t handle, uint32_t value) { set_ent_word<0x3b4, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetKey(uint32_t handle) { return get_ent_int<0x3b4, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetDelay(uint32_t handle, uint32_t bits) { set_ent_word<0x38c, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrEnt_GetDelay(uint32_t handle) { return get_ent_float<0x38c, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetSpawnitem(uint32_t handle, ScriptString value) { set_ent_string<0x3b8, 0x008d20e8>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetSpawnitem(ScriptString* out, uint32_t handle) { return get_ent_string<0x3b8, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetGroupname(uint32_t handle, ScriptString value) { set_ent_string<0x274, 0x008d2110>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetGroupname(ScriptString* out, uint32_t handle) { return get_ent_string<0x274, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetScriptNoteworthy(uint32_t handle, ScriptString value) { set_ent_string<0x27c, 0x008d2114>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetScriptNoteworthy(ScriptString* out, uint32_t handle) { return get_ent_string<0x27c, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetMaxhealth(uint32_t handle, uint32_t value) { set_ent_word<0x354, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetMaxhealth(uint32_t handle) { return get_ent_int<0x354, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetAnimname(uint32_t handle, ScriptString value) { set_ent_string<0x284, 0x008d2118>(handle, &value); }
ScriptString* XB_CDECL ScrEnt_GetAnimname(ScriptString* out, uint32_t handle) { return get_ent_string<0x284, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrEnt_SetPersistentIndex(uint32_t handle, uint32_t value) { set_ent_word<0x290, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetPersistentIndex(uint32_t handle) { return get_ent_int<0x290, 0x008d20e8>(handle); }
void XB_CDECL ScrEnt_SetTakedamage(uint32_t handle, uint32_t value) { set_ent_word<0x29c, 0x008d2100, 1>(handle, &value); }
uint32_t XB_CDECL ScrEnt_GetTakedamage(uint32_t handle) { return get_ent_int<0x29c, 0x008d2104>(handle); }

}  // namespace game
