#include "bro_scr_anim_reach.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using TagVec = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);

inline ScrVec3 lane_vector() {
    const uint32_t lane = undefined_lane();
    return {{lane, lane, lane}};
}
// GScr_GetTagOrigin / GScr_GetTagAngles (`address`) of `tag` on `ent`, out preset to the undefined lane.
inline ScrVec3 tag_vector(uint32_t address, uint32_t ent, ScriptString* tag) {
    ScrVec3 out = lane_vector();
    slot<TagVec>(address)(ent, out.v, tag);
    return out;
}
// ScrEnt_ / ScrPathNode_ GetOrigin / GetAngles (`address`, out first) into a copy.
inline ScrVec3 get_vector(uint32_t address, uint32_t handle) {
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(address)(buffer, handle);
    return {{v[0], v[1], v[2]}};
}
inline uint32_t* guy_at(DynArray* guys, uint32_t i) {
    if (i >= guys->size) DynArray_Resize(reinterpret_cast<uint8_t*>(guys), i + 1, i + 4);
    return guys->data + i;
}
// DynArray copy-assign (0x0045c270, thiscall) into a zeroed by-value argument.
inline DynArray copy_array(const DynArray* source) {
    DynArray copy = {0, 0, 0};
    title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0045c270)(&copy, source);  // DynArray_Assign4
    return copy;
}
inline void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn

// The anim under `key` in the table of `guy`'s animname (level + 0x19c; a missing key is added as 0);
// the animname (in `animname`) is the caller's to release.
inline uint32_t anim_of(const uint32_t* guy, const uint32_t* key, ScriptString* animname) {
    const ScriptString* name = ScrCall_ScrEnt_GetAnimname(guy, animname);
    uint32_t hash;
    using Index = uint32_t* XB_THISCALL(uint32_t, const uint32_t*);
    using Find = uint32_t* XB_THISCALL(uint32_t*, uint32_t*, const uint32_t*);
    uint32_t* const map = title_function<Index>(0x004641e0)(
        title_global<uint32_t>(0x00922ee0) + 0x19c,
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name)));  // HashMap_Index_8
    uint32_t it;
    title_function<Find>(0x0045e6d0)(map, &it, key);  // HashMap_Find
    if (it == map[2]) {
        const uint32_t pair[2] = {*key, 0};
        uint32_t inserted;
        it = *title_function<Find>(0x004608a0)(map, &inserted, pair);  // HashMap_Insert
    }
    return pointer<uint32_t>(it)[3];
}

inline void reach(uint32_t core_ent, DynArray* guys, uint32_t anime, ScriptString* tag, uint32_t tag_ent, const ScrVec3& origin,
           const ScrVec3& angles) {
    ScriptString tag_copy;
    ScriptString_Copy(&tag_copy, tag);
    BroScr_AnimReachCore(core_ent, copy_array(guys), anime, tag_copy, origin, tag_ent, angles);
    if (guys->data) Scr_Free(guys->data);
    scr_release(tag);
}

}  // namespace

void XB_CDECL BroScr_AnimReachCore(uint32_t ent, DynArray guys, uint32_t anime, ScriptString tag, ScrVec3 origin,
                                   uint32_t tag_ent, ScrVec3 angles) {
    if (tag.rep) {
        const uint32_t holder = entity_handle_is_valid(tag_ent) ? tag_ent : ent;
        origin = tag_vector(0x0092261c, holder, &tag);  // GScr_GetTagOrigin
        angles = tag_vector(0x00922620, holder, &tag);  // GScr_GetTagAngles
    }
    uint32_t spawned;
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 at = get_vector(0x00922b20, *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x7f8));  // ScrEnt_GetOrigin(player)
        spawned = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &at, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    int32_t started = 0;
    ScrVec3 goal = lane_vector();
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
        ScriptString animname;
        const uint32_t anim = anim_of(guy_at(&guys, i), &anime, &animname);
        slot<void XB_CDECL(uint32_t*, ScrVec3*, ScrVec3*, uint32_t)>(0x009223fc)(goal.v, &origin, &angles,
                                                                               anim);  // GScr_TransformLocalOffset
        scr_release(&animname);
        slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922684)(*guy_at(&guys, i), &goal);  // GScr_SetGoalPos
        ++started;
        const uint32_t guy = *guy_at(&guys, i);
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a28;
            functor[1] = 0x0012f2e0;  // BroScr_AnimReachGuyThread
            functor[2] = guy;
            functor[3] = spawned;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, guy, functor, 0);  // Scr_CreateThread
    }
    for (int32_t n = started; n; --n) Scr_WaitTill(spawned, 0x2253e8ee);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
        const uint32_t radius = *key_slot(*guy_at(&guys, i), 0x25bf26c6, undefined_float());
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(*guy_at(&guys, i), radius);  // ScrSentient_SetFloat_18 (goal radius)
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(spawned);  // GScr_Delete
    if (guys.data) Scr_Free(guys.data);
    scr_release(&tag);
}

