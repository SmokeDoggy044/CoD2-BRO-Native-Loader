#include "bro_scr_world.h"

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "scr_access.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "title_data.h"

#include <cstring>

namespace game {

uint32_t* XB_CDECL BroScr_GetLevelVecOrZero(uint32_t* out, ScriptString name) {
    if (name.rep == nullptr) {
        out[0] = 0;  // 0.0f
        out[1] = 0;
        out[2] = 0;
    } else {
        uint32_t hash;
        const uint32_t* key = title_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(0x0011d8b0)(&hash, &name);  // Scr_HashScriptString
        HashMap* map = reinterpret_cast<HashMap*>(title_global<uint32_t>(0x00922ee0) + 0x3b4);
        const uint32_t* v = reinterpret_cast<const uint32_t*>(HashMap_Index(map, key));
        const uint32_t w0 = v[0], w1 = v[1], w2 = v[2];
        const float u = title_global<float>(0x00922eb8);  // g_scrUndefinedVecLane
        float f[3];
        std::memcpy(f, v, sizeof(f));
        if (!(f[0] == u) || !(f[1] == u) || !(f[2] == u)) {
            out[0] = w0;
            out[1] = w1;
            out[2] = w2;
        } else {
            out[0] = 0;
            out[1] = 0;
            out[2] = 0;
        }
    }
    scr_release(&name);
    return out;
}

namespace {

uint32_t level() { return title_global<uint32_t>(0x00922ee0); }

// HashMap_Index_PROLOGUE_11 (0x00461120): a string map's value for the key word.
ScriptString* string_map_index(uint32_t map, const uint32_t* key) {
    return reinterpret_cast<ScriptString*>(title_function<uint32_t XB_THISCALL(uint32_t, const uint32_t*)>(0x00461120)(map, key));
}

void assign_cstring(ScriptString* string, uint32_t text) {
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(string, text);  // ScriptString_AssignCString
}

uint32_t* hash_name(uint32_t* out, uint32_t name) {
    return title_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x0011d890)(out, name);  // Scr_HashName
}

// A thread (functor {0x007f5ba0, function, ent} from ScrIface_Alloc, null when it fails;
// Scr_CreateThread) on every entity whose targetname is `name` (Scr_GetEntArray, key 0x0d786b4c).
// Skipped when there are none (count == 0, or <= 0 when `signed_gate`); before the loop the level
// effect map's (+0x57c) entry `fx_key` is set to `fx` when given. Entries are read through the
// array's growing accessor (DynArray_Resize past the count).
void threads_on_targetname(uint32_t name, bool signed_gate, uint32_t fx_key, uint32_t fx, uint32_t function) {
    DynArray ents{nullptr, 0, 0};
    ScriptString targetname;
    scr_string_from(&targetname, name);
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&targetname)), 0x0d786b4c,
                    reinterpret_cast<uint8_t*>(&ents), 0);
    scr_release(&targetname);
    const bool any = signed_gate ? static_cast<int32_t>(ents.size) > 0 : ents.size != 0;
    if (any) {
        if (fx) {
            uint32_t key = fx_key;
            assign_cstring(string_map_index(level() + 0x57c, &key), fx);
        }
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
            if (i >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), i + 1, i + 4);
            const uint32_t ent = ents.data[i];
            uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0xc));  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f5ba0;
                functor[1] = function;
                functor[2] = ent;
            }
            using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
            scr_slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
        }
    }
    if (ents.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(ents.data);  // Scr_Free
}

}  // namespace

void XB_CDECL BroScr_TreeBurstModelFor() {
    const uint32_t offset[3] = {0, 0, title_global<uint32_t>(0x00824fec)};  // (0, 0, 68)
    uint32_t key;
    const uint32_t* hash = hash_name(&key, 0x00824fcc);  // "p_env_oak_grn_med_b_d_anim_stnd"
    uint32_t* v = reinterpret_cast<uint32_t*>(HashMap_Index(reinterpret_cast<HashMap*>(level() + 0x3b4), hash));
    v[0] = offset[0];
    v[1] = offset[1];
    v[2] = offset[2];
    hash = hash_name(&key, 0x00824fcc);
    assign_cstring(string_map_index(level() + 0x388, hash), 0x00824fb8);  // "p_env_oak_grn_med_b"
}

