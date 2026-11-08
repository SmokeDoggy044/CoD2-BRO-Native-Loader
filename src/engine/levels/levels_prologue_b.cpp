#include "levels_prologue_b.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_array.h"
#include "g_scr_runtime.h"
#include "levels_twins.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

uint32_t level() { return title_global<uint32_t>(0x00921cbc); }
// The level's own entity: the word at +4 of the level block (a null block reads address 4).
const uint32_t* level_ent() { return level() ? pointer<uint32_t>(level() + 4) : nullptr; }

using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ...);
using CreateThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using GetOrigin = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using SetModel = void XB_CDECL(uint32_t, ScriptString*, uint32_t);
using MedicLine = ScriptString* XB_CDECL(ScriptString*, uint32_t);

ScriptString* from_text(ScriptString* out, uint32_t text) {
    ScriptString_FromCString(out, pointer<const char>(text));
    return out;
}

// level+0x124 [speaker][line] = text (HashMap_Index of the outer, then of the inner map).
void set_dialogue(uint32_t speaker, uint32_t line, uint32_t text) {
    using Index = ScriptString* XB_THISCALL(uint8_t*, const uint32_t*);
    uint32_t keys[2] = {line, speaker};
    uint8_t* const lines = HashMap_Index_PROLOGUE_7(pointer<HashMap>(level() + 0x124), &keys[1]);
    ScriptString_AssignCString(title_function<Index>(0x00461120)(lines, &keys[0]), pointer<const char>(text));
}

// The squad model names by the hash of the current model: full -> LOD and LOD -> full; 0 = none.
uint32_t lod_model(uint32_t hash) {
    if (hash == 0x845f7321) return 0x007f5fd8;  // "c_1stInffall_Lod1_Multi_3A"
    if (hash == 0x1904dbfa) return 0x007f7284;  // "c_brookeur1_Lod0"
    if (hash == 0xde24d242) return 0x007f7270;  // "c_kellyeur1_Lod0"
    if (hash == 0xc554dd42) return 0x007f725c;  // "c_delaneur1_Lod0"
    return 0;
}
uint32_t full_model(uint32_t hash) {
    if (hash == 0xc355d366) return 0x007f7244;  // "c_1stInffall_Multi_3A"
    if (hash == 0xf1f5b907) return 0x007f7238;  // "c_brookeur1"
    if (hash == 0x807bf914) return 0x007f722c;  // "c_kellyeur1"
    if (hash == 0x804d190a) return 0x007f7220;  // "c_delaneur1"
    return 0;
}

// The hash of the entity's model (read into the `scratch` string, released after).
uint32_t model_hash(const uint32_t* ent, ScriptString* scratch, uint32_t* hash) {
    const uint32_t value = *Scr_HashScriptString(
        hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetModel(ent, scratch)));
    ScriptString_Release(scratch);
    return value;
}

}  // namespace

