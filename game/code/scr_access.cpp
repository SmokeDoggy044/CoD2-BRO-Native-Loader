#include "scr_access.h"

#include "g_scr_runtime.h"
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

// Actors (entity +0x240) and sentients (entity +0x244) by entity handle: the entity lookup
// above, then the pointer, both must be set; no handle-0 check in the setters. The hook gets the
// actor or sentient.
template <uint32_t Sub>
uint8_t* sub_object(uint32_t handle) {
    uint8_t* ent = entity(handle);
    return ent ? *reinterpret_cast<uint8_t**>(ent + Sub) : nullptr;
}

template <uint32_t Sub, uint32_t Offset, uint32_t Hook, uint32_t Count>
void set_sub_word(uint32_t handle, uint32_t* value) {
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) return;
    if (FieldHook* hook = hook_at(Hook)) {
        hook(object, Offset, value);
        return;
    }
    for (uint32_t i = 0; i < Count; ++i) reinterpret_cast<uint32_t*>(object + Offset)[i] = value[i];
}

template <typename T, uint32_t Sub, uint32_t Offset, uint32_t Hook>
void set_sub_narrow(uint32_t handle, uint32_t* value) {
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) return;
    if (FieldHook* hook = hook_at(Hook)) {
        hook(object, Offset, value);
        return;
    }
    *reinterpret_cast<T*>(object + Offset) = static_cast<T>(*value);
}

template <uint32_t Sub, uint32_t Offset, uint32_t Hook>
void set_sub_string(uint32_t handle, ScriptString* value) {
    if (uint8_t* object = sub_object<Sub>(handle)) {
        if (FieldHook* hook = hook_at(Hook))
            hook(object, Offset, value);
        else
            string_assign(object + Offset, value);
    }
    string_release(value);
}

template <uint32_t Sub, uint32_t Offset, uint32_t Hook>
ScriptString* get_sub_string(ScriptString* out, uint32_t* handle) {
    uint8_t* object = sub_object<Sub>(*handle);
    if (!object) return ScriptString_FromRep(out, nullptr);
    if (FieldHook* hook = hook_at(Hook)) {
        ScriptString* value = reinterpret_cast<ScriptString*>(handle);  // the handle's argument slot
        ScriptString_FromRep(value, nullptr);
        hook(object, Offset, value);
        ScriptString_Copy(out, value);
        string_release(value);
        return out;
    }
    return ScriptString_Copy(out, reinterpret_cast<ScriptString*>(object + Offset));
}

template <uint32_t Sub, uint32_t Offset, uint32_t Hook>
uint32_t* get_sub_vector(uint32_t* out, uint32_t handle) {
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) {
        default_vector(out);
        return out;
    }
    if (FieldHook* hook = hook_at(Hook)) {
        uint32_t value[3];
        default_vector(value);
        hook(object, Offset, value);
        copy_vector(out, value);
        return out;
    }
    copy_vector(out, reinterpret_cast<const uint32_t*>(object + Offset));
    return out;
}

// With the hook the handle's argument slot holds the value it fills in.
template <uint32_t Sub, uint32_t Offset, uint32_t Hook>
uint32_t get_sub_int(uint32_t handle) {
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) return 0;
    if (FieldHook* hook = hook_at(Hook)) {
        hook(object, Offset, &handle);
        return handle;
    }
    return *reinterpret_cast<const uint32_t*>(object + Offset);
}

template <uint32_t Sub, uint32_t Offset, uint32_t Hook>
float get_sub_float(uint32_t handle) {
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) return title_global<float>(0x0083eafc);  // 0
    if (FieldHook* hook = hook_at(Hook)) {
        hook(object, Offset, &handle);
        return *reinterpret_cast<const float*>(&handle);
    }
    return *reinterpret_cast<const float*>(object + Offset);
}

