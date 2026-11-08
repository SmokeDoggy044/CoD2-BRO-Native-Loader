#include "bro_scr_anim_scenes.h"

#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_anim.h"
#include "bro_scr_actor_threads.h"
#include "bro_scr_ai_anim.h"
#include "bro_scr_anim_threads.h"
#include "bro_scr_common.h"
#include "bro_scr_notetrack.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "levels_twins.h"
#include "bro_scr_entity_threads.h"
#include "bro_scr_wait_misc.h"
#include "bro_scr_wait_notify.h"

namespace game {

using namespace bro_ai;

namespace {

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using LoopGuys = void XB_CDECL(const uint32_t*, uint32_t, uint32_t, ScriptString, uint32_t, ScrVec3, uint32_t, uint32_t,
                               ScrVec3);
using SingleGuys = void XB_CDECL(uint32_t, uint32_t, uint32_t, ScriptString, ScrVec3, uint32_t, uint32_t, ScrVec3);
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
    own_function<LoopGuys>(&BroScr_AnimLoopGuys)(ref, guys, a3, copy, p5, origin, p7, p8, angles);  // BroScr_AnimLoopGuys
}
inline void single_guys(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString* anime, const ScrVec3& origin, uint32_t tag_ent,
                        uint32_t flag, const ScrVec3& angles) {
    ScriptString copy;
    ScriptString_Copy(&copy, anime);
    own_function<SingleGuys>(&BroScr_AnimSingleGuys)(ent, guys, a3, copy, origin, tag_ent, flag, angles);  // BroScr_AnimSingleGuys
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
                  static_cast<int32_t>(own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(*tank), 0x00325b53)->size)))
                break;
            DynArray* const guys = own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(*tank), 0x00325b53);
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
            title_function<Start>(0x0078ab20)(0, 0, 0, 0, own_address(&BroScr_AnimLoopAtNodeArrayCall), guy, &guy, idle_key, anim, stop_hash,
                                              pointer<uint32_t>(0x00922ea0), tank);  // (BroScr_AnimLoopAtNodeArrayCall)
            scr_release(&idle);
            break;
        }
    }
    scr_release(anime);
}

// The inlined element access: &data[i], the array resized to (i + 1, i + 4) first when i >= count.
inline uint32_t* element_at(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}
// The level's per-animname map at `offset` (HashMap_Index_8 by the hash of `guy`'s animname): the
// node under `key`, a missing key inserted as 0. The animname (in `animname`) is the caller's to release.
inline uint32_t* scene_node(uint32_t offset, const uint32_t* guy, const uint32_t* key, ScriptString* animname) {
    using Index = uint32_t* XB_THISCALL(uint32_t, const uint32_t*);
    using Find = uint32_t* XB_THISCALL(uint32_t*, uint32_t*, const uint32_t*);
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(guy, animname);
    uint32_t hash;
    uint32_t* const map = own_function<Index>(&HashMap_Index_PROLOGUE_8)(
        title_global<uint32_t>(0x00922ee0) + offset,
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name)));  // HashMap_Index_8
    uint32_t it;
    own_function<Find>(&HashMap_Find)(map, &it, key);  // HashMap_Find
    if (it == map[2]) {
        const uint32_t pair[2] = {*key, 0};
        uint32_t inserted;
        it = *own_function<Find>(&HashMap_Insert_PROLOGUE_2)(map, &inserted, pair);  // HashMap_Insert
    }
    return pointer<uint32_t>(it);
}
// The scene's sound string: level +0x124 [hash(animname)] [key] (HashMap_Index_7, HashMap_Index_11).
inline ScriptString* scene_sound(const uint32_t* guy, const uint32_t* key, ScriptString* animname) {
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(guy, animname);
    uint32_t hash;
    uint8_t* const map = own_function<uint8_t* XB_THISCALL(uint32_t, const uint32_t*)>(&HashMap_Index_PROLOGUE_7)(
        title_global<uint32_t>(0x00922ee0) + 0x124, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name)));
    return title_function<ScriptString* XB_THISCALL(uint8_t*, const uint32_t*)>(0x00461120)(map, key);
}
// A script_origin spawned at the 0x00922eec entity's origin.
inline uint32_t spawn_scene_origin() {
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab88);  // "script_origin"
    uint32_t buffer[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(  // ScrEnt_GetOrigin
        buffer, title_global<uint32_t>(0x00922eec));
    uint32_t origin[3];
    origin[0] = o[0];
    origin[1] = o[1];
    origin[2] = o[2];
    const uint32_t spawned =
        slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
    scr_release(&classname);
    return spawned;
}
// Scr_CreateThread of a {0x007f4a88, function, on, guy, notify} functor on `on`.
inline void notify_thread(uint32_t function, uint32_t on, const uint32_t* guy, uint32_t notify) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a88;
        functor[1] = function;
        functor[2] = on;
        functor[3] = *guy;
        functor[4] = notify;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
        0, 0, 0, on, functor, 0);  // Scr_CreateThread
}