void XB_CDECL BroScr_TreeBurstInit() {
    uint32_t key = 0x8c1b319d;
    assign_cstring(string_map_index(level() + 0x238, &key), 0x007f4954);  // "treeburst"
    key = 0xf815b725;
    assign_cstring(string_map_index(level() + 0x604, &key), 0x008250a0);  // "artillery_treeburst"
    BroScr_TreeBurstModelFor();
    const uint32_t object = level();
    const uint32_t field = object ? object + 4 : 0;
    const uint32_t value = *reinterpret_cast<volatile uint32_t*>(field);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<StartThread>(0x0011a910)(0, 0, 0, 0, 0x00165960, value);  // Scr_StartThreadFunctor1(BroScr_SetupTreeBursts)
}

void XB_CDECL BroScr_GetWindowBreaks() { threads_on_targetname(0x00824414, false, 0, 0, 0x001642b0); }  // "window_break", BroScr_WindowBreakThink

void XB_CDECL BroScr_StoneBreakFx() {
    threads_on_targetname(0x00825394, false, 0xdcb4052e, 0x00825380, 0x0015cee0);  // "stone_break", "impact_lmg_concrete", BroScr_StoneBreakTriggerThink
}

void XB_CDECL BroScr_MinefieldTriggers() {
    threads_on_targetname(0x00825400, true, 0x7857af8e, 0x007f50f0, 0x0015ff60);  // "minefield", "artillery_generic", BroScr_MinefieldTriggerThink
}

ScriptString* XB_CDECL Bro__treeburst_gmi__tree_burst_getUndamagedModel(ScriptString* out, ScriptString name) {
    using StringOp = ScriptString* XB_THISCALL(ScriptString*, const ScriptString*);
    ScriptString model;
    scr_string_from(&model, 0x00824fb8);  // "p_env_oak_grn_med_b"
    ScriptString value;
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d5d0)(&value, 0);  // ScriptString_FromRep(null)
    ScriptString text;
    scr_string_from(&text, 0x00824ff8);  // "..._getUndamagedModel(): modelname is undefined!  ret..."
    ScriptString joined;
    ScriptString warning;
    using Concat = ScriptString* XB_CDECL(ScriptString*, const ScriptString*, const ScriptString*);
    using ConcatC = ScriptString* XB_CDECL(ScriptString*, const ScriptString*, uint32_t);
    title_function<ConcatC>(0x0011eec0)(&warning, title_function<Concat>(0x0011e340)(&joined, &text, &model),
                                        0x00824ff0);  // ScriptString_ConcatCString(ScriptString_Concat(text, model), ..)
    scr_release(&joined);
    scr_release(&text);
    if (name.rep == nullptr) {
        title_function<StringOp>(0x0011d5e0)(out, &model);  // ScriptString_Copy
    } else {
        uint32_t hash;
        const uint32_t* key = title_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(0x0011d8b0)(&hash, &name);  // Scr_HashScriptString
        using Index = ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
        const ScriptString* found = title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x388, key);  // HashMap_Index
        title_function<StringOp>(0x0011d600)(&value, found);  // ScriptString_Assign
        title_function<StringOp>(0x0011d5e0)(out, value.rep ? &value : &model);  // ScriptString_Copy
    }
    scr_release(&warning);
    scr_release(&value);
    scr_release(&model);
    scr_release(&name);
    return out;
}

