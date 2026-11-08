#include "bro_scr_flak88.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "levels_twins.h"
#include "bro_scr_death_watch.h"
#include "bro_scr_level_threads.h"
#include "bro_scr_level_threads_2.h"

namespace game {

using namespace bro_ai;

namespace {

// Copies of the bro_scr_level_threads.cpp helpers (BroScr_FlakPanzerThink's).
using CreateThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using GetVec = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
using Ent = void XB_CDECL(uint32_t);

inline uint32_t* new_functor(uint32_t size) {
    return slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(size);  // ScrIface_Alloc
}

inline void create_thread(uint32_t owner, uint32_t* functor) {
    slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
}

// Scr_CreateThread(owner, {0x007f4a1c, body, a, b}).
inline void thread_2(uint32_t owner, uint32_t body, uint32_t a, uint32_t b) {
    uint32_t* const functor = new_functor(0x10);
    if (functor) {
        functor[0] = 0x007f4a1c;
        functor[1] = body;
        functor[2] = a;
        functor[3] = b;
    }
    create_thread(owner, functor);
}

// Scr_CreateThread(owner, {0x007f4af4, body, a, b, c}).
inline void thread_3(uint32_t owner, uint32_t body, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t* const functor = new_functor(0x14);
    if (functor) {
        functor[0] = 0x007f4af4;
        functor[1] = body;
        functor[2] = a;
        functor[3] = b;
        functor[4] = c;
    }
    create_thread(owner, functor);
}

// Scr_CreateThread(owner, {0x007f9a78, body, a, a copy of *array (DynArray_Assign4)}).
inline void thread_array(uint32_t owner, uint32_t body, uint32_t a, const DynArray* array) {
    uint32_t* const functor = new_functor(0x18);
    if (functor) {
        functor[0] = 0x007f9a78;
        functor[1] = body;
        functor[2] = a;
        functor[3] = 0;
        functor[4] = 0;
        functor[5] = 0;
        own_function<void XB_THISCALL(uint32_t*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&functor[3], array);  // DynArray_Assign4_PROLOGUE
    }
    create_thread(owner, functor);
}

// The DynArray element at i, grown first when i is past the end (the scripts' checked index).
inline uint32_t& element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data[i];
}

// The level's byte / int array at `offset` [i], grown first (no null check on the level object).
inline uint8_t& level_byte(uint32_t offset, uint32_t i) {
    DynArrayBytes* const array = pointer<DynArrayBytes>(title_global<uint32_t>(0x00922ee0) + offset);
    if (i >= array->size) DynArray_ResizeBytes(array, i + 1, i + 4);
    return array->data[i];
}

inline uint32_t& level_int(uint32_t offset, uint32_t i) {
    DynArray* const array = pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + offset);
    if (i >= array->size) DynArray_ResizeInt(array, i + 1, i + 4);
    return array->data[i];
}

// The level entity's script key table (the level object read without a null check).
inline ScriptKeyTable* level_table() {
    return ent_script_object(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4));
}

// Whether the level's byte array key 0xe337f297 [flaknum] is 0 (read from a copy of the array).
inline bool flak_flag_clear(uint32_t flaknum) {
    DynArrayBytes copy;
    DynArrayBytes* const flags = static_cast<DynArrayBytes*>(
        ScrField_he337f297_GetArrayCopy(reinterpret_cast<uint8_t*>(level_table()) + 0x10, &copy));
    if (flaknum >= flags->size) DynArray_ResizeBytesNoFill(flags, flaknum + 1, flaknum + 4);
    const bool clear = flags->data[flaknum] == 0;
    if (copy.data) Scr_Free(copy.data);
    return clear;
}

// The entity's bool key, added as the undefined bool (zero-extended: the title pushes a word with
// stale upper bytes).
inline uint8_t* bool_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<uint8_t*>(key_slot(ent, key, title_global<uint8_t>(0x00922ed0)));
}

// The anim-set struct of field 0xc4813666 (ScriptKeyTable_GetStruct1c).
inline const uint32_t* flak_anims(uint32_t ent) {
    return pointer<uint32_t>(ScriptKeyTable_GetStruct1c(ent_script_object(ent), 0xc4813666));
}

