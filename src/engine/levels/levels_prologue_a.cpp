#include "levels_prologue_a.h"

#include "bro_scr_ai_common.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "levels_twins.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_battle_threads.h"

namespace game {

namespace {

using bro_ai::pointer;
using bro_ai::slot;

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The prologue's level block (its own copy of the levelscript block pointer).
uint32_t level() { return title_global<uint32_t>(0x00921cbc); }

bool valid(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(ent) != 0; }  // Scr_EntityHandleIsValid

// GScr_GetEntArray as the scripts call it (a fifth, unread 0 pushed).
using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);

// &array->data[i], resizing first when i >= size.
uint32_t* element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data + i;
}

// Sets `name` to "0" when it is empty (InitDebugCvars).
void default_cvar(uint32_t name) {
    ScriptString value;
    const bool empty = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, name), pointer<const char>(0x0082dfeb));  // ""
    ScriptString_Release(&value);
    if (empty) slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(name, 0x0082e098);  // GScr_SetCvar(name, "0")
}

// The level block's +0x124 map: map[speaker][line] = text (HashMap_Index_PROLOGUE_7, then HashMap_Index_PROLOGUE_11).
void set_line(uint32_t speaker, uint32_t line, uint32_t text) {
    using Index = ScriptString* XB_THISCALL(uint8_t*, const uint32_t*);
    uint8_t* const lines = HashMap_Index_PROLOGUE_7(pointer<HashMap>(level() + 0x124), &speaker);
    ScriptString_AssignCString(title_function<Index>(0x00461120)(lines, &line), pointer<const char>(text));
}

}  // namespace

ScriptString* XB_CDECL BroLvl_prologue_RandomManDownMedicLine(ScriptString* out) {
    switch (slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(2)) {  // Scr_RandomInt
        case 0: ScriptString_FromCString(out, pointer<const char>(0x007f48ec)); break;  // "SQD_DEN_SIT_09_Medic"
        case 1: ScriptString_FromCString(out, pointer<const char>(0x007f4904)); break;  // "SQD_DEN_SIT_08_ManDownMed"
        default: ScriptString_Copy(out, &title_global<ScriptString>(0x00922e90)); break;  // the empty string
    }
    return out;
}

uint32_t XB_CDECL BroLvl_prologue_AnimHandleByName(const char* set, const char* anim) {
    uint32_t handle = 0;
    uint32_t hash;
    const uint32_t set_hash = *Scr_HashName(&hash, set);
    const uint32_t anim_hash = *Scr_HashName(&hash, anim);
    using GetSet = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t*, uint32_t);
    const uint32_t found = title_function<GetSet>(0x0041b800)(set_hash, anim_hash, &handle, 0);  // LevelScript_prologue_AnimHandle_GetSet
    return found ? handle : 0;
}

void XB_CDECL BroLvl_prologue_InitDebugCvars() {
    default_cvar(0x007f4ccc);  // "skipto"
    default_cvar(0x007f4cc4);  // "artist"
    default_cvar(0x007f4cb0);  // "create_shrimp_paths"
    default_cvar(0x007f4ca4);  // "no_shrimps"
    default_cvar(0x007f4c94);  // "no_scene_anim"
}

void XB_CDECL BroLvl_prologue_ShermanDestroyed(uint32_t tank) {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x007f4e50));  // "v_sherman_d1"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(tank, &text, 0);  // GScr_SetModel
    ScriptString_Release(&text);
    slot<void XB_CDECL(uint32_t)>(0x009228d0)(tank);  // GScr_Vehicle_ClearTurretTarget
    uint32_t buffer[3];
    const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, tank);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    // GScr_Earthquake(0.25, 3, origin, 1050)
    slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, origin, 0x44834000);
    ScriptString_FromCString(&text, pointer<const char>(0x007f4e40));  // "vehicle_explode"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(tank, &text, 0, 0, 0);  // GScr_PlaySound
    ScriptString_Release(&text);
    bool defined;
    if (*ScrField_mgturret_IsDefined(&defined, tank)) slot<void XB_CDECL(uint32_t)>(0x00922540)(*ScrField_mgturret(tank));  // GScr_Delete
}

