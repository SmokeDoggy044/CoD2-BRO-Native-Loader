#include "bro_scr_notetrack.h"

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

constexpr uint32_t kModels = 0xef260d98;  // the actor's created-model array field

// HashMap_Index (0x00461120) on the notetrack parameter map: the value word for `key`.
uint32_t* param(uint32_t params, const uint32_t* key) {
    return title_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(0x00461120)(params, key);
}

uint32_t spawn(ScriptString* classname) {
    const uint32_t zero[3] = {0, 0, 0};
    return scr_slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(classname, zero, 0);  // GScr_Spawn
}

// GScr_GetTagOrigin / GScr_GetTagAngles (`slot`) of `tag` on `ent`, out preset to the undefined lane.
ScrVec3 tag_vector(uint32_t slot, uint32_t ent, uint32_t* tag) {
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t out[3] = {lane, lane, lane};
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*)>(slot)(ent, out, tag);
    return {{out[0], out[1], out[2]}};
}

// ScrEnt_GetOrigin / ScrEnt_GetAngles (`slot`, out first).
ScrVec3 ent_vector(uint32_t slot, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* v = scr_slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(slot)(buffer, ent);
    return {{v[0], v[1], v[2]}};
}

void set_vector(uint32_t slot, uint32_t ent, ScrVec3 v) { scr_slot<void XB_CDECL(uint32_t, ScrVec3)>(slot)(ent, v); }

uint32_t* grow(uint32_t* array, uint32_t index) {
    if (index >= array[2]) DynArray_Resize(reinterpret_cast<uint8_t*>(array), index + 1, index + 4);
    return pointer<uint32_t>(array[0]) + index;
}

// Element `index` of the model array (ScriptKeyTable_GetArray, grown when past the count).
uint32_t* model_at(uint32_t actor, uint32_t index) {
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return grow(title_function<GetArray>(0x0045cd50)(BroScr_ScriptObjectOrEmpty(actor), kModels), index);
}

// The model array through ScriptKeyTable_Find, added empty (ScriptKeyTable_SetArray) when missing.
uint32_t* model_array(uint32_t actor) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(actor);
    uint32_t* value = ScriptKeyTable_Find(table, kModels);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        value = title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*)>(0x0045cc80)(table, kModels, empty);
    }
    return pointer<uint32_t>(*value);
}

uint32_t level_entity() {
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    return title_global<uint32_t>(level ? level + 4 : 0);
}

}  // namespace

void XB_CDECL BroScr_CreateModel(uint32_t actor, uint32_t params) {
    const uint32_t ent = actor;
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    const uint32_t index = title_function<GetArray>(0x0045cd50)(BroScr_ScriptObjectOrEmpty(ent), kModels)[2];
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab98);  // "script_model"
    const uint32_t model = spawn(&classname);
    *model_at(ent, index) = model;
    actor = index * 4;  // the original keeps the byte offset in the argument slot
    uint32_t key;
    uint32_t* name = param(params, Scr_HashName(&key, pointer<const char>(0x00824ebc)));  // "create model"
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t)>(0x00922548)(ent, name, 0);       // GScr_SetModel
    key = 0x0cf2dc5b;
    const ScrVec3 origin = tag_vector(0x0092261c, ent, param(params, &key));  // GScr_GetTagOrigin
    set_vector(0x00922b24, *model_at(ent, index), origin);                    // ScrEnt_SetOrigin
    key = 0x0cf2dc5b;
    const ScrVec3 angles = tag_vector(0x00922620, ent, param(params, &key));  // GScr_GetTagAngles
    set_vector(0x00922ba4, *model_at(ent, index), angles);                    // ScrEnt_SetAngles
    scr_release(&classname);
}