// *out = the entity's origin (copied out of the call's buffer).
inline void origin_of(ScrVec3* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    out->v[0] = o[0];
    out->v[1] = o[1];
    out->v[2] = o[2];
}

inline void flak_sound(uint32_t ent, uint32_t text) {
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(text));
    slot<PlaySound>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
    ScriptString_Release(&sound);
}

// BroScr_PutGunInHand(guy, hand) for each guy (none when the array was never allocated).
inline void guns_in_hand(DynArray* guys, uint32_t hand) {
    if (!guys->capacity) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys->size); ++i)
        BroScr_PutGunInHand(element(guys, i), hand);
}

// A BroScr_FlakCrewAnim thread {0x007ffb0c: guy, flak, flaknum, anim} on each guy (none when the array
// was never allocated).
inline void crew_anim_threads(DynArray* guys, uint32_t flak, uint32_t flaknum, const ScriptString* anim) {
    if (!guys->capacity) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys->size); ++i) {
        const uint32_t guy = element(guys, i);
        uint32_t* const functor = new_functor(0x18);
        if (functor) {
            functor[0] = 0x007ffb0c;
            functor[1] = own_address(&BroScr_FlakCrewAnim);  // BroScr_FlakCrewAnim
            functor[2] = guy;
            functor[3] = flak;
            functor[4] = flaknum;
            ScriptString_Copy(reinterpret_cast<ScriptString*>(&functor[5]), anim);
        }
        create_thread(guy, functor);
    }
}

// Scr_WaitTill(level, "flak_anim_done" + flaknum) (the text left in *done).
inline void wait_flak_anim_done(ScriptString* done, uint32_t flaknum) {
    ScriptString_AssignCString(done, pointer<const char>(0x00824a44));  // "flak_anim_done"
    ScriptString_AppendInt(done, static_cast<int32_t>(flaknum));
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    uint32_t hash;
    const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(done));
    Scr_WaitTill(*level, h);
}

inline bool string_is(const ScriptString* s, uint32_t text) { return ScriptString_Equals(s, pointer<const char>(text)); }

}  // namespace

