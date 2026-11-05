#include "bro_scr_anim_scenes.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_anim_threads.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
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

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using LoopGuys = void XB_CDECL(const uint32_t*, uint32_t, uint32_t, ScriptString, uint32_t, ScrVec3, uint32_t, uint32_t,
                               ScrVec3);
using SingleGuys = void XB_CDECL(uint32_t, uint32_t, uint32_t, ScriptString, uint32_t, ScrVec3, uint32_t, ScrVec3);
using TagVec = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);

inline ScrVec3 get_vector(uint32_t address, uint32_t handle) {
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(address)(buffer, handle);
    ScrVec3 out;
    out.v[0] = v[0];
    out.v[1] = v[1];
    out.v[2] = v[2];
    return out;
}
inline ScrVec3 lanes() {
    const uint32_t lane = undefined_lane();
    ScrVec3 out;
    out.v[0] = lane;
    out.v[1] = lane;
    out.v[2] = lane;
    return out;
}
inline ScrVec3 vector_ec4() {  // the vector at 0x00922ec4
    ScrVec3 out;
    out.v[0] = title_global<uint32_t>(0x00922ec4);
    out.v[1] = title_global<uint32_t>(0x00922ec8);
    out.v[2] = title_global<uint32_t>(0x00922ecc);
    return out;
}
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
inline bool node_valid(uint32_t node) { return node != 0 && node != 0xffffffff; }
inline void loop_guys(const uint32_t* ref, uint32_t guys, uint32_t a3, ScriptString* anime, uint32_t p5, const ScrVec3& origin,
                      uint32_t p7, uint32_t p8, const ScrVec3& angles) {
    ScriptString copy;
    ScriptString_Copy(&copy, anime);
    title_function<LoopGuys>(0x00176720)(ref, guys, a3, copy, p5, origin, p7, p8, angles);  // BroScr_AnimLoopGuys
}
inline void single_guys(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString* anime, uint32_t p5, const ScrVec3& origin,
                        uint32_t flag, const ScrVec3& angles) {
    ScriptString copy;
    ScriptString_Copy(&copy, anime);
    title_function<SingleGuys>(0x00177450)(ent, guys, a3, copy, p5, origin, flag, angles);  // BroScr_AnimSingleGuys
}
// The scene place checks of the _2 forms (all results unused).
inline void place_checks(const DynArray* guys, uint32_t ent, ScriptString* tag, uint32_t node, uint32_t tag_ent) {
    DynArray* const list = const_cast<DynArray*>(guys);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list->size); ++i) {
        if (i >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), i + 1, i + 4);
        entity_handle_is_valid(list->data[i]);
    }
    if (tag->rep) {
        const uint32_t holder = entity_handle_is_valid(tag_ent) ? tag_ent : ent;
        uint32_t origin[3];
        const ScrVec3 lane = lanes();
        origin[0] = lane.v[0];
        origin[1] = lane.v[1];
        origin[2] = lane.v[2];
        slot<TagVec>(0x0092261c)(holder, origin, tag);  // GScr_GetTagOrigin
        uint32_t angles[3] = {lane.v[0], lane.v[1], lane.v[2]};
        slot<TagVec>(0x00922620)(holder, angles, tag);  // GScr_GetTagAngles
    } else if (node_valid(node)) {
        get_vector(0x00922ab8, node);  // ScrPathNode_GetOrigin
        get_vector(0x00922ac0, node);  // ScrPathNode_GetAngles
    } else {
        get_vector(0x00922b20, ent);  // ScrEnt_GetOrigin
        get_vector(0x00922ba0, ent);  // ScrEnt_GetAngles
    }
}
inline uint32_t guy_field(uint32_t guy) { return *int_slot(guy, 0x9c0e3c3d); }
inline ScriptString* string_field(uint32_t ent, uint32_t key) {
    return title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(ent_script_object(ent), key);  // ScriptKeyTable_GetString
}
inline void set_animname(uint32_t ent, const ScriptString* name) {
    ScriptString copy;
    ScriptString_Copy(&copy, name);
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(ent, copy);  // ScrEnt_SetAnimname
}
inline void sherman_tank_anim(uint32_t* tank, uint32_t id, ScriptString* anime) {
    bool defined;
    if (*ScrField_guys_IsDefined(&defined, *tank)) {
        using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
        for (uint32_t i = 0;; ++i) {
            if (!(static_cast<int32_t>(i) <
                  static_cast<int32_t>(title_function<GetArray>(0x0045cd50)(ent_script_object(*tank), 0x00325b53)->size)))
                break;
            DynArray* const guys = title_function<GetArray>(0x0045cd50)(ent_script_object(*tank), 0x00325b53);
            if (i >= guys->size) DynArray_Resize(reinterpret_cast<uint8_t*>(guys), i + 1, i + 4);
            uint32_t guy = guys->data[i];
            if (guy_field(guy) != id) continue;
            slot<void XB_CDECL(uint32_t)>(0x009224c8)(guy);  // GScr_StopAnimScripted
            set_animname(guy, string_field(guy, 0xf203df5e));
            uint32_t hash;
            queue_notify(guy, *Scr_HashName(&hash, pointer<const char>(0x0082558c)));  // "stopanim"
            {
                ScriptString anim;
                ScriptString_Copy(&anim, string_field(guy, 0xb6abdc5e));
                uint32_t anime_hash;
                const uint32_t a3 = *Scr_HashScriptString(&anime_hash, reinterpret_cast<const uint8_t* const*>(anime));
                BroScr_AnimSingleSoloAtNode(guy, guy, a3, anim, title_global<uint32_t>(0x00922ea0), *tank);
            }
            {
                ScriptString name;
                scr_string_from(&name, 0x0082557c);  // "sherman_tank"
                set_animname(guy, &name);
                scr_release(&name);
            }
            ScriptString idle;
            ScriptString* const idle_name =
                ScriptString_FromCStringPlusInt(&idle, pointer<const char>(0x00804310), static_cast<int32_t>(guy_field(guy)));  // "idle"
            uint32_t stop;
            const uint32_t* const stop_hash = Scr_HashName(&stop, pointer<const char>(0x0082558c));
            ScriptString* const anim = string_field(guy, 0xb6abdc5e);
            uint32_t idle_hash;
            const uint32_t* const idle_key = Scr_HashScriptString(&idle_hash, reinterpret_cast<const uint8_t* const*>(idle_name));
            using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*,
                                            const uint32_t*, ScriptString*, const uint32_t*, uint32_t*, uint32_t*);
            title_function<Start>(0x0078ab20)(0, 0, 0, 0, 0x00179650, guy, &guy, idle_key, anim, stop_hash,
                                              pointer<uint32_t>(0x00922ea0), tank);  // (BroScr_AnimLoopAtNodeArrayCall)
            scr_release(&idle);
            break;
        }
    }
    scr_release(anime);
}

}  // namespace