// A reference (entity/node handle) through the result pointer; the hook fills the handle's slot,
// cleared first. 0 for an invalid handle.
template <uint32_t Sub, uint32_t Offset, uint32_t Hook>
uint32_t* get_sub_ref(uint32_t* out, uint32_t handle) {
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) {
        *out = 0;
        return out;
    }
    if (FieldHook* hook = hook_at(Hook)) {
        handle = 0;
        hook(object, Offset, &handle);
        *out = handle;
        return out;
    }
    *out = *reinterpret_cast<const uint32_t*>(object + Offset);
    return out;
}

// A 16- or 8-bit field returned in AX / AL. The rest of EAX is what the original leaves there:
// the slot index when the index or generation check fails (only bits above AL survive), 0 for a
// missing entity or object, the object's address when read, the hook's result with the hook.
template <typename T, uint32_t Sub, uint32_t Offset, uint32_t Hook>
uint32_t get_sub_narrow(uint32_t handle) {
    constexpr uint32_t keep = sizeof(T) == 2 ? 0xffff0000u : 0xffffff00u;
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return index & keep;
    uint8_t* object = sub_object<Sub>(handle);
    if (!object) return 0;
    if (FieldHook* hook = hook_at(Hook)) {
        const uint32_t result = hook(object, Offset, &handle);
        return (result & keep) | (handle & ~keep);
    }
    return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)) & keep) |
           *reinterpret_cast<const T*>(object + Offset);
}

// The ScrCall_* wrappers: thiscall on a handle object (its handle at +0), calling the accessor
// through its pointer in the builtin table (0x009229xx-0x00922bxx). Their C++ exception frames
// around the temporaries have no effect.
template <uint32_t Slot>
ScriptString* call_get_string(const uint32_t* self, ScriptString* out) {
    using Get = ScriptString* XB_CDECL(ScriptString*, uint32_t);
    ScriptString value;
    const ScriptString* result = title_global<Get*>(Slot)(&value, *self);
    ScriptString_Copy(out, result);
    string_release(&value);
    return out;
}

template <uint32_t Slot>
uint32_t* call_get_vector(const uint32_t* self, uint32_t* out) {
    using Get = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t value[3];
    copy_vector(out, title_global<Get*>(Slot)(value, *self));
    return out;
}

template <uint32_t Slot>
const ScrVec3* call_set_vector(const uint32_t* self, const ScrVec3* value) {
    title_global<void XB_CDECL (*)(uint32_t, ScrVec3)>(Slot)(*self, *value);
    return value;
}

// The callee takes over the new reference.
template <uint32_t Slot>
const ScriptString* call_set_string(const uint32_t* self, const ScriptString* value) {
    ScriptString copy;
    ScriptString_Copy(&copy, value);
    title_global<void XB_CDECL (*)(uint32_t, ScriptString)>(Slot)(*self, copy);
    return value;
}