void XB_CDECL BroScr_AnimReach(uint32_t ent, DynArray guys, uint32_t anime, ScriptString tag, uint32_t node,
                               uint32_t tag_ent) {
    ScrVec3 origin;
    ScrVec3 angles;
    if (node != 0 && node != 0xffffffff) {
        origin = get_vector(0x00922ab8, node);  // ScrPathNode_GetOrigin
        angles = get_vector(0x00922ac0, node);  // ScrPathNode_GetAngles
    } else {
        origin = get_vector(0x00922b20, ent);  // ScrEnt_GetOrigin
        angles = get_vector(0x00922ba0, ent);  // ScrEnt_GetAngles
    }
    reach(ent, &guys, anime, &tag, tag_ent, origin, angles);
}

void XB_CDECL BroScr_AnimReachEnt(uint32_t ent, DynArray guys, uint32_t anime, ScriptString tag, uint32_t node,
                                  uint32_t tag_ent) {
    ScrVec3 origin;
    ScrVec3 angles;
    if (entity_handle_is_valid(node)) {
        origin = get_vector(0x00922b20, node);  // ScrEnt_GetOrigin
        angles = get_vector(0x00922ba0, node);  // ScrEnt_GetAngles
    } else {
        origin = get_vector(0x00922b20, ent);
        angles = get_vector(0x00922ba0, ent);
    }
    reach(ent, &guys, anime, &tag, tag_ent, origin, angles);
}

void XB_CDECL BroScr_AnimScriptedRestoreAnimname(uint32_t ent, uint32_t guy, ScriptString tag) {
    end_on(guy, 0x78dbe561);
    end_on(guy, kDeath);
    end_on(ent, kDeath);
    end_on(ent, 0x78dbe561);
    Scr_WaitTill(guy, 0x0d6e32cd);
    slot<void XB_CDECL(uint32_t)>(0x009224c8)(guy);  // GScr_StopAnimScripted
    slot<void XB_CDECL(uint32_t)>(0x0092250c)(guy);  // GScr_Unlink
    const uint32_t key = 0x140aaa8f;
    ScriptString animname;
    const uint32_t anim = anim_of(&guy, &key, &animname);
    ScrVec3 angles = tag_vector(0x00922620, ent, &tag);  // GScr_GetTagAngles
    ScrVec3 origin = tag_vector(0x0092261c, ent, &tag);  // GScr_GetTagOrigin
    slot<void XB_CDECL(uint32_t, uint32_t, ScrVec3*, ScrVec3*, uint32_t)>(0x009224c0)(guy, 0x54e3b2b6, &origin, &angles,
                                                                                     anim);  // GScr_AnimScriptedNoNotify
    scr_release(&animname);
    Scr_WaitTill(guy, 0x54e3b2b6);
    bool defined;
    if (*ScrField_oldanimname_IsDefined(&defined, guy)) {
        ScriptString name;
        ScriptString_Copy(&name, title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
                                     ent_script_object(guy), 0x55576ed6));  // ScriptKeyTable_GetString (oldanimname)
        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(guy, name);  // ScrEnt_SetAnimname
    }
    queue_notify(guy, 0x996a3647);
    scr_release(&tag);
}

}  // namespace game