void XB_CDECL BroLvl_prologue_DeleteNoteworthyAndTargeter(uint32_t, uint32_t name_id) {
    // The name is built in the argument's own slot, as the original does.
    ScriptString* const name = reinterpret_cast<ScriptString*>(&name_id);
    const uint8_t* const text = slot<uint8_t* XB_CDECL(int32_t)>(0x00922a28)(static_cast<int32_t>(name_id));  // IntHashMap_FindValue
    ScriptString_FromCString(name, reinterpret_cast<const char*>(text));
    uint32_t ent = slot<GetEnt>(0x009221c4)(name, 0x93c97595, 0, 0, 0);  // GScr_GetEntArray(name, script_noteworthy)
    uint32_t targeter = 0;
    if (valid(ent)) {
        ScriptString targetname;
        targeter = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTargetname(&ent, &targetname), 0xee2890d4, 0, 0, 0);  // by target
        ScriptString_Release(&targetname);
    }
    if (valid(targeter)) slot<void XB_CDECL(uint32_t)>(0x00922540)(targeter);  // GScr_Delete
    if (valid(ent)) slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);
    ScriptString_Release(name);
}

uint32_t* XB_CDECL BroLvl_prologue_Event3BloomfieldHoldUp(uint32_t* out) {
    const uint32_t actor = *pointer<uint32_t>(level() + 0x848);
    uint32_t idle = 0;
    ScriptString line;
    ScriptString_FromRep(&line, nullptr);
    const uint32_t lane = bro_ai::undefined_lane();
    uint32_t angles[3] = {lane, lane, lane};
    ScriptString spot;
    ScriptString key;
    ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));    // "targetname"
    ScriptString_FromCString(&spot, pointer<const char>(0x007f5ed0));   // "event3_bloomfield_spot1"
    using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);
    uint32_t node = slot<GetNode>(0x009221cc)(&spot, &key, 0, 0);       // GScr_GetNode
    if (node == 0xffffffffu) node = 0;
    ScriptString_Release(&spot);
    ScriptString_Release(&key);
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(0x00922ab8)(buffer, node);  // ScrPathNode_GetOrigin
    const uint32_t at[3] = {v[0], v[1], v[2]};
    uint32_t buffer2[3];
    v = slot<GetVec>(0x00922b20)(buffer2, *pointer<uint32_t>(level() + 0x848));  // ScrEnt_GetOrigin
    const uint32_t from[3] = {v[0], v[1], v[2]};
    const long double d2 = slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(from, at);  // Scr_DistanceSquared
    if (static_cast<long double>(title_global<float>(0x0083ef28)) > d2) {  // 160 (FCOMIP)
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(*pointer<uint32_t>(level() + 0x848)) == 0x070b89a4)  // ScrActor_GetInt_184
            idle = title_global<uint32_t>(0x008c5a58);  // c_standA_talk_stop
        else
            idle = title_global<uint32_t>(0x008c58c0);  // c_crouchaim_talk_stop
    }
    const uint32_t hold = title_global<uint32_t>(0x008c5aac);  // f_PRO_E03L04_BLO_HoldUpHold
    ScriptString_AssignCString(&line, pointer<const char>(0x007f5eb4));  // "PRO_E03L04_BLO_HoldUpHold"
    angles[0] = 0;
    angles[1] = title_global<uint32_t>(0x0083eb60);  // 180
    angles[2] = 0;
    out[0] = actor;
    out[2] = hold;
    out[1] = idle;
    ScriptString_Copy(reinterpret_cast<ScriptString*>(out + 3), &line);
    out[4] = angles[0];
    out[5] = angles[1];
    out[6] = angles[2];
    ScriptString_Release(&line);
    return out;
}

uint8_t* XB_CDECL BroLvl_prologue_AllList880XBelowNeg1760(uint8_t* out) {
    int32_t below = 0;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(pointer<DynArray>(level() + 0x880)->size); ++i) {
        uint32_t buffer[3];
        const uint32_t ent = *element(pointer<DynArray>(level() + 0x880), i);
        const uint32_t* const v = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const uint32_t origin[3] = {v[0], v[1], v[2]};
        if (title_global<float>(0x007f6234) > bro_ai::as_float(origin[0])) ++below;  // -1760 (COMISS)
    }
    *out = below == static_cast<int32_t>(pointer<DynArray>(level() + 0x880)->size);
    return out;
}