// level + offset [hash(guy's animname)] [key]: a looped scene's per-variant array (`index` / `array` are
// HashMap_Index_10 / HashMap_Index for the int maps +0x5b0 / +0x744, 0x0078e280 / 0x0078d350 for the string
// map +0x634). The animname (in `animname`) is the caller's to release.
inline DynArray* variant_array(uint32_t offset, uint32_t index, uint32_t array, const uint32_t* guy, const uint32_t* key,
                               ScriptString* animname) {
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(guy, animname);
    uint32_t hash;
    const uint32_t* const h = Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
    uint32_t* const map =
        title_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(index)(title_global<uint32_t>(0x00922ee0) + offset, h);
    return title_function<DynArray* XB_THISCALL(uint32_t*, const uint32_t*)>(array)(map, key);
}
// &array[i], reserved to (i + 1, i + 4) first by `reserve` (DynArray_ReserveInt0 / _ReserveString) when i >= count.
inline uint32_t* variant_at(DynArray* array, uint32_t i, uint32_t reserve) {
    if (i >= array->size) title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(reserve)(array, i + 1, i + 4);
    return &array->data[i];
}
constexpr uint32_t kStringIndex = 0x0078e280;  // HashMap_Index_BUCHHOLZ
constexpr uint32_t kStringArray = 0x0078d350;
constexpr uint32_t kReserveInt = 0x0045d480;     // DynArray_ReserveInt0
constexpr uint32_t kReserveString = 0x0045c2e0;  // DynArray_ReserveString
// Scr_CreateThread of a {0x007f4b00, function, on, notify} functor on `on`.
inline void notify_thread_on(uint32_t function, uint32_t on, uint32_t notify) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4b00;
        functor[1] = function;
        functor[2] = on;
        functor[3] = notify;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
        0, 0, 0, on, functor, 0);  // Scr_CreateThread
}
// DynArray copy-assign (0x0045c270, thiscall) into a zeroed by-value argument.
inline DynArray copy_array(const DynArray* source) {
    DynArray copy = {0, 0, 0};
    own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&copy, source);  // DynArray_Assign4
    return copy;
}
// The guy's level +0x160 map, copied (HashMap_CopyCtor_4 inlined) and dropped (the script's discarded read).
inline void drop_map_copy(const uint32_t* guy) {
    ScriptString animname;
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(guy, &animname);
    uint32_t hash;
    const HashMap* const source = reinterpret_cast<HashMap*>(HashMap_Index_6(
        pointer<HashMap>(title_global<uint32_t>(0x00922ee0) + 0x160),
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name))));
    HashMap copy;
    copy.traits = source->traits;
    copy.head = own_function<HashNode* XB_CDECL()>(&std_list_BuyHead)();  // std_list_BuyHead
    copy.size = 0;
    copy.buckets = nullptr;
    copy.buckets_last = nullptr;
    copy.buckets_end = nullptr;
    HashMap_CopyFrom_4(&copy, source);
    own_function<void XB_THISCALL(HashMap*)>(&HashMap_Dtor_PROLOGUE_19)(&copy);  // HashMap_Dtor
    scr_release(&animname);
}