template <uint32_t Slot>
const uint32_t* call_set_word(const uint32_t* self, const uint32_t* value) {
    title_global<void XB_CDECL (*)(uint32_t, uint32_t)>(Slot)(*self, *value);
    return value;
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

void XB_CDECL ScrActor_SetFloat_d4(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0xd4, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_d4(uint32_t handle) { return get_sub_float<0x240, 0xd4, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_d8(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0xd8, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_d8(uint32_t handle) { return get_sub_float<0x240, 0xd8, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetVec_108(uint32_t handle, ScrVec3 value) { set_sub_word<0x240, 0x108, 0x008d211c, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrActor_GetVec_108(uint32_t* out, uint32_t handle) { return get_sub_vector<0x240, 0x108, 0x008d20e8>(out, handle); }
void XB_CDECL ScrActor_SetVec_114(uint32_t handle, ScrVec3 value) { set_sub_word<0x240, 0x114, 0x008d211c, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrActor_GetVec_114(uint32_t* out, uint32_t handle) { return get_sub_vector<0x240, 0x114, 0x008d20e8>(out, handle); }
void XB_CDECL ScrActor_SetVec_120(uint32_t handle, ScrVec3 value) { set_sub_word<0x240, 0x120, 0x008d211c, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrActor_GetVec_120(uint32_t* out, uint32_t handle) { return get_sub_vector<0x240, 0x120, 0x008d20e8>(out, handle); }
void XB_CDECL ScrActor_SetFloat_828(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x828, 0x008d2120, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_828(uint32_t handle) { return get_sub_float<0x240, 0x828, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_82c(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x82c, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_82c(uint32_t handle) { return get_sub_float<0x240, 0x82c, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_824(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x824, 0x008d2120, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_824(uint32_t handle) { return get_sub_float<0x240, 0x824, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_830(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x830, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_830(uint32_t handle) { return get_sub_int<0x240, 0x830, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_7b0(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x7b0, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_7b0(uint32_t handle) { return get_sub_int<0x240, 0x7b0, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_7b4(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x7b4, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_7b4(uint32_t handle) { return get_sub_int<0x240, 0x7b4, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetShort_7dc(uint32_t handle, uint32_t value) { set_sub_narrow<uint16_t, 0x240, 0x7dc, 0x008d20e8>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetShort_7dc(uint32_t handle) { return get_sub_narrow<uint16_t, 0x240, 0x7dc, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_7b8(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x7b8, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_7b8(uint32_t handle) { return get_sub_float<0x240, 0x7b8, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_7bc(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x7bc, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_7bc(uint32_t handle) { return get_sub_float<0x240, 0x7bc, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_228(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x228, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_228(uint32_t handle) { return get_sub_int<0x240, 0x228, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_22c(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x22c, 0x008d211c, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_22c(uint32_t handle) { return get_sub_int<0x240, 0x22c, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetVec_234(uint32_t handle, ScrVec3 value) { set_sub_word<0x240, 0x234, 0x008d211c, 3>(handle, value.v); }
uint32_t* XB_CDECL ScrActor_GetVec_234(uint32_t* out, uint32_t handle) { return get_sub_vector<0x240, 0x234, 0x008d20e8>(out, handle); }
void XB_CDECL ScrActor_SetInt_230(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x230, 0x008d211c, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_230(uint32_t handle) { return get_sub_int<0x240, 0x230, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_240(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x240, 0x008d211c, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_240(uint32_t handle) { return get_sub_int<0x240, 0x240, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_194(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x194, 0x008d211c, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_194(uint32_t handle) { return get_sub_int<0x240, 0x194, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_7a4(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x7a4, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_7a4(uint32_t handle) { return get_sub_float<0x240, 0x7a4, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_154(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x154, 0x008d211c, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_154(uint32_t handle) { return get_sub_float<0x240, 0x154, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_7e4(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x7e4, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_7e4(uint32_t handle) { return get_sub_float<0x240, 0x7e4, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_7e8(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x7e8, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_7e8(uint32_t handle) { return get_sub_int<0x240, 0x7e8, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_7ec(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x7ec, 0x008d2124, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_7ec(uint32_t handle) { return get_sub_int<0x240, 0x7ec, 0x008d2128>(handle); }
void XB_CDECL ScrActor_SetInt_1308(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x1308, 0x008d2124, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_1308(uint32_t handle) { return get_sub_int<0x240, 0x1308, 0x008d2128>(handle); }
void XB_CDECL ScrActor_SetString_ec(uint32_t handle, ScriptString value) { set_sub_string<0x240, 0xec, 0x008d20e8>(handle, &value); }
ScriptString* XB_CDECL ScrActor_GetString_ec(ScriptString* out, uint32_t handle) { return get_sub_string<0x240, 0xec, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrActor_SetInt_f0(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0xf0, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_f0(uint32_t handle) { return get_sub_int<0x240, 0xf0, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_f4(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0xf4, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_f4(uint32_t handle) { return get_sub_int<0x240, 0xf4, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetString_f8(uint32_t handle, ScriptString value) { set_sub_string<0x240, 0xf8, 0x008d20e8>(handle, &value); }
ScriptString* XB_CDECL ScrActor_GetString_f8(ScriptString* out, uint32_t handle) { return get_sub_string<0x240, 0xf8, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrActor_SetInt_7d8(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x7d8, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_7d8(uint32_t handle) { return get_sub_int<0x240, 0x7d8, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetFloat_130c(uint32_t handle, uint32_t bits) { set_sub_word<0x240, 0x130c, 0x008d2120, 1>(handle, &bits); }
float XB_CDECL ScrActor_GetFloat_130c(uint32_t handle) { return get_sub_float<0x240, 0x130c, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetRef_1310(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x1310, 0x008d211c, 1>(handle, &value); }
uint32_t* XB_CDECL ScrActor_GetRef_1310(uint32_t* out, uint32_t handle) { return get_sub_ref<0x240, 0x1310, 0x008d20e8>(out, handle); }
void XB_CDECL ScrActor_SetInt_131c(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x131c, 0x008d212c, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_131c(uint32_t handle) { return get_sub_int<0x240, 0x131c, 0x008d2130>(handle); }
void XB_CDECL ScrActor_SetInt_1328(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x1328, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_1328(uint32_t handle) { return get_sub_int<0x240, 0x1328, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetRef_12a0(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x12a0, 0x008d213c, 1>(handle, &value); }
uint32_t* XB_CDECL ScrActor_GetRef_12a0(uint32_t* out, uint32_t handle) { return get_sub_ref<0x240, 0x12a0, 0x008d2140>(out, handle); }
void XB_CDECL ScrActor_SetInt_cc(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0xcc, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_cc(uint32_t handle) { return get_sub_int<0x240, 0xcc, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetByte_1374(uint32_t handle, uint32_t value) { set_sub_narrow<uint8_t, 0x240, 0x1374, 0x008d20e8>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetByte_1374(uint32_t handle) { return get_sub_narrow<uint8_t, 0x240, 0x1374, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetByte_7de(uint32_t handle, uint32_t value) { set_sub_narrow<uint8_t, 0x240, 0x7de, 0x008d20e8>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetByte_7de(uint32_t handle) { return get_sub_narrow<uint8_t, 0x240, 0x7de, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_1378(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x1378, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_1378(uint32_t handle) { return get_sub_int<0x240, 0x1378, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetInt_137c(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x137c, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_137c(uint32_t handle) { return get_sub_int<0x240, 0x137c, 0x008d20e8>(handle); }
void XB_CDECL ScrActor_SetString_1384(uint32_t handle, ScriptString value) { set_sub_string<0x240, 0x1384, 0x008d211c>(handle, &value); }
ScriptString* XB_CDECL ScrActor_GetString_1384(ScriptString* out, uint32_t handle) { return get_sub_string<0x240, 0x1384, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrActor_SetString_1388(uint32_t handle, ScriptString value) { set_sub_string<0x240, 0x1388, 0x008d211c>(handle, &value); }
ScriptString* XB_CDECL ScrActor_GetString_1388(ScriptString* out, uint32_t handle) { return get_sub_string<0x240, 0x1388, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrActor_SetString_138c(uint32_t handle, ScriptString value) { set_sub_string<0x240, 0x138c, 0x008d211c>(handle, &value); }
ScriptString* XB_CDECL ScrActor_GetString_138c(ScriptString* out, uint32_t handle) { return get_sub_string<0x240, 0x138c, 0x008d20e8>(out, &handle); }
void XB_CDECL ScrActor_SetString_2c0(uint32_t handle, ScriptString value) { set_sub_string<0x240, 0x2c0, 0x008d211c>(handle, &value); }
ScriptString* XB_CDECL ScrActor_GetString_2c0(ScriptString* out, uint32_t handle) { return get_sub_string<0x240, 0x2c0, 0x008d2134>(out, &handle); }
void XB_CDECL ScrActor_SetInt_184(uint32_t handle, uint32_t value) { set_sub_word<0x240, 0x184, 0x008d2138, 1>(handle, &value); }
uint32_t XB_CDECL ScrActor_GetInt_184(uint32_t handle) { return get_sub_int<0x240, 0x184, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetTeam(uint32_t handle, ScriptString value) { set_sub_string<0x244, 0x4, 0x008d2148>(handle, &value); }
ScriptString* XB_CDECL ScrSentient_GetTeam(ScriptString* out, uint32_t handle) { return get_sub_string<0x244, 0x4, 0x008d214c>(out, &handle); }
void XB_CDECL ScrSentient_SetInt_28(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x28, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrSentient_GetInt_28(uint32_t handle) { return get_sub_int<0x244, 0x28, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetFloat_2c(uint32_t handle, uint32_t bits) { set_sub_word<0x244, 0x2c, 0x008d20e8, 1>(handle, &bits); }
float XB_CDECL ScrSentient_GetFloat_2c(uint32_t handle) { return get_sub_float<0x244, 0x2c, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetRef_48(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x48, 0x008d2144, 1>(handle, &value); }
uint32_t* XB_CDECL ScrSentient_GetRef_48(uint32_t* out, uint32_t handle) { return get_sub_ref<0x244, 0x48, 0x008d2158>(out, handle); }
void XB_CDECL ScrSentient_SetGoalNode(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x50, 0x008d2144, 1>(handle, &value); }
uint32_t* XB_CDECL ScrSentient_GetGoalNode(uint32_t* out, uint32_t handle) { return get_sub_ref<0x244, 0x50, 0x008d20e8>(out, handle); }
void XB_CDECL ScrSentient_SetFloat_18(uint32_t handle, uint32_t bits) { set_sub_word<0x244, 0x18, 0x008d2150, 1>(handle, &bits); }
float XB_CDECL ScrSentient_GetFloat_18(uint32_t handle) { return get_sub_float<0x244, 0x18, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetGoalArc(uint32_t handle, uint32_t bits) { set_sub_word<0x244, 0x24, 0x008d2154, 1>(handle, &bits); }
float XB_CDECL ScrSentient_GetGoalArc(uint32_t handle) { return get_sub_float<0x244, 0x24, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetRef_3c(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x3c, 0x008d2144, 1>(handle, &value); }
uint32_t* XB_CDECL ScrSentient_GetRef_3c(uint32_t* out, uint32_t handle) { return get_sub_ref<0x244, 0x3c, 0x008d2160>(out, handle); }
void XB_CDECL ScrSentient_SetInt_30(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x30, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrSentient_GetInt_30(uint32_t handle) { return get_sub_int<0x244, 0x30, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetInt_34(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x34, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrSentient_GetInt_34(uint32_t handle) { return get_sub_int<0x244, 0x34, 0x008d20e8>(handle); }
void XB_CDECL ScrSentient_SetInt_38(uint32_t handle, uint32_t value) { set_sub_word<0x244, 0x38, 0x008d20e8, 1>(handle, &value); }
uint32_t XB_CDECL ScrSentient_GetInt_38(uint32_t handle) { return get_sub_int<0x244, 0x38, 0x008d20e8>(handle); }

ScriptString* XB_THISCALL ScrCall_ScrPathNode_GetTarget(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922aa0>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrVehNode_GetTargetname(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922ae0>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrVehNode_GetTarget(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922ae8>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrVehNode_GetScriptNoteworthy(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922b10>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetClassname(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922b18>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetModel(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922b28>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetTarget(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922b48>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetTargetname(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922b50>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetGroupname(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922c18>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetScriptNoteworthy(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922c28>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrEnt_GetAnimname(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922c40>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrSentient_GetTeam(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922dc8>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrPathNode_GetScriptNoteworthy(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922ab0>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrActor_GetString_ec(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922d28>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrPathNode_GetTargetname(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922a98>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrActor_GetString_1384(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922da0>(self, out); }
ScriptString* XB_THISCALL ScrCall_ScrActor_GetString_2c0(const uint32_t* self, ScriptString* out) { return call_get_string<0x00922db8>(self, out); }
uint32_t* XB_THISCALL ScrCall_ScrPathNode_GetOrigin(const uint32_t* self, uint32_t* out) { return call_get_vector<0x00922ab8>(self, out); }
uint32_t* XB_THISCALL ScrCall_ScrPathNode_GetAngles(const uint32_t* self, uint32_t* out) { return call_get_vector<0x00922ac0>(self, out); }
uint32_t* XB_THISCALL ScrCall_ScrVehNode_GetOrigin(const uint32_t* self, uint32_t* out) { return call_get_vector<0x00922af0>(self, out); }
uint32_t* XB_THISCALL ScrCall_ScrEnt_GetOrigin(const uint32_t* self, uint32_t* out) { return call_get_vector<0x00922b20>(self, out); }
uint32_t* XB_THISCALL ScrCall_ScrEnt_GetAngles(const uint32_t* self, uint32_t* out) { return call_get_vector<0x00922ba0>(self, out); }
uint32_t* XB_THISCALL ScrCall_ScrVehNode_GetAngles(const uint32_t* self, uint32_t* out) { return call_get_vector<0x00922af8>(self, out); }
const ScrVec3* XB_THISCALL ScrCall_ScrEnt_SetOrigin(const uint32_t* self, const ScrVec3* value) { return call_set_vector<0x00922b24>(self, value); }
const ScrVec3* XB_THISCALL ScrCall_ScrEnt_SetAngles(const uint32_t* self, const ScrVec3* value) { return call_set_vector<0x00922ba4>(self, value); }
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetTargetname(const uint32_t* self, const ScriptString* value) { return call_set_string<0x00922b54>(self, value); }
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetScriptNoteworthy(const uint32_t* self, const ScriptString* value) { return call_set_string<0x00922c2c>(self, value); }
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetAnimname(const uint32_t* self, const ScriptString* value) { return call_set_string<0x00922c44>(self, value); }
const ScriptString* XB_THISCALL ScrCall_ScrActor_SetString_ec(const uint32_t* self, const ScriptString* value) { return call_set_string<0x00922d2c>(self, value); }
const ScriptString* XB_THISCALL ScrCall_ScrEnt_SetGroupname(const uint32_t* self, const ScriptString* value) { return call_set_string<0x00922c1c>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrEnt_SetInt_370(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922b8c>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrEnt_SetHealth(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922b94>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetFloat_7e4(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922d0c>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_7e8(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922d14>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_1308(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922d24>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_1328(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922d6c>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrSentient_SetFloat_18(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922df4>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_1378(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922d94>(self, value); }
const uint32_t* XB_THISCALL ScrCall_ScrActor_SetInt_184(const uint32_t* self, const uint32_t* value) { return call_set_word<0x00922dc4>(self, value); }

// ScrCall_* wrappers of builtins (cdecl): unwrap handle objects, add the builtin's fixed
// arguments and return its result (through `out` where the builtin returns a handle).
namespace {

template <typename F>
F* builtin(uint32_t slot) {
    return title_global<F*>(slot);
}

}  // namespace

uint32_t* XB_CDECL ScrCall_GScr_GetEntArray(uint32_t* out, uint32_t key, uint32_t value, uint32_t array) {
    *out = builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221c4)(key, value, 0, 0, array);
    return out;
}

uint32_t* XB_CDECL ScrCall_GScr_GetNode(uint32_t* out, uint32_t key, uint32_t value) {
    const uint32_t node = builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221cc)(key, value, 0, 0);
    *out = node;
    if (node == 0xffffffffu) *out = 0;
    return out;
}

uint32_t* XB_CDECL ScrCall_GScr_GetVehicleNode(uint32_t* out, uint32_t key, uint32_t value) {
    *out = builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221d0)(key, value, 0, 0);
    return out;
}

uint32_t* XB_CDECL ScrCall_GScr_SpawnVehicle(uint32_t* out, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                                             uint32_t f) {
    *out = builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009222fc)(a, b, c, d,
                                                                                                         e, f);
    return out;
}

uint32_t* XB_CDECL ScrCall_GScr_Spawn(uint32_t* out, uint32_t a, uint32_t b, uint32_t c) {
    *out = builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009222f0)(a, b, c);
    return out;
}

uint32_t XB_CDECL ScrCall_GScr_PlaySound(const uint32_t* ent, uint32_t sound) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221dc)(*ent, sound, 0, 0, 0);
}

// Tail jumps: the handles go into the wrapper's own argument slots, which the builtin then gets.
uint32_t XB_CDECL ScrCall_GScr_LinkTo(const uint32_t* ent, const uint32_t* parent) {
    const uint32_t parent_handle = *parent;
    const uint32_t ent_handle = *ent;
    *reinterpret_cast<uint32_t*>(&parent) = parent_handle;
    *reinterpret_cast<uint32_t*>(&ent) = ent_handle;
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(ent_handle, parent_handle);
}

uint32_t XB_CDECL ScrCall_GScr_IsTouching(const uint32_t* ent, const uint32_t* other) {
    const uint32_t other_handle = *other;
    const uint32_t ent_handle = *ent;
    *reinterpret_cast<uint32_t*>(&other) = other_handle;
    *reinterpret_cast<uint32_t*>(&ent) = ent_handle;
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922530)(ent_handle, other_handle);
}

uint32_t* XB_CDECL ScrCall_GScr_ForceSpawn(uint32_t* out, const uint32_t* spawner, uint32_t flag) {
    *out = builtin<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922520)(*spawner, flag);
    return out;
}

uint32_t* XB_CDECL ScrCall_GScr_UseBy(uint32_t* out, const uint32_t* ent, const uint32_t* user) {
    const uint32_t user_handle = *user;
    *out = builtin<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x0092252c)(*ent, user_handle);
    return out;
}

// x87 results passed on as they are.
long double XB_CDECL ScrCall_Scr_RandomFloatRange(uint32_t low, uint32_t high) {
    return builtin<long double XB_CDECL(uint32_t, uint32_t)>(0x00922238)(low, high);
}

long double XB_CDECL ScrCall_Scr_DistanceSquared(uint32_t a, uint32_t b) {
    return builtin<long double XB_CDECL(uint32_t, uint32_t)>(0x0092226c)(a, b);
}

uint32_t XB_CDECL ScrCall_GScr_ObjectiveAddLocalizedWithPosition(uint32_t index, uint32_t state, uint32_t text,
                                                                 uint32_t position) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t*)>(0x009222b4)(index, state, text, &position);
}

uint32_t XB_CDECL ScrCall_Scr_QueueNotify(const uint32_t* ent, const char* name) {
    uint32_t hash;
    const uint32_t* const key = Scr_HashName(&hash, name);
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(*ent, *key);
}

uint32_t XB_CDECL ScrCall_GScr_PlaySoundWithNotify(const uint32_t* ent, uint32_t sound, uint32_t notify) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x0092220c)(*ent, sound, notify, 0);
}

// SETNE AL over the builtin's EAX: the upper bytes stay the builtin's.
uint32_t XB_CDECL ScrCall_Scr_EntityHandleIsValid(const uint32_t* ent) {
    const uint32_t valid = builtin<uint32_t XB_CDECL(uint32_t)>(0x00922214)(*ent);
    return (valid & 0xffffff00u) | (valid != 0 ? 1u : 0u);
}

const uint32_t* XB_THISCALL ScrCall_ScrActor_SetRef_12a0(const uint32_t* self, const uint32_t* value) {
    return call_set_word<0x00922d74>(self, value);
}

namespace {

// builtin(handle, &vector, tag) into a default-filled local, copied out.
template <uint32_t Slot>
uint32_t* tag_vector(uint32_t* out, const uint32_t* ent, uint32_t tag) {
    uint32_t vec[3];
    default_vector(vec);
    builtin<uint32_t XB_CDECL(uint32_t, uint32_t*, uint32_t)>(Slot)(*ent, vec, tag);
    copy_vector(out, vec);
    return out;
}

// builtin(&vector, argument) into a default-filled local, copied out (not the returned pointer).
template <uint32_t Slot>
uint32_t* vector_of(uint32_t* out, uint32_t arg) {
    uint32_t vec[3];
    default_vector(vec);
    builtin<uint32_t XB_CDECL(uint32_t*, uint32_t)>(Slot)(vec, arg);
    copy_vector(out, vec);
    return out;
}

}  // namespace

uint32_t XB_CDECL ScrCall_GScr_SetFlaggedAnimKnobAll(const uint32_t* ent, const uint32_t* anim, uint32_t a, uint32_t b,
                                                     uint32_t c, uint32_t d, uint32_t e) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
        0x009225e4)(*ent, *anim, a, b, c, d, e, 1);
}

uint32_t* XB_CDECL ScrCall_GScr_GetTagOrigin(uint32_t* out, const uint32_t* ent, uint32_t tag) {
    return tag_vector<0x0092261c>(out, ent, tag);
}

uint32_t* XB_CDECL ScrCall_GScr_GetTagAngles(uint32_t* out, const uint32_t* ent, uint32_t tag) {
    return tag_vector<0x00922620>(out, ent, tag);
}

ScriptString* XB_CDECL ScrCall_GScr_GetLocalizedString(ScriptString* out, uint32_t key) {
    ScriptString value{nullptr};
    const ScriptString* result = builtin<ScriptString* XB_CDECL(ScriptString*, uint32_t)>(0x0092267c)(&value, key);
    ScriptString_Copy(out, result);
    string_release(&value);
    return out;
}

uint32_t* XB_CDECL ScrCall_Scr_VectorToAngles(uint32_t* out, uint32_t vec) { return vector_of<0x00922284>(out, vec); }

uint32_t* XB_CDECL ScrCall_Scr_AnglesToForward(uint32_t* out, uint32_t angles) {
    return vector_of<0x00922290>(out, angles);
}

// Copies the local the builtin filled, not the pointer it returns.
ScriptString* XB_CDECL ScrCall_GScr_GetCvar(ScriptString* out, uint32_t name) {
    ScriptString value;
    ScriptString_FromRep(&value, nullptr);
    builtin<ScriptString* XB_CDECL(ScriptString*, uint32_t)>(0x009222d8)(&value, name);
    ScriptString_Copy(out, &value);
    string_release(&value);
    return out;
}

uint32_t* XB_CDECL ScrCall_Scr_VectorNormalize(uint32_t* out, uint32_t vec) { return vector_of<0x00922280>(out, vec); }

uint32_t* XB_CDECL ScrCall_GScr_GetPersistentVector(uint32_t* out, uint32_t key) {
    uint32_t vec[3] = {};
    const uint32_t* result = builtin<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922328)(vec, key);
    out[0] = result[0];
    out[1] = result[1];
    out[2] = result[2];
    return out;
}

// The builtin gets the address of the wrapper's own string argument.
ScriptStringRep* XB_CDECL ScrCall_GScr_ShakeRumbleNamed(uint32_t a, uint32_t b, ScriptString name, uint32_t c) {
    builtin<uint32_t XB_CDECL(uint32_t, uint32_t, ScriptString*, uint32_t)>(0x009223b8)(a, b, &name, c);
    return title_function<ScriptStringRep* XB_THISCALL(ScriptString*)>(0x0011d640)(&name);  // ScriptString_Release
}

uint32_t XB_CDECL ScrCall_GScr_GetZoneAtPoint(const uint32_t* point) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x0092249c)(point[0], point[1], point[2]);
}

uint32_t XB_CDECL ScrCall_GScr_PlaySoundWithNotify_2(const uint32_t* ent, uint32_t sound) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x0092220c)(*ent, sound, 0, 0);
}

uint32_t* XB_CDECL ScrCall_GScr_GetOrigin(uint32_t* out, const uint32_t* ent) {
    uint32_t vec[3];
    default_vector(vec);
    builtin<uint32_t XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(*ent, vec);
    copy_vector(out, vec);
    return out;
}

uint32_t XB_CDECL ScrCall_GScr_AmbientPlay(uint32_t sound) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922310)(sound, 0x3f800000u);  // 1.0f
}

uint32_t XB_CDECL ScrCall_GScr_SetAnimKnobAll(const uint32_t* ent, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                                              uint32_t e) {
    return builtin<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225c8)(
        *ent, a, b, c, d, e, 1);
}

}  // namespace game
