#include "bro_scr_anim.h"

#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "qcommon_fs.h"
#include "title_data.h"
#include "levels_twins.h"

namespace game {

namespace {

constexpr uint32_t kOne = 0x3f800000;  // float words pushed as immediates
using SetAnim6 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SetAnim7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SetAnim8 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void SetAnimKnob(uint32_t entity, uint32_t anim, uint32_t weight, uint32_t time, uint32_t rate, uint32_t restart) {
    scr_slot<SetAnim6>(0x009225d8)(entity, anim, weight, time, rate, restart);  // GScr_SetAnimKnob
}

uint32_t QueueNotifyExitCover(uint32_t entity) {
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(entity, 0x281d9462);  // Scr_QueueNotify
    return 0;
}

}  // namespace

uint32_t XB_CDECL Bro_anim_exit_cover_left(uint32_t entity) {
    return QueueNotifyExitCover(entity);
}

uint32_t XB_CDECL Bro_anim_exit_cover_right(uint32_t entity) {
    return QueueNotifyExitCover(entity);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf684(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf684_2(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf684_3(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf7a4(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf7a4), kOne, 0x3e4ccccd, kOne, 1);
}

void XB_CDECL BroScr_TurretSetProneRecoverAnim(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf7bc), kOne, 0x3e4ccccd, kOne, 1);
}

void XB_CDECL BroScr_SetProneLegsStraightKnob(uint32_t entity, uint32_t time) {
    scr_slot<SetAnim7>(0x009225c8)(entity, title_global<uint32_t>(0x008cf138), title_global<uint32_t>(0x008cd7dc), kOne,
                                   time, kOne, 0);  // GScr_SetAnimKnobAll(prone_legsstraight, Body)
}

void XB_CDECL BroScr_SetAimAnimKnobs(uint32_t entity, uint32_t flag, uint32_t anim1, uint32_t anim2, uint32_t anim3,
                                     uint32_t weight, uint32_t time, uint32_t rate) {
    SetAnimKnob(entity, anim1, weight, time, rate, 0);
    scr_slot<SetAnim7>(0x009225e0)(entity, flag, anim2, weight, time, rate, 0);  // GScr_SetFlaggedAnimKnob
    SetAnimKnob(entity, anim3, weight, time, rate, 0);
}

void XB_CDECL BroScr_AnimCmd_SetAnim(uint32_t a1, uint32_t, uint32_t a3, uint32_t, uint32_t, uint32_t a6, uint32_t a7,
                                     uint32_t a8) {
    scr_slot<SetAnim6>(0x009225dc)(a1, a3, a6, a7, a8, 0);  // GScr_SetAnim
}

void XB_CDECL BroScr_AnimCmd_SetAnimKnobAll(uint32_t a1, uint32_t, uint32_t a3, uint32_t a4, uint32_t, uint32_t a6,
                                            uint32_t a7, uint32_t a8) {
    scr_slot<SetAnim7>(0x009225c8)(a1, a3, a4, a6, a7, a8, 0);  // GScr_SetAnimKnobAll
}

void XB_CDECL BroScr_AnimCmd_SetFlaggedAnimKnobAll(uint32_t a1, uint32_t, uint32_t a3, uint32_t a4, uint32_t a5,
                                                   uint32_t a6, uint32_t a7, uint32_t a8) {
    scr_slot<SetAnim8>(0x009225e4)(a1, a5, a3, a4, a6, a7, a8, 0);  // GScr_SetFlaggedAnimKnobAll
}

void XB_CDECL BroScr_AddToNamedEntList(ScriptString name, uint32_t ent) {
    uint32_t hash;
    own_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(&Scr_HashScriptString)(&hash, &name);  // Scr_HashScriptString
    alignas(4) uint8_t copy[0x30];
    HashMap_CopyCtor_3(reinterpret_cast<HashMap*>(copy),
                       reinterpret_cast<const HashMap*>(title_global<uint32_t>(0x00922ee0) + 0x408));
    own_function<void XB_THISCALL(uint8_t*)>(&HashMap_Dtor_PROLOGUE_15)(copy);  // HashMap_Dtor_PROLOGUE_15
    auto list = [&hash]() {
        return HashMap_Index_4(reinterpret_cast<HashMap*>(title_global<uint32_t>(0x00922ee0) + 0x408), &hash);
    };
    using At = uint32_t* XB_THISCALL(uint8_t*, const int32_t*);
    const auto at = title_function<At>(0x0078d440);  // the int map's operator[]
    int32_t index = 0;
    if (*reinterpret_cast<int32_t*>(list() + 0xc) == 0) {
        index = 0;
        *at(list(), &index) = ent;
    } else {
        index = 0;
        for (int32_t count = *reinterpret_cast<int32_t*>(list() + 0xc); index < count;) {
            if (*at(list(), &index) == ent) {
                scr_release(&name);
                return;
            }
            ++index;
            count = *reinterpret_cast<int32_t*>(list() + 0xc);
        }
        int32_t pair[2];
        pair[0] = *reinterpret_cast<int32_t*>(list() + 0xc);
        index = pair[0];
        uint8_t* const map = list();
        uint32_t found;
        title_function<void XB_THISCALL(uint8_t*, uint32_t*, const int32_t*)>(0x0078b560)(map, &found, &index);  // find
        if (found == *reinterpret_cast<uint32_t*>(map + 8)) {  // end
            pair[1] = static_cast<int32_t>(title_global<uint32_t>(0x00922eb4));  // g_scrUndefinedHandle
            uint32_t result[2];
            using Insert = uint32_t* XB_THISCALL(uint8_t*, uint32_t*, const int32_t*);
            found = *title_function<Insert>(0x0078d040)(map, result, pair);  // insert
        }
        *reinterpret_cast<uint32_t*>(found + 0xc) = ent;
    }
    scr_release(&name);
}

namespace {

// The weight list of the group's anim set: level +0x5dc map by the first guy's animname hash
// (HashMap_Index_5), then by `key` (HashMap_Index_2); an int array.
DynArray* anim_weights(DynArray* guys, uint32_t* key) {
    if (guys->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(guys), 1, 4);
    ScriptString animname;
    const ScriptString* name = ScrCall_ScrEnt_GetAnimname(guys->data, &animname);
    uint32_t hash;
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
    void* map = own_function<void* XB_THISCALL(uint32_t, const uint32_t*)>(&HashMap_Index_5)(title_global<uint32_t>(0x00922ee0) + 0x5dc, &hash);
    DynArray* weights = own_function<DynArray* XB_THISCALL(void*, uint32_t*)>(&HashMap_Index_2)(map, key);
    scr_release(&animname);
    return weights;
}

uint32_t* weight_at(DynArray* weights, uint32_t i) {
    if (i >= weights->size) own_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(&DynArray_ResizeInt)(weights, i + 1, i + 4);  // DynArray_ResizeInt
    return weights->data + i;
}

}  // namespace

uint32_t* XB_CDECL BroScr_AnimLoopPickIndex(uint32_t* out, DynArray guys, uint32_t key) {
    if (guys.size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(&guys), 1, 4);
    uint32_t count;
    {
        ScriptString animname;
        const ScriptString* name = ScrCall_ScrEnt_GetAnimname(guys.data, &animname);
        uint32_t hash;
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
        count = own_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(&HashMap_Index_PROLOGUE_10)(
            title_global<uint32_t>(0x00922ee0) + 0x744, &hash)[3];  // HashMap_Index_10: the set's anims
        scr_release(&animname);
    }
    uint32_t result = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(count);  // Scr_RandomInt
    if (static_cast<int32_t>(count) > 1) {
        float total = 0.0f;
        uint32_t weighted = 0;
        const bool any = static_cast<int32_t>(count) > 0;
        if (any) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
                const bool set = *weight_at(anim_weights(&guys, &key), i) != title_global<uint32_t>(0x00922ec0);
                if (set) {
                    ++weighted;
                    total = platform_addss(static_cast<float>(static_cast<int32_t>(*weight_at(anim_weights(&guys, &key), i))), total);
                }
            }
        }
        if (weighted == count) {
            // All anims weighted: the first whose running sum passes RandomFloat(total).
            const float pick = static_cast<float>(scr_slot<long double XB_CDECL(float)>(0x00922230)(total));  // Scr_RandomFloat
            float sum = 0.0f;
            if (any) {
                for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
                    sum = platform_addss(static_cast<float>(static_cast<int32_t>(*weight_at(anim_weights(&guys, &key), i))), sum);
                    if (sum > pick) {
                        result = i;
                        break;
                    }
                }
            }
        }
    }
    *out = result;
    if (guys.data) own_function<void XB_CDECL(void*)>(&Scr_Free)(guys.data);  // Scr_Free
    return out;
}