// The scene's notetrack entries: level +0x160 [animname hash] [anime] (an array of 0x28-byte string maps).
inline DynArray* note_entries(const uint32_t* animhash, const uint32_t* anime) {
    return reinterpret_cast<DynArray*>(HashMap_Index_8(
        reinterpret_cast<HashMap*>(HashMap_Index_6(pointer<HashMap>(title_global<uint32_t>(0x00922ee0) + 0x160), animhash)),
        anime));
}
// Entry j with the element access inline (DynArray_ResizeHashMap past the count), as the title's first
// lookups are; the later ones call DynArray_AtHashMap (entry_at).
inline uint8_t* entry_inline(const uint32_t* animhash, const uint32_t* anime, uint32_t j) {
    DynArray* const list = note_entries(animhash, anime);
    if (j >= list->size) DynArray_ResizeHashMap(list, j + 1, j + 4);
    return reinterpret_cast<uint8_t*>(list->data) + j * 0x28;
}
inline uint8_t* entry_at(const uint32_t* animhash, const uint32_t* anime, uint32_t j) {
    return DynArray_AtHashMap(note_entries(animhash, anime), j);
}
inline ScriptString* note_field(uint8_t* entry, const uint32_t* key) {
    return title_function<ScriptString* XB_THISCALL(uint8_t*, const uint32_t*)>(0x00461120)(entry, key);  // HashMap_Index_11
}
// The entry's string under the hash of title string `name`: inline entry first, then the name's hash.
inline ScriptString* field_inline(const uint32_t* animhash, const uint32_t* anime, uint32_t j, uint32_t name) {
    uint8_t* const entry = entry_inline(animhash, anime, j);
    uint32_t hash;
    return note_field(entry, Scr_HashName(&hash, pointer<const char>(name)));
}
// The same through DynArray_AtHashMap: the name's hash first.
inline ScriptString* field_at(const uint32_t* animhash, const uint32_t* anime, uint32_t j, uint32_t name) {
    uint32_t hash;
    const uint32_t* const key = Scr_HashName(&hash, pointer<const char>(name));
    return note_field(entry_at(animhash, anime, j), key);
}
// The same under a constant key hash.
inline ScriptString* field_key(const uint32_t* animhash, const uint32_t* anime, uint32_t j, uint32_t key) {
    const uint32_t k = key;
    return note_field(entry_at(animhash, anime, j), &k);
}
inline uint32_t word_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