namespace {

uint32_t* array_at(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data + i;
}

uint32_t ent_array(ScriptString* name) {
    using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    return scr_slot<GetEnt>(0x009221c4)(name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray (by targetname)
}

}  // namespace

void XB_CDECL BroScr_WindowBreakThink(uint32_t trigger) {
    ScriptString temp;
    const bool no_target = ScrCall_ScrEnt_GetTarget(&trigger, &temp)->rep == nullptr;
    scr_release(&temp);
    if (no_target) {
        scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(trigger);  // GScr_Delete
        return;
    }
    uint32_t window = ent_array(ScrCall_ScrEnt_GetTarget(&trigger, &temp));
    scr_release(&temp);
    if (!scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(window)) {  // Scr_EntityHandleIsValid
        scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(trigger);
        return;
    }
    // The window's own target (a clip made non-solid with it).
    uint32_t clip = 0;
    bool has_clip = true;
    {
        ScriptString target1;
        ScriptString target2;
        bool second = false;
        bool missing = true;
        if (ScrCall_ScrEnt_GetTarget(&window, &target1)->rep != nullptr) {
            second = true;
            clip = ent_array(ScrCall_ScrEnt_GetTarget(&window, &target2));
            missing = !scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(clip);
        }
        if (second) scr_release(&target2);
        scr_release(&target1);
        if (missing) has_clip = false;
    }
    scr_slot<void XB_CDECL(uint32_t)>(0x00922644)(trigger);  // GScr_SetEntFlag40000
    scr_slot<void XB_CDECL(uint32_t)>(0x00922650)(window);   // GScr_SetEntFlag80000
    const bool named = ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &temp)->rep != nullptr;
    scr_release(&temp);
    DynArray touching = {nullptr, 0, 0};
    DynArray origins = {nullptr, 0, 0};
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        title_function<uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t)>(0x0011f5d0)(&classname, 0xa106e9bd, &origins,
                                                                                                         0);  // Scr_GetEntArray
        scr_release(&classname);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(origins.size); ++i) {
        if (!scr_slot<uint8_t XB_CDECL(uint32_t, uint32_t)>(0x00922530)(trigger, *array_at(&origins, i))) continue;  // GScr_IsTouching
        if (named) {
            // targetname == the trigger's script_noteworthy; the result is not used.
            ScriptString noteworthy;
            const ScriptString* note = ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy);
            ScriptString targetname;
            ScriptString_EqualsString(ScrCall_ScrEnt_GetTargetname(array_at(&origins, i), &targetname), note);
            scr_release(&targetname);
            scr_release(&noteworthy);
        }
        DynArray_PushBack(reinterpret_cast<uint8_t*>(&touching), array_at(&origins, i));
    }
    if (touching.size == 0) {
        // None: one script_origin at the trigger.
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);
        const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
        uint32_t out[3] = {lane, lane, lane};
        scr_slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(trigger, out);  // GScr_GetOrigin
        const uint32_t at[3] = {out[0], out[1], out[2]};
        uint32_t spawned = scr_slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
        DynArray_PushBack(reinterpret_cast<uint8_t*>(&touching), &spawned);
        scr_release(&classname);
    }
    scr_slot<void XB_CDECL(uint32_t)>(0x00922564)(window);  // GScr_ClearEntFlag400
    if (has_clip) {
        scr_slot<void XB_CDECL(uint32_t)>(0x00922568)(clip);  // GScr_SetEntFlag400
        scr_slot<void XB_CDECL(uint32_t)>(0x009227c0)(clip);  // GScr_MoverNotSolid
    }
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*, DynArray*);
    title_function<StartThread>(0x0017ccc0)(0, 0, 0, 0, 0x0015e4c0, trigger, &window, &clip, &touching);  // Scr_StartThreadFunctor_20_3
    if (origins.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(origins.data);  // Scr_Free
    if (touching.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(touching.data);
}

namespace {

// Script thread with a bool: functor {0x007f4adc, function, ent, flag byte}.
void start_thread_flag(uint32_t ent, uint32_t function, uint8_t flag) {
    uint8_t* functor = static_cast<uint8_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x10));  // ScrIface_Alloc
    if (functor) {
        reinterpret_cast<uint32_t*>(functor)[0] = 0x007f4adc;
        reinterpret_cast<uint32_t*>(functor)[1] = function;
        reinterpret_cast<uint32_t*>(functor)[2] = ent;
        functor[0xc] = flag;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);
}

