#include "bro_scr_world.h"

#include "bro_scr_common.h"
#include "g_script.h"
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

}  // namespace game