void XB_CDECL BroScr_AnimSingleGuys(uint32_t ent, uint32_t guys_word, uint32_t anime, ScriptString tag, ScrVec3 origin,
                                    uint32_t tag_ent, uint32_t flag, ScrVec3 angles) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const uint32_t*, const uint32_t*, const uint32_t*);
    DynArray* const guys = pointer<DynArray>(guys_word);
    // The place at `tag` on tag_ent (else on ent).
    if (tag.rep) {
        uint32_t tmp[3];
        tmp[0] = tmp[1] = tmp[2] = undefined_lane();
        const uint32_t holder = entity_handle_is_valid(tag_ent) ? tag_ent : ent;
        using TagVec3 = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
        slot<TagVec3>(0x0092261c)(holder, tmp, &tag);  // GScr_GetTagOrigin
        origin.v[0] = tmp[0];
        origin.v[1] = tmp[1];
        origin.v[2] = tmp[2];
        tmp[0] = tmp[1] = tmp[2] = undefined_lane();
        slot<TagVec3>(0x00922620)(holder, tmp, &tag);  // GScr_GetTagAngles
        angles.v[0] = tmp[0];
        angles.v[1] = tmp[1];
        angles.v[2] = tmp[2];
    }

    // Each guy: its face anim (map +0x1d8) and sound (+0x124), its scene anim (+0x19c) played with a
    // notetrack thread; the last guy with a scene anim / with only a face anim is remembered.
    int32_t last_anim = -1;
    int32_t last_face = -1;
    for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(guys->size); ++i) {
        ScriptString sound;
        ScriptString_FromRep(&sound, nullptr);
        bool has_face = false;
        bool has_sound = false;
        bool has_anim = false;
        uint32_t face = 0;
        bool test;
        {
            ScriptString animname;
            test = scene_node(0x1d8, element_at(guys, i - 1), &anime, &animname)[3] != 0;
            scr_release(&animname);
        }
        if (test) {
            has_face = true;
            ScriptString animname;
            face = scene_node(0x1d8, element_at(guys, i - 1), &anime, &animname)[3];
            scr_release(&animname);
        }
        {
            ScriptString animname;
            test = scene_sound(element_at(guys, i - 1), &anime, &animname)->rep != nullptr;
            scr_release(&animname);
        }
        if (test) {
            has_sound = true;
            ScriptString animname;
            ScriptString_Assign(&sound, scene_sound(element_at(guys, i - 1), &anime, &animname));
            scr_release(&animname);
        }
        {
            ScriptString animname;
            test = scene_node(0x19c, element_at(guys, i - 1), &anime, &animname)[3] != 0;
            scr_release(&animname);
        }
        if (test) {
            has_anim = true;
            if (static_cast<uint8_t>(flag) != title_global<uint8_t>(0x00922ed0) && static_cast<uint8_t>(flag)) {
                // At the guy's own place.
                uint32_t buffer[3];
                const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, *element_at(guys, i - 1));
                origin.v[0] = o[0];
                origin.v[1] = o[1];
                origin.v[2] = o[2];
                uint32_t buffer2[3];
                const uint32_t* a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer2, *element_at(guys, i - 1));
                angles.v[0] = a[0];
                angles.v[1] = a[1];
                angles.v[2] = a[2];
            }
            {
                ScriptString animname;
                const uint32_t anim = scene_node(0x19c, element_at(guys, i - 1), &anime, &animname)[3];
                slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t)>(0x009224c0)(
                    *element_at(guys, i - 1), 0xce00786e, origin.v, angles.v, anim);  // GScr_AnimScriptedNoNotify
                scr_release(&animname);
            }
            last_anim = static_cast<int32_t>(i - 1);
            {
                // The guy's level +0x160 map, copied and dropped (the script's discarded read).
                ScriptString animname;
                const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(element_at(guys, i - 1), &animname);
                uint32_t hash;
                HashMap copy;
                HashMap_CopyCtor_4(&copy, reinterpret_cast<HashMap*>(HashMap_Index_6(
                                              pointer<HashMap>(title_global<uint32_t>(0x00922ee0) + 0x160),
                                              Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name)))));
                own_function<void XB_THISCALL(HashMap*)>(&HashMap_Dtor_PROLOGUE_19)(&copy);  // HashMap_Dtor
                scr_release(&animname);
            }
            const uint32_t notify = 0xce00786e;
            title_function<StartThread>(0x0017ada0)(0, 0, 0, 0, own_address(&BroScr_AnimNotetrackCommands), ent, element_at(guys, i - 1), &notify, &tag_ent,
                                                    &anime);  // Scr_StartThread_ent_ent_hash_ent_hash(BroScr_AnimNotetrackCommands)
            const uint32_t guy = *element_at(guys, i - 1);
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4b00;
                functor[1] = own_address(&BroScr_AnimNoteTracksThread);  // BroScr_AnimNoteTracksThread
                functor[2] = guy;
                functor[3] = 0xce00786e;
            }
            slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                0, 0, 0, guy, functor, 0);  // Scr_CreateThread
        }
        if (has_face || has_sound) {
            ScriptString empty;
            ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
            ScriptString copy;
            if (has_anim) {
                const uint32_t notify = title_global<uint32_t>(0x00922ee8);
                ScriptString_Copy(&copy, &sound);
                BroScr_StartAnimFaceWait(*element_at(guys, i - 1), face, copy, 0x3f800000, notify, empty,
                                         title_global<uint32_t>(0x00922ea4));
            } else {
                ScriptString_Copy(&copy, &sound);
                BroScr_StartAnimFaceWait(*element_at(guys, i - 1), face, copy, 0x3f800000, 0x56caf2eb, empty,
                                         title_global<uint32_t>(0x00922ea4));
            }
            last_face = static_cast<int32_t>(i - 1);
        }
        {
            ScriptString animname;
            ScrCall_ScrEnt_GetAnimname(element_at(guys, i - 1), &animname);
            BroScr_AddToNamedEntList(animname, anime);
        }
        scr_release(&sound);
    }

    // Wait for the scene: a script_origin notified `anime` when the remembered guy dies or finishes.
    uint32_t waiter = 0;
    if (last_anim != -1) {
        waiter = spawn_scene_origin();
        notify_thread(0x0011fac0, waiter, element_at(guys, static_cast<uint32_t>(last_anim)), anime);  // Scr_NotifyAfterDeath
        notify_thread(own_address(&BroScr_NotifyAfterSingleAnim), waiter, element_at(guys, static_cast<uint32_t>(last_anim)), anime);  // BroScr_NotifyAfterSingleAnim
        Scr_WaitTill(waiter, anime);
    } else if (last_face != -1) {
        waiter = spawn_scene_origin();
        notify_thread(0x0011fac0, waiter, element_at(guys, static_cast<uint32_t>(last_face)), anime);  // Scr_NotifyAfterDeath
        notify_thread(own_address(&BroScr_NotifyAfterWaitTill_56caf2eb), waiter, element_at(guys, static_cast<uint32_t>(last_face)),
                      anime);  // BroScr_NotifyAfterWaitTill_56caf2eb
        Scr_WaitTill(waiter, anime);
    }
    queue_notify(ent, anime);
    if (entity_handle_is_valid(waiter)) slot<void XB_CDECL(uint32_t)>(0x00922540)(waiter);  // GScr_Delete
    scr_release(&tag);
}