void XB_CDECL BroLvl_prologue_KillByTargetnameAfterRandomDelay(uint32_t, ScriptString name, float min, float max) {
    DynArray ents = {nullptr, 0, 0};
    Scr_GetEntArray(address_of(&name), 0x0d786b4c, reinterpret_cast<uint8_t*>(&ents), 0);  // by targetname
    if (ents.size != 0) {
        *static_cast<volatile float*>(&max) = max - min;  // kept in the argument's slot, as the original does
        const float base = min;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
            const long double r = slot<long double XB_CDECL(float)>(0x00922230)(max);  // Scr_RandomFloat (ST0)
            const float delay = static_cast<float>(static_cast<long double>(base) + r);
            const uint32_t ent = *element(&ents, i);
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a70;
                functor[1] = 0x00420740;  // waits `delay`, then GScr_DoDamage(ent, health + 50)
                functor[2] = ent;
                functor[3] = bro_ai::bits(delay);
            }
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                                       0);  // Scr_CreateThread
        }
    }
    if (ents.data) Scr_Free(ents.data);
    ScriptString_Release(&name);
}

void XB_CDECL BroLvl_prologue_StartEvent1Drones3d() {
    DynArray paths = {nullptr, 0, 0};
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(0x007f6654));  // "event1_drones3d"
    Scr_GetEntArray(address_of(&name), 0x0d786b4c, reinterpret_cast<uint8_t*>(&paths), 0);  // by targetname
    ScriptString_Release(&name);
    const uint32_t block = level();
    const uint32_t* const handle = pointer<uint32_t>(block ? block + 4 : 0);
    ScriptString type;
    ScriptString_FromCString(&type, pointer<const char>(0x007f6644));  // "event1_drones"
    uint32_t zero = 0;
    uint32_t min = title_global<uint32_t>(0x0083ec9c);  // 3
    uint32_t max = title_global<uint32_t>(0x0083ece8);  // 5
    uint8_t staggered = 0;
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, DynArray*, ScriptString*,
                                    uint32_t*, uint32_t*, uint32_t*, uint8_t*);
    own_function<Start>(&Scr_StartThread_ent_arrent_str_i_f_f_b_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_DronePathsStartAllCall_6), *handle, &paths, &type, &zero, &min, &max,
                                      &staggered);  // Scr_StartThread_ent_arrent_str_i_f_f_b(BroScr_DronePathsStartAllCall_6)
    ScriptString_Release(&type);
    if (paths.data) Scr_Free(paths.data);
}

void XB_CDECL BroLvl_prologue_InitDialogueKelly() {
    const uint32_t kelly = 0x06833df9;
    set_line(kelly, 0x031bf7ba, 0x007f853c);  // "PRO_E02L01_KEL_TheresTheB.."
    set_line(kelly, 0x0231ff3a, 0x007f8520);  // "PRO_E02L02_KEL_OnTheBridg.."
    set_line(kelly, 0xebe9e618, 0x007f8504);  // "PRO_E03L02_KEL_InTheBuild.."
    set_line(kelly, 0x4c1c08c1, 0x007f84ec);  // "PRO_E03L03_KEL_UpAnOver"
    set_line(kelly, 0x3d689a30, 0x007f84d0);  // "PRO_E03L06_KEL_AllenAndIW.."
    set_line(kelly, 0x7b57f5df, 0x007f84b4);  // "PRO_E04L01_KEL_ThoseSherm.."
    set_line(kelly, 0x17539d89, 0x007f8498);  // "PRO_E04L04_KEL_WATCHITTha.."
    set_line(kelly, 0x66044236, 0x007f847c);  // "PRO_E04L05_KEL_MaybeWeCan.."
    set_line(kelly, 0x15d74c43, 0x007f6310);  // "PRO_E04L06_KEL_STAYDOWNAL.."
    set_line(kelly, 0xe17b2053, 0x007f8460);  // "PRO_E04L12_KEL_STUKASWATC.."
    set_line(kelly, 0x40c72af4, 0x007f8448);  // "PRO_E05L03_KEL_INCOMING"
    set_line(kelly, 0x0627bcce, 0x007f8430);  // "PRO_E06L02_KEL_Allen"
}

}  // namespace game
