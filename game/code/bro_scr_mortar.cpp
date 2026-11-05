#include "bro_scr_mortar.h"

#include <cstring>

#include "bro_scr_common.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

namespace {

// The level object's word at +4 (null object: read through address 0, as the original).
uint32_t level_word() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(field);
}

void free_array(const DynArray* array) {
    if (array->data) title_function<void XB_CDECL(void*)>(0x002f5e10)(array->data);  // Scr_Free
}

void* alloc(uint32_t bytes) { return scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(bytes); }  // ScrIface_Alloc

void create_thread(uint32_t ent, uint32_t* functor) {
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}

}  // namespace

void XB_CDECL BroScr_StartMortarGroupNearEnt(uint32_t ent, DynArray ents, float f1, float f2, uint32_t handle,
                                             uint32_t w, uint32_t i, DynArray extra) {
    const float undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    if (f1 == undefined) f1 = title_global<float>(0x0083f11c);  // 400
    if (f2 == undefined) f2 = title_global<float>(0x007f7bf8);  // 1200
    if (i == title_global<uint32_t>(0x00922ec0)) i = 1;       // g_scrUndefinedInt
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(ents.size); ++n) {
        if (n >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), n + 1, n + 4);
        title_function<void XB_CDECL(uint32_t)>(0x00149f60)(ents.data[n]);  // BroScr_MortarEntInit
    }
    const uint32_t level = level_word();
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const DynArray*, const float*, const float*, const uint32_t*,
                                          const uint32_t*, const uint32_t*, const DynArray*);
    title_function<StartThread>(0x0017cc10)(0, 0, 0, 0, 0x00171530, level, &ent, &ents, &f1, &f2, &handle, &w, &i,
                                            &extra);  // Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr(BroScr_MortarGroupNearEntLoop)
    free_array(&ents);
    free_array(&extra);
}

void XB_CDECL BroScr_StartMortarLoopsNearPlayer(uint32_t self, DynArray ents, uint32_t a4, uint32_t a5, uint32_t a6,
                                                uint32_t a7, uint32_t unused) {
    (void)self;
    (void)unused;
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(ents.size); ++n) {
        if (n >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), n + 1, n + 4);
        const uint32_t ent = ents.data[n];
        uint32_t* init = static_cast<uint32_t*>(alloc(0xc));
        if (init) {
            init[0] = 0x007f5ba0;
            init[1] = 0x00149f60;  // BroScr_MortarEntInit
            init[2] = ent;
        }
        create_thread(ent, init);
        if (n >= ents.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&ents), n + 1, n + 4);
        const uint32_t ent2 = ents.data[n];
        uint32_t* loop = static_cast<uint32_t*>(alloc(0x1c));
        if (loop) {
            loop[0] = 0x00824488;
            loop[1] = 0x00170fb0;  // BroScr_MortarLoopNearPlayer
            loop[2] = ent2;
            loop[3] = a4;
            loop[4] = a5;
            loop[5] = a6;
            loop[6] = a7;
        }
        create_thread(ent2, loop);
    }
    free_array(&ents);
}

namespace {

// A damage value: `value` unless it is the undefined float; then the level map's (+offset) value
// for `type` (FUN_0x004b2ac0, operator[]), or `fallback` when that is undefined too (the map is
// read twice then, as in the original).
uint32_t mortar_value(uint32_t value, uint32_t offset, const uint32_t* type, uint32_t fallback) {
    const float undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    float f;
    std::memcpy(&f, &value, 4);
    if (!(f == undefined)) return value;
    using Index = uint32_t* XB_THISCALL(uint32_t, const uint32_t*);
    const auto index = title_function<Index>(0x004b2ac0);
    const uint32_t stored = *index(title_global<uint32_t>(0x00922ee0) + offset, type);
    std::memcpy(&f, &stored, 4);
    if (f == title_global<float>(0x00922eac)) return title_global<uint32_t>(fallback);
    return *index(title_global<uint32_t>(0x00922ee0) + offset, type);
}

}  // namespace

void XB_CDECL BroScr_MortarThinkWithDefaults(uint32_t ent, uint32_t radius, uint32_t max_damage, uint32_t min_damage,
                                             uint32_t a4, uint32_t a5, uint32_t a6, uint32_t type) {
    radius = mortar_value(radius, 0x65c, &type, 0x0083ed04);              // 300
    max_damage = mortar_value(max_damage, 0x690, &type, 0x0083f11c);      // 400
    min_damage = mortar_value(min_damage, 0x7a0, &type, 0x0083ebc4);      // 25
    using GetOrigin = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
    uint32_t buffer[3];
    const uint32_t* o = scr_slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    uint32_t origin[3] = {o[0], o[1], o[2]};
    scr_slot<void XB_CDECL(const uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00922428)(origin, radius, max_damage,
                                                                                        min_damage);  // GScr_RadiusDamage
    uint32_t buffer2[3];
    o = scr_slot<GetOrigin>(0x00922b20)(buffer2, ent);
    const ScrVec3 pos{{o[0], o[1], o[2]}};
    using Think = void XB_CDECL(uint32_t, ScrVec3, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<Think>(0x0016e760)(ent, pos, a4, a5, a6, type);  // BroScr_MortarThink
}

}  // namespace game