void XB_CDECL BroScr_AnimLoopGuys(const uint32_t* ref, uint32_t guys_word, uint32_t anime, ScriptString tag, uint32_t ender,
                                  ScrVec3 origin, uint32_t tag_ent, uint32_t flag, ScrVec3 angles) {
    using TagVec3 = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
    DynArray* const guys = pointer<DynArray>(guys_word);
    {
        bool defined;
        if (!*ScrField_loops_IsDefined(&defined, *element_at(guys, 0))) *int_slot(*element_at(guys, 0), 0x06f63273) = 0;
    }
    notify_thread_on(own_address(&BroScr_LimitSameAnimation), *element_at(guys, 0), anime);  // BroScr_LimitSameAnimation
    if (ender != undefined_handle()) {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(*ref, ender);  // Scr_EndOn
        notify_thread(own_address(&BroScr_DecrementCounterOnNotify), *ref, element_at(guys, 0), ender);     // BroScr_DecrementCounterOnNotify
    }
    const bool has_tag = tag.rep != nullptr;
    const bool on_tag_ent = has_tag && entity_handle_is_valid(tag_ent);
    uint32_t last_pick = 0;
    for (;;) {
        // The place at `tag` on tag_ent (else on *ref).
        if (has_tag) {
            if (on_tag_ent) {
                uint32_t tmp[3];
                tmp[0] = tmp[1] = tmp[2] = undefined_lane();
                slot<TagVec3>(0x0092261c)(tag_ent, tmp, &tag);  // GScr_GetTagOrigin
                origin.v[0] = tmp[0];
                origin.v[1] = tmp[1];
                origin.v[2] = tmp[2];
                uint32_t tmp2[3];
                tmp2[0] = tmp2[1] = tmp2[2] = undefined_lane();
                slot<TagVec3>(0x00922620)(tag_ent, tmp2, &tag);  // GScr_GetTagAngles
                angles.v[0] = tmp2[0];
                angles.v[1] = tmp2[1];
                angles.v[2] = tmp2[2];
            } else {
                uint32_t tmp[3];
                tmp[0] = tmp[1] = tmp[2] = undefined_lane();
                slot<TagVec3>(0x0092261c)(*ref, tmp, &tag);  // GScr_GetTagOrigin
                origin.v[0] = tmp[0];
                origin.v[1] = tmp[1];
                origin.v[2] = tmp[2];
                uint32_t tmp2[3];
                tmp2[0] = tmp2[1] = tmp2[2] = undefined_lane();
                slot<TagVec3>(0x00922620)(*ref, tmp2, &tag);  // GScr_GetTagAngles
                angles.v[0] = tmp2[0];
                angles.v[1] = tmp2[1];
                angles.v[2] = tmp2[2];
            }
        }

        // The variant: drawn again while it repeats the last (nonzero) one.
        uint32_t out;
        uint32_t pick = *BroScr_AnimLoopPickIndex(&out, copy_array(guys), anime);
        while (pick == last_pick && pick != 0) {
            uint32_t again;
            pick = *BroScr_AnimLoopPickIndex(&again, copy_array(guys), anime);
        }
        last_pick = pick;

        // Each guy: the variant's face anim, sound and scene anim, as in BroScr_AnimSingleGuys.
        int32_t last_anim = -1;
        int32_t last_face = -1;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(guys->size); ++i) {
            ScriptString sound;
            ScriptString_FromRep(&sound, nullptr);
            bool has_face = false;
            bool has_sound = false;
            bool has_anim = false;
            uint32_t face = 0;
            if (slot<uint32_t XB_CDECL(uint32_t)>(0x0092221c)(*element_at(guys, i - 1)) &&    // GScr_IsAI
                !slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(*element_at(guys, i - 1))) {  // GScr_IsAlive
                scr_release(&sound);
                scr_release(&tag);
                return;
            }
            bool test;
            {
                ScriptString animname;
                test = *variant_at(variant_array(0x5b0, own_address(&HashMap_Index_PROLOGUE_10), own_address(&HashMap_Index_PROLOGUE), element_at(guys, i - 1), &anime, &animname), pick,
                                   kReserveInt) != 0;
                scr_release(&animname);
            }
            if (test) {
                has_face = true;
                ScriptString animname;
                face = *variant_at(variant_array(0x5b0, own_address(&HashMap_Index_PROLOGUE_10), own_address(&HashMap_Index_PROLOGUE), element_at(guys, i - 1), &anime, &animname), pick,
                                   kReserveInt);
                scr_release(&animname);
            }
            {
                ScriptString animname;
                test = *variant_at(variant_array(0x634, kStringIndex, kStringArray, element_at(guys, i - 1), &anime, &animname),
                                   pick, kReserveString) != 0;
                scr_release(&animname);
            }
            if (test) {
                has_sound = true;
                ScriptString animname;
                ScriptString_Assign(&sound, reinterpret_cast<ScriptString*>(variant_at(
                                                variant_array(0x634, kStringIndex, kStringArray, element_at(guys, i - 1), &anime,
                                                              &animname),
                                                pick, kReserveString)));
                scr_release(&animname);
            }
            {
                ScriptString animname;
                test = *variant_at(variant_array(0x744, own_address(&HashMap_Index_PROLOGUE_10), own_address(&HashMap_Index_PROLOGUE), element_at(guys, i - 1), &anime, &animname), pick,
                                   kReserveInt) != 0;
                scr_release(&animname);
            }
            if (test) {
                has_anim = true;
                if (static_cast<uint8_t>(flag) != title_global<uint8_t>(0x00922ed0) && static_cast<uint8_t>(flag)) {
                    // At the guy's own place.
                    uint32_t buffer[3];
                    const uint32_t* o =
                        slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, *element_at(guys, i - 1));
                    origin.v[0] = o[0];
                    origin.v[1] = o[1];
                    origin.v[2] = o[2];
                    uint32_t buffer2[3];
                    const uint32_t* a =
                        slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer2, *element_at(guys, i - 1));
                    angles.v[0] = a[0];
                    angles.v[1] = a[1];
                    angles.v[2] = a[2];
                }
                {
                    ScriptString animname;
                    const uint32_t anim = *variant_at(
                        variant_array(0x744, own_address(&HashMap_Index_PROLOGUE_10), own_address(&HashMap_Index_PROLOGUE), element_at(guys, i - 1), &anime, &animname), pick, kReserveInt);
                    slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t)>(0x009224c0)(
                        *element_at(guys, i - 1), 0x7c70a61e, origin.v, angles.v, anim);  // GScr_AnimScriptedNoNotify
                    scr_release(&animname);
                }
                last_anim = static_cast<int32_t>(i - 1);
                drop_map_copy(element_at(guys, i - 1));
                {
                    const uint32_t* const guy = element_at(guys, i - 1);
                    const uint32_t owner = *ref;
                    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x1c);  // ScrIface_Alloc
                    if (functor) {
                        functor[0] = 0x00824428;
                        functor[1] = own_address(&BroScr_AnimNotetrackCommands);  // BroScr_AnimNotetrackCommands
                        functor[2] = owner;
                        functor[3] = *guy;
                        functor[4] = 0x7c70a61e;
                        functor[5] = tag_ent;
                        functor[6] = anime;
                    }
                    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                        0, 0, 0, owner, functor, 0);  // Scr_CreateThread
                }
                notify_thread_on(own_address(&BroScr_AnimNoteTracksThread), *element_at(guys, i - 1), 0x7c70a61e);  // BroScr_AnimNoteTracksThread
            }
            if (has_face || has_sound) {
                const uint32_t last = title_global<uint32_t>(0x00922ea4);
                ScriptString empty;
                ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
                ScriptString copy;
                if (has_anim) {
                    const uint32_t notify = title_global<uint32_t>(0x00922ee8);
                    ScriptString_Copy(&copy, &sound);
                    BroScr_StartAnimFaceWait(*element_at(guys, i - 1), face, copy, 0x3f800000, notify, empty, last);
                } else {
                    ScriptString_Copy(&copy, &sound);
                    BroScr_StartAnimFaceWait(*element_at(guys, i - 1), face, copy, 0x3f800000, 0x7c70a61e, empty, last);
                }
                last_face = static_cast<int32_t>(i - 1);
            }
            {
                ScriptString animname;
                ScrCall_ScrEnt_GetAnimname(element_at(guys, i - 1), &animname);
                BroScr_AddToNamedEntList(animname, anime);
            }
            scr_release(&sound);
        }

        // Wait for the variant's end on the remembered guy.
        if (last_anim != -1) {
            Scr_WaitTillArgs(*element_at(guys, static_cast<uint32_t>(last_anim)), 0x7c70a61e, 0x199a4, 0, 0);
        } else if (last_face != -1) {
            Scr_WaitTill(*element_at(guys, static_cast<uint32_t>(last_face)), 0x7c70a61e);
        }
    }
}

