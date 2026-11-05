#include "bro_scr_stuka_bomb.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_drones.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);

inline uint8_t* level() { return title_global<uint8_t*>(0x00922ee0); }
inline ScrVec3 origin_of(uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    ScrVec3 out;
    out.v[0] = v[0];
    out.v[1] = v[1];
    out.v[2] = v[2];
    return out;
}
// The level float at `offset`, `fallback` (a title constant) when undefined.
inline uint32_t level_float_or(uint32_t offset, uint32_t fallback) {
    const uint32_t value = *reinterpret_cast<uint32_t*>(level() + offset);
    return as_float(value) == as_float(undefined_float()) ? title_global<uint32_t>(fallback) : value;
}

}  // namespace

void XB_CDECL BroScr_StukaBombExplode(uint32_t, uint32_t bomb, int32_t range, int32_t max_damage, int32_t min_damage) {
    if (!*reinterpret_cast<uint32_t*>(level() + 0x264)) return;
    const uint32_t scale = level_float_or(0x630, 0x0083eb24);     // 0.5
    const uint32_t duration = level_float_or(0x268, 0x0083eba8);  // 2
    const uint32_t radius = level_float_or(0x2a0, 0x0083eff8);    // 1024
    const int32_t undefined = static_cast<int32_t>(undefined_int());
    if (range == undefined) range = 0x300;
    if (max_damage == undefined) max_damage = 0x190;
    if (min_damage == undefined) min_damage = 0x19;
    uint32_t origin_ent;
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 at = origin_of(bomb);
        origin_ent = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &at, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    bool play = true;
    if (ScriptString_Equals(reinterpret_cast<ScriptString*>(level() + 0x1c4), pointer<const char>(0x0082bc28))) {  // "piano_lupo"
        const ScrVec3 player = origin_of(title_global<uint32_t>(0x00922eec));
        const ScrVec3 at = origin_of(bomb);
        const long double distance =
            slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x00922268)(&at, &player);  // Scr_Distance
        play = distance > static_cast<long double>(title_global<float>(0x007f6180));  // 250
    }
    if (play)
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
            origin_ent, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(level() + 0x264)), 0x45727144, 0,
            0);  // GScr_PlaySound
    slot<void XB_CDECL(uint32_t)>(0x00922540)(bomb);  // GScr_Delete
    {
        ScrVec3 at = origin_of(origin_ent);
        slot<void XB_CDECL(uint32_t, uint32_t, ScrVec3*, uint32_t)>(0x00922444)(scale, duration, &at, radius);  // GScr_Earthquake
    }
    {
        ScrVec3 at = origin_of(origin_ent);
        const float min_f = static_cast<float>(min_damage);
        const float max_f = static_cast<float>(max_damage);
        const float range_f = static_cast<float>(range);
        slot<void XB_CDECL(ScrVec3*, float, float, float)>(0x00922428)(&at, range_f, max_f, min_f);  // GScr_RadiusDamage
    }
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    queue_notify(*pointer<uint32_t>(object ? object + 4 : 0), 0xa581b0e9);
    BroScr_StukaStartNearbyDrones(origin_ent);
    Scr_WaitTill(origin_ent, 0x45727144);
}

}  // namespace game