void XB_CDECL BroScr_AnimLoopAtEnt(const uint32_t* ref, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                   uint32_t place, uint32_t p7, uint32_t p8) {
    if (static_cast<uint8_t>(p8)) {
        loop_guys(ref, guys, a3, &anime, p5, vector_ec4(), p7, p8, vector_ec4());
    } else if (entity_handle_is_valid(place)) {
        const ScrVec3 origin = get_vector(0x00922b20, place);  // ScrEnt_GetOrigin
        const ScrVec3 angles = get_vector(0x00922ba0, place);  // ScrEnt_GetAngles
        loop_guys(ref, guys, a3, &anime, p5, origin, p7, p8, angles);
    } else if (*ref == level_handle()) {
        loop_guys(ref, guys, a3, &anime, p5, vector_ec4(), p7, 1, vector_ec4());
    } else {
        uint32_t b1[3];
        const uint32_t* o = ScrCall_ScrEnt_GetOrigin(ref, b1);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t b2[3];
        const uint32_t* a = ScrCall_ScrEnt_GetAngles(ref, b2);
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        loop_guys(ref, guys, a3, &anime, p5, origin, p7, p8, angles);
    }
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtNode(const uint32_t* ref, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                    uint32_t place, uint32_t p7, uint32_t p8) {
    if (static_cast<uint8_t>(p8)) {
        loop_guys(ref, guys, a3, &anime, p5, lanes(), p7, p8, lanes());
    } else if (node_valid(place)) {
        const ScrVec3 origin = get_vector(0x00922ab8, place);  // ScrPathNode_GetOrigin
        const ScrVec3 angles = get_vector(0x00922ac0, place);  // ScrPathNode_GetAngles
        loop_guys(ref, guys, a3, &anime, p5, origin, p7, p8, angles);
    } else if (*ref == level_handle()) {
        loop_guys(ref, guys, a3, &anime, p5, lanes(), p7, 1, lanes());
    } else {
        uint32_t b1[3];
        const uint32_t* o = ScrCall_ScrEnt_GetOrigin(ref, b1);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t b2[3];
        const uint32_t* a = ScrCall_ScrEnt_GetAngles(ref, b2);
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        loop_guys(ref, guys, a3, &anime, p5, origin, p7, p8, angles);
    }
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtNode(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                      uint32_t place, uint32_t flag) {
    if (static_cast<uint8_t>(flag)) {
        single_guys(ent, guys, a3, &anime, p5, lanes(), flag, lanes());
    } else if (node_valid(place)) {
        const ScrVec3 origin = get_vector(0x00922ab8, place);  // ScrPathNode_GetOrigin
        const ScrVec3 angles = get_vector(0x00922ac0, place);  // ScrPathNode_GetAngles
        single_guys(ent, guys, a3, &anime, p5, origin, flag, angles);
    } else if (ent == level_handle()) {
        single_guys(ent, guys, a3, &anime, p5, lanes(), 1, lanes());
    } else {
        uint32_t b1[3];
        const uint32_t* o = ScrCall_ScrEnt_GetOrigin(&ent, b1);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t b2[3];
        const uint32_t* a = ScrCall_ScrEnt_GetAngles(&ent, b2);
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        single_guys(ent, guys, a3, &anime, p5, origin, flag, angles);
    }
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtEnt(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                     uint32_t place, uint32_t flag) {
    if (static_cast<uint8_t>(flag)) {
        single_guys(ent, guys, a3, &anime, p5, lanes(), flag, lanes());
    } else if (entity_handle_is_valid(place)) {
        const ScrVec3 origin = get_vector(0x00922b20, place);  // ScrEnt_GetOrigin
        const ScrVec3 angles = get_vector(0x00922ba0, place);  // ScrEnt_GetAngles
        single_guys(ent, guys, a3, &anime, p5, origin, flag, angles);
    } else if (ent == level_handle()) {
        single_guys(ent, guys, a3, &anime, p5, lanes(), 1, lanes());
    } else {
        uint32_t b1[3];
        const uint32_t* o = ScrCall_ScrEnt_GetOrigin(&ent, b1);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t b2[3];
        const uint32_t* a = ScrCall_ScrEnt_GetAngles(&ent, b2);
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        single_guys(ent, guys, a3, &anime, p5, origin, flag, angles);
    }
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtNode_2(uint32_t ent, const void* guys, uint32_t a3, ScriptString tag, uint32_t node,
                                        uint32_t tag_ent) {
    place_checks(static_cast<const DynArray*>(guys), ent, &tag, node, tag_ent);
    ScriptString copy;
    ScriptString_Copy(&copy, &tag);
    BroScr_AnimSingleAtNode(ent, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(guys)), a3, copy, node, tag_ent, 0);
    scr_release(&tag);
}

void XB_CDECL BroScr_AnimLoopAtNode_2(uint32_t ent, const void* guys, uint32_t a3, ScriptString tag, uint32_t p5,
                                      uint32_t node, uint32_t tag_ent) {
    place_checks(static_cast<const DynArray*>(guys), ent, &tag, node, tag_ent);
    ScriptString copy;
    ScriptString_Copy(&copy, &tag);
    BroScr_AnimLoopAtNodeCall_2(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ent)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(guys)), a3, copy, p5, node, tag_ent);
    scr_release(&tag);
}

void XB_CDECL BroScr_WaitAnimSoloFinished(uint32_t node, uint32_t guy, uint32_t a3, ScriptString tag, uint32_t p5,
                                          uint32_t p6) {
    const int32_t start = get_time();
    for (;;) {
        if (!entity_handle_is_valid(node)) break;
        ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(node);  // ScrCall_GScr_GetEntScriptObject
        if (!table) break;
        const uint8_t* const value = reinterpret_cast<const uint8_t*>(ScriptKeyTable_Find(table, 0xdedbf624));
        const uint8_t undefined = title_global<uint8_t>(0x00922ed0);
        if ((value ? *value : undefined) == undefined) break;
        Scr_WaitTillString(node, pointer<const char>(0x00825568));  // "finished anim solo"
        if (get_time() > start + 5000) {
            scr_release(&tag);
            return;
        }
    }
    *reinterpret_cast<uint8_t*>(key_slot(node, 0xdedbf624, title_global<uint8_t>(0x00922ed0))) = 1;
    DynArray guys = {nullptr, 0, 0};
    DynArray_Resize(reinterpret_cast<uint8_t*>(&guys), 1, 4);
    guys.data[0] = guy;
    ScriptString copy;
    ScriptString_Copy(&copy, &tag);
    BroScr_AnimSingleAtNode(node, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&guys)), a3, copy, p5, p6, 0);
    queue_notify(node, 0x2079d925);
    if (guys.data) Scr_Free(guys.data);
    scr_release(&tag);
}

void XB_CDECL BroScr_ShermanTankAnim(uint32_t tank, uint32_t id, ScriptString anime) { sherman_tank_anim(&tank, id, &anime); }

void XB_CDECL BroScr_ShermanTankAnim_2(uint32_t tank, uint32_t id, ScriptString anime) { sherman_tank_anim(&tank, id, &anime); }

void XB_CDECL BroScr_ShermanTankAnim_3(uint32_t tank, uint32_t id, ScriptString anime) { sherman_tank_anim(&tank, id, &anime); }

}  // namespace game