ScriptString* XB_CDECL BroLvl_prologue_KellyAllClearLines(ScriptString* out, uint32_t ent, uint32_t line,
                                                          uint32_t index) {
    const uint32_t undefined = undefined_int();
    uint32_t pick = index != undefined ? index : undefined;
    if (line == 0xb807fa29) {
        if (index == undefined) pick = random_int(3);
        if (pick == 0) return from_text(out, 0x007f572c);  // "SQD_KEL_ALC_01_AllClearSa"
        if (pick == 1) return from_text(out, 0x007f5748);  // "SQD_KEL_ALC_02_AllClearSe"
        if (pick == 2) return from_text(out, 0x007f5764);  // "SQD_KEL_ALC_03_AllClearSi"
    } else if (line == 0xcc2871d3) {
        return from_text(out, 0x007f5714);  // "SQD_KEL_ALC_05_ClearUp"
    } else if (line == 0xa1cfbdbe) {
        return from_text(out, 0x007f56f8);  // "SQD_KEL_ALC_04_ClearDown"
    } else if (line == 0x06079852) {
        return from_text(out, 0x007f56e0);  // "SQD_KEL_ALC_06_Clear"
    } else if (line == 0x226688fc) {
        if (index == undefined) pick = random_int(6);
        switch (pick) {
        case 0: return from_text(out, 0x007f56c4);  // "SQD_KEL_ING_01_GetDownGre"
        case 1: return from_text(out, 0x007f56ac);  // "SQD_KEL_ING_02_Grenade"
        case 2: return from_text(out, 0x007f5690);  // "SQD_KEL_ING_03_GrenadeGet"
        case 3: return from_text(out, 0x007f5674);  // "SQD_KEL_ING_04_GrenadeTak"
        case 4: return from_text(out, 0x007f5658);  // "SQD_KEL_ING_05_IncomingGr"
        case 5: return from_text(out, 0x007f563c);  // "SQD_KEL_ING_06_LookoutGre"
        }
    } else if (line == 0xf90d48f4) {
        if (index == undefined) pick = random_int(3);
        if (pick == 0) return from_text(out, 0x007f55e8);  // "PRO_ADDL01_RED_Sargecome"
        if (pick == 1) return from_text(out, 0x007f5604);  // "PRO_ADDL02_RED_Sargeover"
        if (pick == 2) return from_text(out, 0x007f5620);  // "PRO_ADDL03_RED_Sargethis"
    } else if (line == 0x06e31d43) {
        title_function<MedicLine>(0x0041aa00)(out, ent);  // BroLvl_prologue_RandomManDownMedicLine
        return out;
    }
    ScriptString_Copy(out, &title_global<ScriptString>(0x00922e90));  // the empty string
    return out;
}

uint32_t* XB_CDECL BroLvl_prologue_LaunchVelocity(uint32_t* out, uint32_t self, ScrVec3 start, ScrVec3 end,
                                                  uint32_t time) {
    (void)self;
    const int32_t gravity = slot<int32_t XB_CDECL(const char*)>(0x009222dc)(pointer<const char>(0x0083365c));  // GScr_GetCvarInt("g_gravity")
    slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x00922268)(&start, &end);  // Scr_Distance (unused)
    const float t = as_float(time);
    const float t2 = t * t;
    const float drop = static_cast<float>(static_cast<int32_t>(0u - static_cast<uint32_t>(gravity))) *
                       title_global<float>(0x0083eb24);  // 0.5
    const float inv = title_global<float>(0x0083eb20) / t;  // 1.0
    const float z = ((as_float(end.v[2]) - as_float(start.v[2])) - t2 * drop) * inv;
    const float y = inv * (as_float(end.v[1]) - as_float(start.v[1]));
    const float x = inv * (as_float(end.v[0]) - as_float(start.v[0]));
    uint32_t velocity[3] = {bits(x), bits(y), bits(z)};
    title_function<StartThread>(0x0045b670)(0, 0, 0, 0, 0x0041a260, *level_ent(), &start, velocity,
                                            &time);  // Scr_StartThread_ent_vec_vec_f_PROLOGUE
    out[0] = velocity[0];
    out[1] = velocity[1];
    out[2] = velocity[2];
    return out;
}

void XB_CDECL Bro_prologue__Health_Reset(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(ent, *int_slot(ent, 0x166cb1df));  // ScrEnt_SetHealth
}

void XB_CDECL Bro_prologue__Vehicle_TankDeath(uint32_t ent) {
    const uint32_t tank = ent;
    uint32_t& scratch = ent;  // the original keeps its temporaries in the argument slot
    if (*ScrField_h4a3b5a6c_IsDefined(reinterpret_cast<bool*>(&scratch), tank))
        slot<void XB_CDECL(uint32_t)>(0x009221f4)(*ScrField_h4a3b5a6c(tank));  // GScr_FreeSound
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(  // GScr_GetEntVar
        tank, 0x06730ae8, *Scr_HashName(&scratch, pointer<const char>(0x007f5148)));  // "prologue::Vehicle_TankDeath"
    const uint32_t* const owner = level_ent();
    scratch = title_global<uint32_t>(0x0083eba8);  // 2.0
    uint32_t radius = title_global<uint32_t>(0x0083ebec);  // 500.0
    uint32_t buffer[3];
    const uint32_t* const o = slot<GetOrigin>(0x00922b20)(buffer, tank);  // ScrEnt_GetOrigin
    uint32_t origin[3] = {o[0], o[1], o[2]};
    title_function<StartThread>(0x0045b440)(0, 0, 0, 0, 0x004256b0, *owner, origin, &radius,
                                            &scratch);  // Scr_StartThread_ent_vec_f_f_PROLOGUE
    title_function<StartThread>(0x0011a910)(0, 0, 0, 0, 0x0041a800, tank);  // Scr_StartThreadFunctor1
}

