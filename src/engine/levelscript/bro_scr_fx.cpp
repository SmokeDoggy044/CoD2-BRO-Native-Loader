#include "bro_scr_fx.h"

#include "bro_scr_common.h"
#include "title_data.h"
#include "bro_scr_fx_threads.h"
#include "bro_scr_level_threads_2.h"
#include "g_script.h"

namespace game {

namespace {

constexpr uint32_t kUndefinedFloat = 0x00922eac;   // g_scrUndefinedFloat
constexpr uint32_t kUndefinedHandle = 0x00922eb4;  // g_scrUndefinedHandle
constexpr uint32_t kUndefinedInt = 0x00922ec0;     // g_scrUndefinedInt

// Scr_StartThread_ent_hash_i_i_f_f_f_f_hash on BroScr_RandomSoundBursts (0x0015bde0): four zero
// words, the function, the entity, then pointers to the eight values.
void RandomSoundBursts(uint32_t entity, const uint32_t* const (&values)[8]) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*,
                                          const uint32_t*, const uint32_t*, const uint32_t*);
    own_function<StartThread>(&Scr_StartThread_ent_hash_i_i_f_f_f_f_hash_2_entry)(0, 0, 0, 0, own_address(&BroScr_RandomSoundBursts), entity, values[0], values[1], values[2], values[3],
                                            values[4], values[5], values[6], values[7]);
}

}  // namespace

void XB_CDECL BroScr_PlaySoundOn(uint32_t entity, ScriptString key) {
    using Index = uint32_t XB_THISCALL(uint32_t, const ScriptString*);
    const uint32_t alias = title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x458, &key);  // HashMap_Index
    scr_slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221dc)(entity, alias, 0, 0, 0);  // GScr_PlaySound
}

void XB_CDECL BroScr_StartFxLoopingThread() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    const uint32_t value = *reinterpret_cast<volatile uint32_t*>(field);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, own_address(&BroScr_StartLoopingFx), value);  // Scr_StartThreadFunctor1(BroScr_StartLoopingFx)
}

void XB_CDECL BroScr_RandomSoundBurstsThread(uint32_t entity, uint32_t alias) {
    const uint32_t handle = title_global<uint32_t>(kUndefinedHandle);
    const uint32_t f = title_global<uint32_t>(kUndefinedFloat);  // copied as words (movss)
    const uint32_t i = title_global<uint32_t>(kUndefinedInt);
    const uint32_t* const values[8] = {&alias, &i, &i, &f, &title_global<uint32_t>(0x00922ea4), &f,
                                       &title_global<uint32_t>(0x00922ea4), &handle};
    RandomSoundBursts(entity, values);
}

void XB_CDECL BroScr_RandomSoundBurstsThread_2(uint32_t entity, uint32_t alias, uint32_t a3, uint32_t a4, uint32_t a5,
                                               uint32_t a6) {
    const uint32_t handle = title_global<uint32_t>(kUndefinedHandle);
    const uint32_t f = title_global<uint32_t>(kUndefinedFloat);
    const uint32_t* const values[8] = {&alias, &a3, &a4, &a5, &a6, &f, &f, &handle};
    RandomSoundBursts(entity, values);
}

void XB_CDECL BroScr_RandomSoundBurstsThread_3(uint32_t entity, uint32_t alias, uint32_t a3, uint32_t a4, uint32_t a5,
                                               uint32_t a6, uint32_t a7, uint32_t a8) {
    const uint32_t handle = title_global<uint32_t>(kUndefinedHandle);
    const uint32_t* const values[8] = {&alias, &a3, &a4, &a5, &a6, &a7, &a8, &handle};
    RandomSoundBursts(entity, values);
}

void XB_CDECL BroScr_SpawnLoopSoundOrigin(uint32_t self, uint32_t alias, ScrVec3 origin, uint32_t a5, uint32_t a6,
                                          uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11) {
    (void)self;
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab88);  // "script_origin"
    using Spawn = uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t);
    const uint32_t ent = scr_slot<Spawn>(0x009222f0)(&classname, &origin, 0);  // GScr_Spawn
    scr_release(&classname);
    const uint32_t* const values[8] = {&alias, &a5, &a6, &a7, &a8, &a9, &a10, &a11};
    RandomSoundBursts(ent, values);
}

}  // namespace game