// The trees named `name`: field 0x4c7e3571 = the running number, then the BroScr_TreeBurstSetup
// and BroScr_TreeBurstTriggerThink threads with `flag`.
void setup_tree_group(uint32_t name, uint8_t flag, uint32_t* number) {
    DynArray trees = {nullptr, 0, 0};
    {
        ScriptString targetname;
        scr_string_from(&targetname, name);
        title_function<uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t)>(0x0011f5d0)(&targetname, 0x0d786b4c,
                                                                                                         &trees, 0);  // Scr_GetEntArray
        scr_release(&targetname);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(trees.size); ++i) {
        *BroScr_FieldSlot(*array_at(&trees, i), 0x4c7e3571) = *number;
        start_thread_flag(*array_at(&trees, i), 0x0015d7b0, flag);  // BroScr_TreeBurstSetup
        start_thread_flag(*array_at(&trees, i), 0x001640c0, flag);  // BroScr_TreeBurstTriggerThink
        ++*number;
    }
    if (trees.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(trees.data);  // Scr_Free
}

}  // namespace

void XB_CDECL BroScr_SetupTreeBursts() {
    // The original frees the first array at the very end (after the second group).
    uint32_t number = 0;
    DynArray trees = {nullptr, 0, 0};
    {
        ScriptString targetname;
        scr_string_from(&targetname, 0x00825420);  // "tree_burst"
        title_function<uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t)>(0x0011f5d0)(&targetname, 0x0d786b4c,
                                                                                                         &trees, 0);
        scr_release(&targetname);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(trees.size); ++i) {
        *BroScr_FieldSlot(*array_at(&trees, i), 0x4c7e3571) = number;
        start_thread_flag(*array_at(&trees, i), 0x0015d7b0, 1);
        start_thread_flag(*array_at(&trees, i), 0x001640c0, 1);
        ++number;
    }
    setup_tree_group(0x0082540c, 0, &number);  // "tree_burst_scripted"
    if (trees.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(trees.data);
}

namespace {

using CopyString = void XB_THISCALL(ScriptString*, const ScriptString*);

void from_int(ScriptString* out, uint32_t value) { title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011df90)(out, value); }  // ScriptString_FromInt2

// A slot setter (ScrEnt_SetTargetname / SetTarget / SetAnimname) with a copy of `value`.
void set_string(uint32_t slot, uint32_t ent, const ScriptString* value) {
    ScriptString copy;
    title_function<CopyString>(0x0011d5e0)(&copy, value);  // ScriptString_Copy
    scr_slot<void XB_CDECL(uint32_t, ScriptString)>(slot)(ent, copy);
}

// GScr_UseAnimTree(ent, the level's tree for its animname); the animname goes to `temp`.
void use_animname_tree(uint32_t* ent, ScriptString* temp) {
    const ScriptString* name = ScrCall_ScrEnt_GetAnimname(ent, temp);
    uint32_t hash;
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
    using Index = ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(
        *ent, title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x238, &hash));  // GScr_UseAnimTree
    scr_release(temp);
}

// model = Bro__treeburst_gmi__tree_burst_getUndamagedModel(ent's model) into `temp`.
void undamaged_model(ScriptString* model, uint32_t* ent, ScriptString* temp) {
    ScriptString current;
    ScrCall_ScrEnt_GetModel(ent, &current);
    title_function<ScriptString* XB_CDECL(ScriptString*, ScriptString)>(0x0015d650)(temp, current);
    title_function<CopyString>(0x0011d600)(model, temp);  // ScriptString_Assign
    scr_release(temp);
}

uint32_t spawn_model_at(ScriptString* temp, uint32_t ent) {
    scr_string_from(temp, 0x0083ab98);  // "script_model"
    using GetOrigin = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t buffer[3];
    const uint32_t* o = scr_slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const uint32_t spawned = scr_slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(temp, origin, 0);  // GScr_Spawn
    scr_release(temp);
    return spawned;
}

void copy_angles(uint32_t to, uint32_t from) {
    using GetAngles = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t buffer[3];
    const uint32_t* a = scr_slot<GetAngles>(0x00922ba0)(buffer, from);
    scr_slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(to, ScrVec3{{a[0], a[1], a[2]}});  // ScrEnt_SetAngles
}

}  // namespace