uint32_t* XB_CDECL BroLvl_prologue_LineBloomfieldWaitForThe(uint32_t* out) {
    ScriptString line;
    ScriptString_FromRep(&line, nullptr);
    uint32_t facing[3];  // starts as the undefined vector, then (0, 180, 0)
    facing[0] = facing[1] = facing[2] = undefined_lane();
    const uint32_t speaker = *pointer<uint32_t>(level() + 0x848);
    const uint32_t anim = title_global<uint32_t>(0x008c5ab8);  // g_anim_prologue_f_PRO_E03L05_BLO_WaitForThe
    ScriptString_AssignCString(&line, pointer<const char>(0x007f5ee8));  // "PRO_E03L05_BLO_WaitForThe"
    out[0] = speaker;
    out[1] = 0;
    out[2] = anim;
    ScriptString_Copy(reinterpret_cast<ScriptString*>(&out[3]), &line);
    facing[0] = 0;
    facing[1] = title_global<uint32_t>(0x0083eb60);  // 180.0
    facing[2] = 0;
    out[4] = facing[0];
    out[5] = facing[1];
    out[6] = facing[2];
    ScriptString_Release(&line);
    return out;
}

void XB_CDECL BroLvl_prologue_Event3MgRouteThreads(uint32_t self, DynArray gunners, int32_t route) {
    (void)self;
    uint32_t* const array = reinterpret_cast<uint32_t*>(&gunners);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(gunners.size); ++i) {
        ScriptString key;
        from_text(&key, 0x0082dda8);  // "targetname"
        ScriptString name;
        ScriptString* const value =
            ScriptString_FromCStringPlusInt(&name, pointer<const char>(0x007f6250), route);  // "event3_mg_route"
        using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);
        uint32_t node = slot<GetNode>(0x009221cc)(value, &key, 0, 0);  // GScr_GetNode
        if (node == 0xffffffff) node = 0;
        ScriptString_Release(&name);
        ScriptString_Release(&key);
        const bool last = i == gunners.size - 1;
        const uint32_t gunner = *BroScr_ArrayElement(array, i);
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4b78;
            functor[1] = 0x0041d4a0;
            functor[2] = gunner;
            functor[3] = node;
            *reinterpret_cast<uint8_t*>(&functor[4]) = last;
        }
        slot<CreateThread>(0x0092216c)(0, 0, 0, gunner, functor, 0);  // Scr_CreateThread
        ++route;  // in the argument slot, as the original
    }
    if (gunners.data) Scr_Free(gunners.data);
}

void XB_CDECL BroLvl_prologue_AllyManDownMedicLine() {
    uint32_t speaker = undefined_int();
    const float best = title_global<float>(0x0083ee84);  // 1e8
    DynArray allies = {nullptr, 0, 0};
    ScriptString team;
    from_text(&team, 0x00832084);  // "allies"
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&allies, &team);  // GScr_GetAI
    ScriptString_Release(&team);
    uint32_t* const array = reinterpret_cast<uint32_t*>(&allies);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(allies.size); ++i) {
        uint32_t buffer[3];
        const uint32_t* p = slot<GetOrigin>(0x00922b20)(buffer, title_global<uint32_t>(0x00921cd0));  // ScrEnt_GetOrigin
        const uint32_t player[3] = {p[0], p[1], p[2]};
        const uint32_t ally = *BroScr_ArrayElement(array, i);
        uint32_t buffer2[3];
        p = slot<GetOrigin>(0x00922b20)(buffer2, ally);
        const uint32_t at[3] = {p[0], p[1], p[2]};
        using DistanceSquared = long double XB_CDECL(const uint32_t*, const uint32_t*);
        const long double d = slot<DistanceSquared>(0x0092226c)(at, player);  // Scr_DistanceSquared (ST0)
        const int32_t closer = static_cast<long double>(best) > d ? 1 : 0;
        if (static_cast<float>(closer) != title_global<float>(0x0083eafc)) speaker = i;  // 0.0
    }
    ScriptString line;
    title_function<MedicLine>(0x0041aa00)(&line, *level_ent());  // BroLvl_prologue_RandomManDownMedicLine
    if (line.rep) {
        using PlaySound = int32_t XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t);
        slot<PlaySound>(0x0092220c)(*BroScr_ArrayElement(array, speaker), &line, 0xfaca5d86, 0);  // GScr_PlaySoundWithNotify
    }
    ScriptString_Release(&line);
    if (allies.data) Scr_Free(allies.data);
}