void XB_CDECL BroScr_AnimNotetrackCommands(uint32_t owner, uint32_t guy, uint32_t notify, uint32_t tag_ent, uint32_t anime) {
    using Attach = void XB_CDECL(uint32_t, ScriptString*, ScriptString*, uint32_t);
    using Detach = void XB_CDECL(uint32_t, ScriptString*, ScriptString*);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*, ScriptString*);
    // One notetrack thread per guy: the previous one (field notetrack_wait) is terminated.
    queue_notify(guy, 0x3621f548);
    slot<void XB_CDECL(uint32_t)>(0x009221a0)(*key_slot(guy, 0xd850f89e, 0));  // Scr_TerminateThread
    uint32_t* const wait = key_slot(guy, 0xd850f89e, 0);
    *wait = slot<uint32_t XB_CDECL()>(0x0092219c)();  // Scr_GetCurrentThreadHandle
    const uint32_t holder = entity_handle_is_valid(tag_ent) ? tag_ent : owner;
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(guy, notify,
                                                                                      &note);  // Scr_WaitTillOutputStr_ORAN
        bool faced = false;
        uint32_t note_hash;
        Scr_HashScriptString(&note_hash, reinterpret_cast<const uint8_t* const*>(&note));
        if (note_hash == 0x199a4) break;  // "end"
        uint32_t animhash;
        {
            ScriptString animname;
            Scr_HashScriptString(&animhash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(&guy, &animname)));
            scr_release(&animname);
        }
        // Every entry of the scene's notetrack table.
        for (uint32_t j = 0; static_cast<int32_t>(j) < static_cast<int32_t>(note_entries(&animhash, &anime)->size); ++j) {
            if (ScriptString_EqualsString(&note, field_inline(&animhash, &anime, j, 0x0082555c))) {  // "notetrack"
                if (field_inline(&animhash, &anime, j, 0x00825554)->rep) {  // "anime": only for that scene
                    uint32_t hash;
                    if (*Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(
                                                         field_inline(&animhash, &anime, j, 0x00825554))) != anime)
                        continue;
                }
                if (field_inline(&animhash, &anime, j, 0x00825544)->rep) {  // "attach gun left"
                    BroScr_AnimNotetrack_DeleteAttached(guy);
                } else if (field_inline(&animhash, &anime, j, 0x00825530)->rep) {  // "attach gun right"
                    BroScr_AnimNotetrack_DeleteAttached_2(guy);
                } else if (field_inline(&animhash, &anime, j, 0x00825524)->rep) {  // "detach gun"
                    BroScr_NotetrackDropGun(owner, guy, word_of(entry_inline(&animhash, &anime, j)));
                } else if (field_inline(&animhash, &anime, j, 0x00825518)->rep) {  // "swap from"
                    {
                        ScriptString* const model = string_field(guy, 0xe86f599a);
                        ScriptString* const tag = field_at(&animhash, &anime, j, 0x00825518);  // "swap from"
                        slot<Detach>(0x009224d8)(guy, model, tag);                               // GScr_Detach
                    }
                    ScriptString* const model = string_field(guy, 0xe86f599a);
                    ScriptString* const tag = field_at(&animhash, &anime, j, 0x0082550c);  // "self tag"
                    slot<Attach>(0x009224d0)(guy, model, tag, 0);                            // GScr_AttachWithOwner
                } else if (field_at(&animhash, &anime, j, 0x008254fc)->rep) {  // "attach model"
                    if (field_at(&animhash, &anime, j, 0x008254f4)->rep) {      // "selftag"
                        ScriptString* const tag = field_at(&animhash, &anime, j, 0x008254f4);
                        ScriptString* const model = field_at(&animhash, &anime, j, 0x008254fc);
                        slot<Attach>(0x009224d0)(guy, model, tag, 0);  // GScr_AttachWithOwner
                    } else {
                        ScriptString* const tag = field_at(&animhash, &anime, j, 0x008254f0);  // "tag"
                        ScriptString* const model = field_at(&animhash, &anime, j, 0x008254fc);
                        slot<Attach>(0x009224d0)(holder, model, tag, 0);  // GScr_AttachWithOwner
                    }
                } else {
                    if (field_at(&animhash, &anime, j, 0x008254e0)->rep) {  // "detach model"
                        if (field_at(&animhash, &anime, j, 0x008254f4)->rep) {  // "selftag"
                            ScriptString* const tag = field_at(&animhash, &anime, j, 0x008254f4);
                            ScriptString* const model = field_at(&animhash, &anime, j, 0x008254e0);
                            slot<Detach>(0x009224d8)(guy, model, tag);  // GScr_Detach
                        } else {
                            ScriptString* const tag = field_at(&animhash, &anime, j, 0x008254f0);  // "tag"
                            ScriptString* const model = field_at(&animhash, &anime, j, 0x008254e0);
                            slot<Detach>(0x009224d8)(holder, model, tag);  // GScr_Detach
                        }
                    }
                    if (field_at(&animhash, &anime, j, 0x008254d8)->rep) {  // "sound"
                        ScriptString none;
                        ScriptString_FromRep(&none, nullptr);
                        ScriptString animname;
                        const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(&guy, &animname);
                        const uint32_t key = 0x070659a4;
                        uint32_t hash;
                        ScriptString* const alias = note_field(
                            entry_at(Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name)), &anime, j), &key);
                        own_function<StartThread>(&Scr_StartThread_ent_str_str_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_PlaySoundOnTag), guy, alias,
                                                                &none);  // Scr_StartThread_ent_str_str(BroScr_PlaySoundOnTag)
                        scr_release(&animname);
                        scr_release(&none);
                    }
                    if (!faced) {
                        if (field_key(&animhash, &anime, j, 0x84f611ff)->rep) {
                            ScriptString name;
                            scr_string_from(&name, 0x0081093c);  // "dialogue"
                            BroScr_NotetrackFaceAnim(guy, anime, j, name);
                            faced = true;
                        } else if (field_key(&animhash, &anime, j, 0xcea13d87)->rep) {
                            ScriptString name;
                            scr_string_from(&name, 0x008254d0);  // "dialog"
                            BroScr_NotetrackFaceAnim(guy, anime, j, name);
                            faced = true;
                        }
                    }
                    if (field_at(&animhash, &anime, j, 0x00824ebc)->rep) {  // "create model"
                        BroScr_CreateModel(guy, word_of(entry_at(&animhash, &anime, j)));
                    } else if (field_at(&animhash, &anime, j, 0x008254c0)->rep) {  // "delete model"
                        BroScr_DeleteModel(guy, word_of(entry_at(&animhash, &anime, j)));
                    }
                    // Reads whose results the script drops (they add the keys).
                    if (field_key(&animhash, &anime, j, 0x0cf2dc5b)->rep) field_key(&animhash, &anime, j, 0xcc529814);
                    if (field_key(&animhash, &anime, j, 0x0001dc47)->rep) field_key(&animhash, &anime, j, 0xcc529814);
                }
            } else {
                // Not this entry's notetrack: the built-in ones.
                uint32_t hash;
                if (note_hash == *Scr_HashName(&hash, pointer<const char>(0x008254ac))) {  // "anim_gunhand = left"
                    BroScr_PutGunInHand(guy, 0x003798b4);
                } else if (note_hash == *Scr_HashName(&hash, pointer<const char>(0x00825494))) {  // "anim_gunhand = right"
                    BroScr_PutGunInHand(guy, 0x07151174);
                } else if (note_hash == *Scr_HashName(&hash, pointer<const char>(0x00825488))) {  // "lookat = on"
                    BroScr_AnimNotetrack_Alert(guy, title_global<uint32_t>(0x00922eec), 10000);
                } else if (note_hash == *Scr_HashName(&hash, pointer<const char>(0x00825478))) {  // "lookat = off"
                    BroScr_AnimNotetrack_Alert(guy, guy, 0);
                }
            }
        }
    }
    scr_release(&note);
}