void XB_CDECL BroScr_DeleteModel(uint32_t actor, uint32_t params) {
    const uint32_t ent = actor;
    bool defined;
    title_function<bool* XB_CDECL(bool*, uint32_t)>(0x00146bd0)(&defined, ent);  // ScrField_scriptmodel_IsDefined
    if (!defined) {
        ScriptString message;
        scr_string_from(&message, 0x00824ed0);  // "Tried to remove a model with delete model before it was create model'd on guy: "
        ScriptString animname;
        const ScriptString* name = ScrCall_ScrEnt_GetAnimname(&actor, &animname);
        ScriptString text;
        ScriptString_Concat(&text, &message, name);
        ScrLocal_ReleaseString_2(text);
        scr_release(&animname);
        scr_release(&message);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(model_array(ent)[2]); ++i) {
        uint32_t key = 0xdec84f52;
        if (*param(params, &key)) {
            const ScrVec3 angles = ent_vector(0x00922ba0, *model_at(ent, i));  // ScrEnt_GetAngles
            const uint32_t lane = title_global<uint32_t>(0x00922eb8);
            uint32_t forward[3] = {lane, lane, lane};
            scr_slot<void XB_CDECL(uint32_t*, const ScrVec3*)>(0x00922290)(forward, &angles);  // Scr_AnglesToForward (unused)
            ent_vector(0x00922b20, *model_at(ent, i));                                        // ScrEnt_GetOrigin (unused)
            const ScrVec3 origin = ent_vector(0x00922b20, *model_at(ent, i));
            // GScr_RadiusDamage(origin, 350, 700, 50)
            scr_slot<void XB_CDECL(const ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922428)(&origin, 0x43af0000, 0x442f0000,
                                                                                             0x42480000);
        } else {
            key = 0xe6f61fe4;
            if (*param(params, &key)) {
                const uint32_t level = level_entity();
                key = 0xe6f61fe4;
                using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
                title_function<StartThread>(0x0045a7c0)(0, 0, 0, 0, 0x001b6020, level, param(params, &key));  // Scr_StartThread_ent_str(BroScr_ExploderIfSet)
            }
        }
        scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(*grow(model_array(ent), i));  // GScr_Delete
    }
}

void XB_CDECL BroScr_NotetrackDropGun(uint32_t target, uint32_t actor, uint32_t params) {
    bool defined;
    title_function<bool* XB_CDECL(bool*, uint32_t)>(0x0012ac60)(&defined, actor);  // ScrField_h4225d5c5_IsDefined
    if (defined) return;
    const bool attach = target != actor;
    ScriptString classname;
    ScrCall_GScr_GetWeaponClassname2(&classname, scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(actor));  // ScrActor_GetInt_f0
    uint32_t gun = spawn(&classname);
    {
        ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(actor);
        uint32_t* value = ScriptKeyTable_Find(table, 0x4225d5c5);
        if (!value) value = ScriptKeyTable_Set(table, 0x4225d5c5, 0);
        *value = gun;
    }
    uint32_t key = 0x0001dc47;
    const ScrVec3 origin = tag_vector(0x0092261c, target, param(params, &key));  // GScr_GetTagOrigin
    set_vector(0x00922b24, gun, origin);                                         // ScrEnt_SetOrigin
    key = 0x0001dc47;
    const ScrVec3 angles = tag_vector(0x00922620, target, param(params, &key));  // GScr_GetTagAngles
    set_vector(0x00922ba4, gun, angles);                                         // ScrEnt_SetAngles
    if (attach) {
        const uint32_t offset[3] = {0, 0, 0};
        const uint32_t turn[3] = {0, 0, 0};
        key = 0x0001dc47;
        using Link = void XB_CDECL(uint32_t, uint32_t, uint32_t*, const uint32_t*, const uint32_t*, uint32_t);
        scr_slot<Link>(0x009224f4)(gun, target, param(params, &key), turn, offset, 0);  // GScr_LinkToTagWithOffset
    } else {
        ScriptString name;
        scr_string_from(&name, 0x0083ab88);  // "script_origin"
        uint32_t holder = spawn(&name);
        set_vector(0x00922b24, holder, ent_vector(0x00922b20, gun));
        set_vector(0x00922ba4, holder, ent_vector(0x00922ba0, gun));
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*);
        title_function<StartThread>(0x0045a620)(0, 0, 0, 0, 0x0011fbf0, level_entity(), &gun, &holder);  // Scr_StartThread_ent_ent_ent
        scr_release(&name);
    }
    title_function<void XB_CDECL(uint32_t, uint32_t)>(0x001ad3f0)(actor, 0x0036b1a5);  // BroScr_PutGunInHand
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d94)(actor, 0);                 // ScrActor_SetInt_1378
    scr_release(&classname);
}

}  // namespace game