void XB_CDECL BroScr_TreeBurstSetup(uint32_t tree, uint32_t per_target) {
    // The original uses the flag's argument slot for its string temporaries.
    ScriptString* const temp = reinterpret_cast<ScriptString*>(&per_target);
    ScriptString model;
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d5d0)(&model, 0);  // ScriptString_FromRep(null)
    if (static_cast<uint8_t>(per_target)) {
        // A dying copy of each target: script_model "<number>tree_to_die_<i>" with the undamaged
        // model, the target's angles, targeting the original, which becomes a "treeburst".
        DynArray targets = {nullptr, 0, 0};
        title_function<uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t)>(0x0011f5d0)(
            ScrCall_ScrEnt_GetTarget(&tree, temp), 0x0d786b4c, &targets, 0);  // Scr_GetEntArray
        scr_release(temp);
        const bool has_target = ScrCall_ScrEnt_GetTarget(&tree, temp)->rep != nullptr;
        scr_release(temp);
        if (has_target) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
                uint32_t spawned = spawn_model_at(temp, *array_at(&targets, i));
                ScriptString undamaged;
                undamaged_model(&model, array_at(&targets, i), &undamaged);
                scr_slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(spawned, &model, 0);  // GScr_SetModel
                ScriptString number;
                from_int(&number, *BroScr_FieldSlot(tree, 0x4c7e3571));
                ScriptString index;
                from_int(&index, i);
                ScriptString prefix;
                ScriptString_ConcatCString(&prefix, &number, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00825060)));  // "tree_to_die_"
                ScriptString name;
                ScriptString_Concat(&name, &prefix, &index);
                set_string(0x00922b54, spawned, &name);  // ScrEnt_SetTargetname
                scr_release(&name);
                scr_release(&prefix);
                copy_angles(spawned, *array_at(&targets, i));
                ScriptString targetname;
                const ScriptString* target = ScrCall_ScrEnt_GetTargetname(&spawned, &targetname);
                set_string(0x00922b4c, *array_at(&targets, i), target);  // ScrEnt_SetTarget
                scr_release(&targetname);
                ScriptString anim;
                scr_string_from(&anim, 0x007f4954);  // "treeburst"
                set_string(0x00922c44, *array_at(&targets, i), &anim);  // ScrEnt_SetAnimname
                scr_release(&anim);
                ScriptString animname;
                use_animname_tree(array_at(&targets, i), &animname);
                scr_slot<void XB_CDECL(uint32_t)>(0x00922568)(*array_at(&targets, i));  // GScr_SetEntFlag400
                scr_release(&index);
                scr_release(&number);
            }
        }
        if (targets.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(targets.data);  // Scr_Free
    } else {
        // A dying copy of the tree itself: "<number>tree_to_die_0".
        uint32_t spawned = spawn_model_at(temp, tree);
        undamaged_model(&model, &tree, temp);
        scr_slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(spawned, &model, 0);
        ScriptString number;
        from_int(&number, *BroScr_FieldSlot(tree, 0x4c7e3571));
        ScriptString prefix;
        ScriptString_ConcatCString(&prefix, &number, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00825060)));
        set_string(0x00922b54, spawned,
                   title_function<ScriptString* XB_CDECL(ScriptString*, const ScriptString*, uint32_t)>(0x0011b1c0)(temp, &prefix, 0));  // ScriptString_PlusInt
        scr_release(temp);
        scr_release(&prefix);
        copy_angles(spawned, tree);
        set_string(0x00922b4c, tree, ScrCall_ScrEnt_GetTargetname(&spawned, temp));
        scr_release(temp);
        scr_string_from(temp, 0x007f4954);  // "treeburst"
        set_string(0x00922c44, tree, temp);
        scr_release(temp);
        use_animname_tree(&tree, temp);
        scr_slot<void XB_CDECL(uint32_t)>(0x00922568)(tree);  // GScr_SetEntFlag400
        scr_release(&number);
    }
    scr_release(&model);
}

}  // namespace game