void XB_CDECL BroScr_Flak88Think(uint32_t ent, uint32_t flaknum) {
    using IsAlive = uint32_t XB_CDECL(uint32_t);
    using SetModel = void XB_CDECL(uint32_t, ScriptString*, uint32_t);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*);
    // The flak type: string field 0xd1c52a4f when defined.
    ScriptString type;
    ScriptString_FromRep(&type, nullptr);
    bool defined;
    if (*ScrField_hd1c52a4f_IsDefined(&defined, ent)) ScriptString_Assign(&type, ScrField_hd1c52a4f(ent));
    {
        uint32_t* const functor = new_functor(0x14);
        if (functor) {
            functor[0] = 0x008023c8;
            functor[1] = own_address(&BroScr_FlakDeathThink);  // BroScr_FlakDeathThink
            functor[2] = ent;
            functor[3] = flaknum;
            *reinterpret_cast<uint8_t*>(&functor[4]) = 1;
        }
        create_thread(ent, functor);
    }
    // The flak's targets (when it has a target): start triggers (BroScr_FlakWaitStartTrigger threads,
    // then a wait for "start_turret"), script models (BroScr_PlantBombObjective threads), the rest the
    // crew spawners.
    DynArray targets = {nullptr, 0, 0};
    bool targeted;
    {
        ScriptString target;
        targeted = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
        ScriptString_Release(&target);
    }
    if (targeted) {
        ScriptString target;
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))),
                        0x0d786b4c, reinterpret_cast<uint8_t*>(&targets), 0);  // by targetname
        ScriptString_Release(&target);
    }
    bool start_trigger = false;
    DynArray spawners = {nullptr, 0, 0};
    if (targets.capacity) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
            bool is;
            {
                ScriptString classname;
                is = string_is(ScrCall_ScrEnt_GetClassname(&element(&targets, i), &classname), 0x00832270);  // "trigger_multiple"
                ScriptString_Release(&classname);
            }
            if (is) {
                thread_3(ent, own_address(&BroScr_FlakWaitStartTrigger), ent, element(&targets, i), flaknum);  // BroScr_FlakWaitStartTrigger
                start_trigger = true;
                continue;
            }
            {
                ScriptString classname;
                is = string_is(ScrCall_ScrEnt_GetClassname(&element(&targets, i), &classname), 0x0083ab98);  // "script_model"
                ScriptString_Release(&classname);
            }
            if (is)
                title_function<StartThread>(0x0045ad90)(0, 0, 0, 0, own_address(&BroScr_PlantBombObjective), ent, &element(&targets, i),
                                                        &flaknum);  // Scr_StartThread_ent_ent_i(BroScr_PlantBombObjective)
            else
                DynArray_PushBack(reinterpret_cast<uint8_t*>(&spawners), &element(&targets, i));
        }
        if (start_trigger) Scr_WaitTillString(ent, pointer<const char>(0x00825330));  // "start_turret"
    }
    // The crew: one guy per spawner (when there are spawners), all of them alive.
    DynArray guys = {nullptr, 0, 0};
    bool crewed = true;
    if (spawners.capacity) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
            const uint32_t guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922518)(element(&spawners, i), 0);  // GScr_DoSpawn
            element(&guys, i) = guy;
        }
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
            if (!slot<IsAlive>(0x00922210)(element(&guys, i))) {  // GScr_IsAlive
                crewed = false;
                break;
            }
        }
        if (crewed && static_cast<int32_t>(guys.size) < static_cast<int32_t>(spawners.size)) crewed = false;
    }
    if (crewed) {
        // Type "controlled" (compared twice, the first result unused): the flak 88 turret model.
        string_is(&type, 0x008251ec);  // "controlled"
        if (string_is(&type, 0x008251ec)) {
            ScriptString model;
            ScriptString_FromCString(&model, pointer<const char>(0x0082521c));  // "xmodel/turret_flak88"
            slot<SetModel>(0x00922548)(ent, &model, 0);  // GScr_SetModel
            ScriptString_Release(&model);
        }
        // Manned (level bytes +0x684), the level flag array key 0xe337f297 and bytes +0x548 cleared.
        level_byte(0x684, flaknum) = 1;
        {
            ScriptKeyTable* const table = level_table();
            uint32_t* value = ScriptKeyTable_Find(table, 0xe337f297);
            if (!value) {
                const uint32_t empty[3] = {0, 0, 0};
                value = ScriptKeyTable_SetArray_4(table, 0xe337f297, empty);
            }
            DynArrayBytes* const flags = pointer<DynArrayBytes>(*value);
            if (flaknum >= flags->size) DynArray_ResizeBytesNoFill(flags, flaknum + 1, flaknum + 4);
            flags->data[flaknum] = 0;
        }
        level_byte(0x548, flaknum) = 0;
        // The flak's setup function (key 0xc511e266: flak, type, the anim set 0xc4813666).
        {
            using Setup = void XB_CDECL(uint32_t, ScriptString, const uint32_t*);
            const uint32_t* const anims = flak_anims(ent);
            ScriptString copy;
            ScriptString_Copy(&copy, &type);
            title_function<Setup>(*key_slot(ent, 0xc511e266, 0))(ent, copy, anims);
        }
        // Per guy: gun in hand, the crew think (function key 0x2f31323b: guy, index), a
        // BroScr_FlakStopMovingOnDeath thread, the clip-off bool 0x1ea97196 cleared and a
        // BroScr_FlakWaitDeath thread {0x007f4b84: guy, the flak's origin, flaknum}.
        if (guys.capacity) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
                BroScr_PutGunInHand(element(&guys, i), 0x0036b1a5);
                const uint32_t think = *key_slot(ent, 0x2f31323b, 0);
                ScriptString message;
                ScriptString_FromCString(&message, pointer<const char>(0x0082531c));  // "No function ptr!"
                ScrLocal_ReleaseString(ent, think != 0, message);
                const uint32_t guy = element(&guys, i);
                thread_2(guy, think, guy, i);
                thread_3(ent, own_address(&BroScr_FlakStopMovingOnDeath), ent, element(&guys, i), flaknum);  // BroScr_FlakStopMovingOnDeath
                *bool_slot(element(&guys, i), 0x1ea97196) = 0;
                ScrVec3 origin;
                origin_of(&origin, ent);
                const uint32_t waiter = element(&guys, i);
                uint32_t* const functor = new_functor(0x1c);
                if (functor) {
                    functor[0] = 0x007f4b84;
                    functor[1] = own_address(&BroScr_FlakWaitDeath);  // BroScr_FlakWaitDeath
                    functor[2] = waiter;
                    functor[3] = origin.v[0];
                    functor[4] = origin.v[1];
                    functor[5] = origin.v[2];
                    functor[6] = flaknum;
                }
                create_thread(waiter, functor);
            }
        }
        level_int(0x50c, flaknum) = 0;
        level_int(0x2d4, flaknum) = 0;
        *bool_slot(ent, 0x34314719) = 0;
        *int_slot(ent, 0x872c50bf) = 0;
        if (guys.capacity) thread_array(ent, own_address(&BroScr_FlakLoaderAmmoAnim), ent, &guys);  // BroScr_FlakLoaderAmmoAnim
        thread_2(ent, own_address(&BroScr_FlakPanzerStopLoopSounds), ent, flaknum);  // BroScr_FlakPanzerStopLoopSounds
        thread_2(ent, own_address(&BroScr_FlakDied), ent, flaknum);  // BroScr_FlakDied
        // While manned: with the level flag clear, guns in hand and the round's anim ("preidle" while
        // level byte +0x548 is set, "turn" once, else a BroScr_FlakGunnerFireAnim thread), a wait for
        // "fire_turret", then "fire" for the crew; else guns on the back and 1 s.
        ScriptString anim, done;
        ScriptString_FromRep(&anim, nullptr);
        ScriptString_FromRep(&done, nullptr);
        while (level_byte(0x684, flaknum)) {
            if (!flak_flag_clear(flaknum)) {
                guns_in_hand(&guys, 0x07151174);
                Scr_Wait(1.0f);
                continue;
            }
            guns_in_hand(&guys, 0x0036b1a5);
            if (level_byte(0x548, flaknum)) {
                ScriptString_AssignCString(&anim, pointer<const char>(0x008252e8));  // "preidle"
            } else if (!level_int(0x2d4, flaknum)) {
                level_int(0x2d4, flaknum) = 1;
                ScriptString_AssignCString(&anim, pointer<const char>(0x008252e0));  // "turn"
            } else {
                thread_2(ent, own_address(&BroScr_FlakGunnerFireAnim), ent, flaknum);  // BroScr_FlakGunnerFireAnim
            }
            Scr_WaitTillString(ent, pointer<const char>(0x00824c90));  // "fire_turret"
            ScriptString_AssignCString(&anim, pointer<const char>(0x0082c09c));  // "fire"
            // A "fake" flak (script_noteworthy) only plays the sound; a real one fires.
            bool fake;
            {
                ScriptString noteworthy;
                if (ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep) {
                    ScriptString again;
                    fake = string_is(ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &again), 0x00825378);  // "fake"
                    ScriptString_Release(&again);
                } else {
                    fake = false;
                }
                ScriptString_Release(&noteworthy);
            }
            if (fake) {
                if (string_is(&anim, 0x0082c09c)) flak_sound(ent, 0x0082536c);  // "flak88_fire"
            } else if (string_is(&anim, 0x0082c09c)) {
                slot<Ent>(0x009228d4)(ent);  // GScr_Vehicle_FireWeapon
            }
            crew_anim_threads(&guys, ent, flaknum, &anim);
            wait_flak_anim_done(&done, flaknum);
        }
        ScriptString_Release(&done);
        ScriptString_Release(&anim);
    }
    if (guys.data) Scr_Free(guys.data);
    if (spawners.data) Scr_Free(spawners.data);
    if (targets.data) Scr_Free(targets.data);
    ScriptString_Release(&type);
}

}  // namespace game
