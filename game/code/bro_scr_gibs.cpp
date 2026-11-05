#include "bro_scr_gibs.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A script-interface slot (pScr_*: a word in the import block at 0x00922xxx) called as F.
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t bits_of(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);

int32_t random_int(int32_t limit) { return slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(limit); }  // Scr_RandomInt
long double random_float(uint32_t limit) { return slot<long double XB_CDECL(uint32_t)>(0x00922230)(limit); }  // Scr_RandomFloat (ST0)

// Element `i` of a local entity array, grown to i + 1 (capacity i + 4) first when too short.
uint32_t* element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}

// GScr_SetModel(gib i, name): the name is built before the element is read (and the array grown).
void set_model_of(DynArray* gibs, uint32_t i, uint32_t name) {
    ScriptString model;
    ScriptString_FromCString(&model, pointer<const char>(name));
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(*element(gibs, i), &model, 0);  // GScr_SetModel
    ScriptString_Release(&model);
}

// (base + random - 0.5) per axis in x87 precision; z's random is used unrounded (ST0).
ScrVec3 jittered(float x_base, float rx, float y_base, float ry, float z_base, long double rz) {
    const long double half = platform_x87_ld(title_global<float>(0x0083eb24));  // 0.5
    return {{
        bits_of(static_cast<float>(platform_x87_ld(x_base) + rx - half)),
        bits_of(static_cast<float>(platform_x87_ld(y_base) + ry - half)),
        bits_of(static_cast<float>(rz + z_base - half)),
    }};
}

// Scr_CreateThread on `gib` with a functor {0x007f49e0, thread, gib, a, b, c} (ScrIface_Alloc).
void gib_thread(uint32_t thread, uint32_t gib, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t* functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x18);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f49e0;
        functor[1] = thread;
        functor[2] = gib;
        functor[3] = a;
        functor[4] = b;
        functor[5] = c;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, gib, functor,
                                                                                                    0);  // Scr_CreateThread
}

}  // namespace

void XB_CDECL BroScr_SpawnStoneGibs(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, bool stone_only) {
    DynArray gibs = {};
    const int32_t count = random_int(1) + 1;
    for (int32_t i = 0; i < count; ++i) {
        {
            ScriptString classname;
            ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
            const uint32_t zero[3] = {bits_of(0.0f), bits_of(0.0f), bits_of(0.0f)};
            const uint32_t gib = slot<Spawn>(0x009222f0)(&classname, zero, 0);  // GScr_Spawn
            *element(&gibs, i) = gib;
            ScriptString_Release(&classname);
        }
        const uint32_t kStone = 0x00824b74, kBrick = 0x00824b60;  // "xmodel/gib_stone1", "xmodel/gib_brick"
        set_model_of(&gibs, i, stone_only ? kStone : random_int(100) > 50 ? kBrick : kStone);
        uint32_t buffer[3];
        const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const float z_base = as_float(o[2]);
        o = slot<GetVec>(0x00922b20)(buffer, ent);
        const float y_base = as_float(o[1]);
        const float ry = static_cast<float>(random_float(0x3f800000));
        o = slot<GetVec>(0x00922b20)(buffer, ent);
        const float x_base = as_float(o[0]);
        const float rx = static_cast<float>(random_float(0x3f800000));
        const long double rz = random_float(0x3f800000);
        const ScrVec3 position = jittered(x_base, rx, y_base, ry, z_base, rz);
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(*element(&gibs, i), position);  // ScrEnt_SetOrigin
        gib_thread(0x00122b50, *element(&gibs, i), a2, a3, a4);  // BroScr_StoneGibLaunch
        gib_thread(0x0013e180, *element(&gibs, i), a2, a3, a4);  // BroScr_SpawnConcreteGibs
        gib_thread(0x0013e180, *element(&gibs, i), a2, a3, a4);
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    if (gibs.data) Scr_Free(gibs.data);
}

void XB_CDECL BroScr_SpawnWoodGibs(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, bool init_target) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, 0x06730ae8);  // Scr_QueueNotify
    DynArray gibs = {};
    const int32_t count = slot<int32_t XB_CDECL(int32_t, int32_t)>(0x00922234)(3, 4);  // Scr_RandomIntRange
    const auto spawned_count = []() { return pointer<int32_t>(title_global<uint32_t>(0x00922ee0) + 0x314); };
    for (int32_t i = 0; i < count; ++i) {
        if (*spawned_count() > title_global<int32_t>(0x008cd190)) continue;  // level gib limit
        {
            ScriptString classname;
            ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
            const uint32_t zero[3] = {bits_of(0.0f), bits_of(0.0f), bits_of(0.0f)};
            const uint32_t zone = slot<uint32_t XB_CDECL(uint32_t)>(0x00922498)(ent);  // GScr_GetEntZone
            const uint32_t gib = slot<Spawn>(0x009222f0)(&classname, zero, zone);  // GScr_Spawn
            *element(&gibs, i) = gib;
            ScriptString_Release(&classname);
        }
        ++*spawned_count();
        const int32_t roll = random_int(100);
        // "wood_gib_a" / "wood_gib_b" / "wood_gib_c"
        set_model_of(&gibs, i, roll > 66 ? 0x00824ba0 : roll > 33 ? 0x00824b94 : 0x00824b88);
        uint32_t buffer[3];
        const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const float z_base = as_float(o[2]);
        o = slot<GetVec>(0x00922b20)(buffer, ent);
        const float y_base = as_float(o[1]);
        const float ry = static_cast<float>(random_float(0x3f800000));
        o = slot<GetVec>(0x00922b20)(buffer, ent);
        const float x_base = as_float(o[0]);
        const float rx = static_cast<float>(random_float(0x3f800000));
        const long double rz = random_float(0x3f800000);
        const ScrVec3 position = jittered(x_base, rx, y_base, ry, z_base, rz);
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(*element(&gibs, i), position);  // ScrEnt_SetOrigin
        gib_thread(0x00123d70, *element(&gibs, i), a2, a3, a4);  // BroScr_WoodGibLaunch
        gib_thread(0x0013f990, *element(&gibs, i), a2, a3, a4);  // BroScr_SpawnWoodGibsSmall
    }
    if (init_target) {
        ScriptString target;
        const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&ent, &target);
        using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
        const uint32_t found = slot<GetEntArray>(0x009221c4)(name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
        ScriptString_Release(&target);
        if (slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(found)) {  // Scr_EntityHandleIsValid
            ScriptString targetname;
            const ScriptString* const own_name = ScrCall_ScrEnt_GetTargetname(&ent, &targetname);
            uint32_t buffer[3];
            const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);
            const uint32_t origin[3] = {o[0], o[1], o[2]};
            // Scr_StartThread_ent_vec_str (0x0045b780): BroScr_WoodGibInit(found, origin, ent's targetname)
            using Start = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                        const ScriptString*);
            title_function<Start>(0x0045b780)(0, 0, 0, 0, 0x00123f00, found, origin, own_name);
            ScriptString_Release(&targetname);
        }
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    if (gibs.data) Scr_Free(gibs.data);
}

}  // namespace game