void XB_CDECL BroLvl_prologue_SwapLodModel(uint32_t ent, uint32_t lod, uint32_t tag) {
    ScriptString model;
    ScriptString_FromRep(&model, nullptr);
    const bool to_lod = static_cast<uint8_t>(lod) != 0;
    ScriptString* const scratch = reinterpret_cast<ScriptString*>(&lod);  // the argument slot holds the model read
    uint32_t hash;
    const uint32_t block = level();
    if (to_lod) {
        DynArray_PushBack(pointer<uint8_t>(block + 0x874), &ent);
        if (const uint32_t name = lod_model(model_hash(&ent, scratch, &hash)))
            ScriptString_AssignCString(&model, pointer<const char>(name));
    } else if (ent == *(block ? pointer<uint32_t>(block + 4) : nullptr)) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(pointer<uint32_t>(level() + 0x874)[2]);
             ++i) {
            const uint32_t* const listed = BroScr_ArrayElement(pointer<uint32_t>(level() + 0x874), i);
            if (const uint32_t name = full_model(model_hash(listed, scratch, &hash)))
                ScriptString_AssignCString(&model, pointer<const char>(name));
            slot<SetModel>(0x00922548)(*BroScr_ArrayElement(pointer<uint32_t>(level() + 0x874), i), &model, 0);  // GScr_SetModel
        }
        DynArray_Resize(pointer<uint8_t>(level() + 0x874), 0, 0);
        ScriptString_Release(&model);
        return;
    } else {
        if (const uint32_t name = full_model(model_hash(&ent, scratch, &hash)))
            ScriptString_AssignCString(&model, pointer<const char>(name));
    }
    const uint32_t with = tag != undefined_handle() && tag == 0xe2de2626 ? *pointer<uint32_t>(level() + 0x854) : 0;
    slot<SetModel>(0x00922548)(ent, &model, with);  // GScr_SetModel
    ScriptString_Release(&model);
}

void XB_CDECL BroLvl_prologue_InitDialogueMisc() {
    set_dialogue(0x0627bcce, 0x0001b597, 0x007f82bc);  // "PRO_E03L07_KEL_NOW"
    set_dialogue(0x0627bcce, 0x0fc840c4, 0x007f699c);  // "PRO_E04L09_ALL_NiceShotSa"
    set_dialogue(0x0627bcce, 0xf808b693, 0x007f7de4);  // "PRO_E06L01_ALL_TAKECOVERT"
}

uint32_t* XB_CDECL LevelScript_prologue_SetImportCallbacks(uint32_t* block) {
    block[5] = 0x00464d00;
    block[6] = 0x00464dc0;
    block[7] = 0x00464e60;
    block[8] = 0x00464ea0;
    block[9] = 0x00464ed0;
    block[10] = 0x00464f20;
    block[11] = 0x00464f50;
    block[12] = 0x00464f90;
    block[13] = 0x00464fc0;
    block[14] = 0x00465080;
    block[15] = 0x00465120;
    block[16] = 0x00465160;
    block[17] = 0x00465190;
    block[18] = 0x004651e0;
    block[19] = 0x00465210;
    block[20] = 0x00465250;
    return block;
}

}  // namespace game