namespace {

ScrVec3 lane_vector() {
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    return {{lane, lane, lane}};
}

// GScr_GetTagOrigin / GScr_GetTagAngles (`slot`) of `tag` on `ent`, out preset to the undefined lane.
ScrVec3 tag_vector(uint32_t slot, uint32_t ent, ScriptString* tag) {
    ScrVec3 out = lane_vector();
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(slot)(ent, out.v, tag);
    return out;
}

// ScrEnt_ / ScrPathNode_ GetOrigin / GetAngles (`slot`, out first).
ScrVec3 get_vector(uint32_t slot, uint32_t handle) {
    uint32_t buffer[3];
    const uint32_t* v = scr_slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(slot)(buffer, handle);
    return {{v[0], v[1], v[2]}};
}

uint32_t* guy_at(DynArray* guys, uint32_t i) {
    if (i >= guys->size) DynArray_Resize(reinterpret_cast<uint8_t*>(guys), i + 1, i + 4);
    return guys->data + i;
}

// Shared body of the two dead placers below.
void place_guys(uint32_t ent, DynArray* guys, uint32_t* key, ScriptString* tag, uint32_t node, uint32_t tag_ent, bool teleport) {
    ScrVec3 origin = lane_vector();
    ScrVec3 angles = lane_vector();
    if (tag->rep) {
        const uint32_t holder = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(tag_ent) ? tag_ent : ent;  // Scr_EntityHandleIsValid
        origin = tag_vector(0x0092261c, holder, tag);
        angles = tag_vector(0x00922620, holder, tag);
    } else if (node != 0 && node != 0xffffffff) {
        origin = get_vector(0x00922ab8, node);  // ScrPathNode_GetOrigin
        angles = get_vector(0x00922ac0, node);  // ScrPathNode_GetAngles
    } else {
        origin = get_vector(0x00922b20, ent);  // ScrEnt_GetOrigin
        angles = get_vector(0x00922ba0, ent);  // ScrEnt_GetAngles
    }
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 at = get_vector(0x00922b20, title_global<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x7f8));
        scr_slot<uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t)>(0x009222f0)(&classname, &at, 0);  // GScr_Spawn (unused)
        scr_release(&classname);
    }
    ScrVec3 offset = teleport ? lane_vector() : ScrVec3{{0, 0, 0}};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys->size); ++i) {
        ScriptString animname;
        const ScriptString* name = ScrCall_ScrEnt_GetAnimname(guy_at(guys, i), &animname);
        uint32_t hash;
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
        uint32_t* map = own_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(&HashMap_Index_PROLOGUE_8)(
            title_global<uint32_t>(0x00922ee0) + 0x19c, &hash);  // HashMap_Index_8
        uint32_t it;
        own_function<uint32_t* XB_THISCALL(uint32_t*, uint32_t*, const uint32_t*)>(&HashMap_Find)(map, &it, key);  // HashMap_Find
        if (it == map[2]) {
            const uint32_t pair[2] = {*key, 0};
            uint32_t inserted;
            it = *own_function<uint32_t* XB_THISCALL(uint32_t*, uint32_t*, const uint32_t*)>(&HashMap_Insert_PROLOGUE_2)(map, &inserted, pair);  // HashMap_Insert
        }
        const uint32_t local = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(it))[3];
        scr_slot<void XB_CDECL(uint32_t*, const ScrVec3*, const ScrVec3*, uint32_t)>(0x009223fc)(offset.v, &origin, &angles,
                                                                                               local);  // GScr_TransformLocalOffset
        scr_release(&animname);
        if (teleport && scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(*guy_at(guys, i)))  // GScr_IsSentient
            scr_slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t, uint32_t)>(0x009226f8)(*guy_at(guys, i), offset.v, 0, 0);  // GScr_ActorTryTeleport
        else
            scr_slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(*guy_at(guys, i), offset);  // ScrEnt_SetOrigin
    }
    if (guys->data) own_function<void XB_CDECL(void*)>(&Scr_Free)(guys->data);  // Scr_Free
    scr_release(tag);
}

}  // namespace

void XB_CDECL BroScr_AnimGroupTeleportGuys(uint32_t ent, DynArray guys, uint32_t key, ScriptString tag, uint32_t node,
                                           uint32_t tag_ent) {
    place_guys(ent, &guys, &key, &tag, node, tag_ent, true);
}

void XB_CDECL BroScr_AnimGroupPlaceGuys(uint32_t ent, DynArray guys, uint32_t key, ScriptString tag, uint32_t node,
                                        uint32_t tag_ent) {
    place_guys(ent, &guys, &key, &tag, node, tag_ent, false);
}

}  // namespace game