namespace {

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

void XB_CDECL BroScr_AnimSingleAtNode(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t place,
                                      uint32_t tag_ent, uint32_t flag) {
    if (static_cast<uint8_t>(flag)) {
        single_guys(ent, guys, a3, &anime, lanes(), tag_ent, flag, lanes());
    } else if (node_valid(place)) {
        const ScrVec3 origin = get_vector(0x00922ab8, place);  // ScrPathNode_GetOrigin
        const ScrVec3 angles = get_vector(0x00922ac0, place);  // ScrPathNode_GetAngles
        single_guys(ent, guys, a3, &anime, origin, tag_ent, flag, angles);
    } else if (ent == level_handle()) {
        single_guys(ent, guys, a3, &anime, lanes(), tag_ent, 1, lanes());
    } else {
        uint32_t b1[3];
        const uint32_t* o = ScrCall_ScrEnt_GetOrigin(&ent, b1);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t b2[3];
        const uint32_t* a = ScrCall_ScrEnt_GetAngles(&ent, b2);
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        single_guys(ent, guys, a3, &anime, origin, tag_ent, flag, angles);
    }
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtEnt(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t place,
                                     uint32_t tag_ent, uint32_t flag) {
    if (static_cast<uint8_t>(flag)) {
        single_guys(ent, guys, a3, &anime, lanes(), tag_ent, flag, lanes());
    } else if (entity_handle_is_valid(place)) {
        const ScrVec3 origin = get_vector(0x00922b20, place);  // ScrEnt_GetOrigin
        const ScrVec3 angles = get_vector(0x00922ba0, place);  // ScrEnt_GetAngles
        single_guys(ent, guys, a3, &anime, origin, tag_ent, flag, angles);
    } else if (ent == level_handle()) {
        single_guys(ent, guys, a3, &anime, lanes(), tag_ent, 1, lanes());
    } else {
        uint32_t b1[3];
        const uint32_t* o = ScrCall_ScrEnt_GetOrigin(&ent, b1);
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t b2[3];
        const uint32_t* a = ScrCall_ScrEnt_GetAngles(&ent, b2);
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        single_guys(ent, guys, a3, &anime, origin, tag_ent, flag, angles);
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
