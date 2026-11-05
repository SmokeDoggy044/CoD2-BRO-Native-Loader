#include "bro_scr_vehicle_riders.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_level_threads.h"
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

constexpr uint32_t kRiderAnimDone = 0x54e3b2b6;  // the riders' anim notify

// The rider's anim under `key`: level +0x19c [hash(animname)] through HashMap_Index_13 (0x00461030).
// The animname (in `animname`) is the caller's to release.
uint32_t rider_anim_indexed(const uint32_t* rider, uint32_t key, ScriptString* animname) {
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(rider, animname);
    const uint32_t k = key;
    uint32_t hash;
    const uint32_t* const h = Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
    uint32_t* const map = title_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(0x004641e0)(
        title_global<uint32_t>(0x00922ee0) + 0x19c, h);  // HashMap_Index_8
    return *title_function<uint32_t* XB_THISCALL(uint32_t*, const uint32_t*)>(0x00461030)(map, &k);  // HashMap_Index_13
}
// The same with HashMap_Index_13 inline: HashMap_Find, a missing key inserted as 0.
uint32_t rider_anim_found(const uint32_t* rider, uint32_t key, ScriptString* animname) {
    using Find = uint32_t* XB_THISCALL(uint32_t*, uint32_t*, const uint32_t*);
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(rider, animname);
    const uint32_t k = key;
    uint32_t hash;
    const uint32_t* const h = Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
    uint32_t* const map = title_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(0x004641e0)(
        title_global<uint32_t>(0x00922ee0) + 0x19c, h);  // HashMap_Index_8
    uint32_t it;
    title_function<Find>(0x0045e6d0)(map, &it, &k);  // HashMap_Find
    if (it == map[2]) {
        const uint32_t pair[2] = {k, 0};
        uint32_t inserted;
        it = *title_function<Find>(0x004608a0)(map, &inserted, pair);  // HashMap_Insert
    }
    return pointer<uint32_t>(it)[3];
}
// GScr_AnimScriptedNoNotify of `anim` at the vehicle's `tag` (angles, then origin, through the interface
// slots on undefined-lane buffers), then the animname released.
void anim_at_tag(uint32_t vehicle, const uint32_t* rider, ScriptString* tag, uint32_t anim, ScriptString* animname) {
    using TagVec = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
    uint32_t tmp[3];
    tmp[0] = tmp[1] = tmp[2] = undefined_lane();
    slot<TagVec>(0x00922620)(vehicle, tmp, tag);  // GScr_GetTagAngles
    uint32_t angles[3];
    angles[0] = tmp[0];
    angles[1] = tmp[1];
    angles[2] = tmp[2];
    uint32_t tmp2[3];
    tmp2[0] = tmp2[1] = tmp2[2] = undefined_lane();
    slot<TagVec>(0x0092261c)(vehicle, tmp2, tag);  // GScr_GetTagOrigin
    uint32_t origin[3];
    origin[0] = tmp2[0];
    origin[1] = tmp2[1];
    origin[2] = tmp2[2];
    slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t)>(0x009224c0)(
        *rider, kRiderAnimDone, origin, angles, anim);  // GScr_AnimScriptedNoNotify
    scr_release(animname);
}
// The rider's anim `key` there: HashMap_Index_13 lookup (first) / the inline one (rider_anim_found).
void rider_anim_at_tag_indexed(uint32_t vehicle, const uint32_t* rider, ScriptString* tag, uint32_t key) {
    ScriptString animname;
    anim_at_tag(vehicle, rider, tag, rider_anim_indexed(rider, key, &animname), &animname);
}
void rider_anim_at_tag(uint32_t vehicle, const uint32_t* rider, ScriptString* tag, uint32_t key) {
    ScriptString animname;
    anim_at_tag(vehicle, rider, tag, rider_anim_found(rider, key, &animname), &animname);
}
// The same through the ScrCall_GScr_GetTagAngles / _GetTagOrigin wrappers (HashMap_Index_13 lookup).
void rider_anim_at_tag_call(uint32_t* vehicle, const uint32_t* rider, ScriptString* tag, uint32_t key) {
    ScriptString animname;
    const uint32_t anim = rider_anim_indexed(rider, key, &animname);
    const uint32_t tag_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tag));
    uint32_t a[3];
    const uint32_t* const angles = ScrCall_GScr_GetTagAngles(a, vehicle, tag_word);
    uint32_t o[3];
    const uint32_t* const origin = ScrCall_GScr_GetTagOrigin(o, vehicle, tag_word);
    slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t)>(0x009224c0)(
        *rider, kRiderAnimDone, origin, angles, anim);  // GScr_AnimScriptedNoNotify
    scr_release(&animname);
}
void rider_wait(uint32_t rider) { Scr_WaitTill(rider, kRiderAnimDone); }
// The vehicle speed tests (ScrEnt slot 0x00922b38), through the script's float truth values.
bool vehicle_stopped(uint32_t vehicle) {
    const long double speed = slot<long double XB_CDECL(uint32_t)>(0x00922b38)(vehicle);
    return static_cast<float>(title_global<float>(0x0083eb94) > speed ? 1 : 0) != 0.0f;  // 0.01 > speed
}
bool vehicle_moving(uint32_t vehicle) {
    const long double speed = slot<long double XB_CDECL(uint32_t)>(0x00922b38)(vehicle);
    return static_cast<float>(speed > title_global<float>(0x0083eb94) ? 1 : 0) != 0.0f;  // speed > 0.01
}
uint32_t vehicle_turn(uint32_t vehicle) { return *handle_slot(vehicle, 0x59cb9dd1); }

}  // namespace

void XB_CDECL BroScr_VehicleRidersThink(uint32_t vehicle, uint32_t rider, ScriptString tag) {
    using EndOn = void XB_CDECL(uint32_t, uint32_t);
    slot<EndOn>(0x00922188)(rider, 0x0d6e32cd);  // Scr_EndOn
    slot<EndOn>(0x00922188)(rider, 0x78dbe561);
    slot<EndOn>(0x00922188)(rider, 0x06730ae8);  // death
    slot<EndOn>(0x00922188)(vehicle, 0x06730ae8);
    slot<EndOn>(0x00922188)(vehicle, 0x78dbe561);
    if (rider == *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x7f8)) {
        scr_release(&tag);
        return;
    }
    if (!ScriptString_Equals(&tag, pointer<const char>(0x007fa47c)) &&   // "tag_gunner"
        !ScriptString_Equals(&tag, pointer<const char>(0x007f5c28))) {  // "tag_driver"
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                              const ScriptString*);
        title_function<StartThread>(0x0045b3b0)(0, 0, 0, 0, 0x0016aa90, vehicle, &rider,
                                                &tag);  // Scr_StartThread_ent_ent_str(BroScr_AnimScriptedRestoreAnimname)
    }
    bool first = true;
    bool shot = false;
    for (;;) {
        if (*ScrField_alert_state(vehicle) != title_global<uint32_t>(0x00922ef0) &&
            !ScriptString_Equals(&tag, pointer<const char>(0x007fa47c)) &&
            !ScriptString_Equals(&tag, pointer<const char>(0x007f5c28))) {
            // Passengers by the vehicle's alert state.
            if (first) {
                rider_anim_at_tag_indexed(vehicle, &rider, &tag, 0x8af9a2f2);
                rider_wait(rider);
                first = false;
            } else if (*ScrField_alert_state(vehicle) == title_global<uint32_t>(0x008cd188)) {
                rider_anim_at_tag_call(&vehicle, &rider, &tag, 0xe0d0eea0);
                rider_wait(rider);
            } else if (*ScrField_alert_state(vehicle) == title_global<uint32_t>(0x008cd18c)) {
                const int32_t r = random_int(100);
                if (!(r < 70) && !shot) {
                    shot = true;
                    ScriptString out;
                    if (r < 85) {
                        rider_anim_at_tag_call(&vehicle, &rider, &tag, 0x8a8b7c57);
                        BroScr_DroneShoot(&out, rider, kRiderAnimDone);
                    } else {
                        rider_anim_at_tag_call(&vehicle, &rider, &tag, 0x8a8b7c54);
                        BroScr_DroneShoot(&out, rider, kRiderAnimDone);
                    }
                    scr_release(&out);
                } else {
                    shot = false;
                    rider_anim_at_tag_call(&vehicle, &rider, &tag, 0xe0d0eea0);
                    rider_wait(rider);
                }
            }
            Scr_Wait(0.05f);
            continue;
        }
        first = true;
        if (ScriptString_Equals(&tag, pointer<const char>(0x007fa47c))) {  // "tag_gunner"
            rider_anim_at_tag(vehicle, &rider, &tag, 0x8aa69263);
            rider_wait(rider);
            continue;
        }
        if (vehicle_stopped(vehicle)) {
            while (vehicle_stopped(vehicle)) {
                rider_anim_at_tag(vehicle, &rider, &tag, 0x3453ec23);
                rider_wait(rider);
            }
            rider_anim_at_tag(vehicle, &rider, &tag, 0x8bd5b2f2);
            rider_wait(rider);
        } else {
            // Moving: the turn anims by the vehicle's turn field ("right" / "left" hashes).
            while (vehicle_moving(vehicle)) {
                if (vehicle_turn(vehicle) == 0x07151174) {
                    rider_anim_at_tag(vehicle, &rider, &tag, 0x95f5df60);
                    rider_wait(rider);
                    while (vehicle_turn(vehicle) == 0x07151174 && vehicle_moving(vehicle)) {
                        rider_anim_at_tag(vehicle, &rider, &tag, 0x8ba57d74);
                        rider_wait(rider);
                    }
                    rider_anim_at_tag(vehicle, &rider, &tag, 0xbebbf706);
                    rider_wait(rider);
                } else if (vehicle_turn(vehicle) == 0x003798b4) {
                    rider_anim_at_tag(vehicle, &rider, &tag, 0x95f5a760);
                    rider_wait(rider);
                    while (vehicle_turn(vehicle) == 0x003798b4 && vehicle_moving(vehicle)) {
                        rider_anim_at_tag(vehicle, &rider, &tag, 0x8ba57d6a);
                        rider_wait(rider);
                    }
                    rider_anim_at_tag(vehicle, &rider, &tag, 0xbeb4f706);
                    rider_wait(rider);
                } else {
                    rider_anim_at_tag(vehicle, &rider, &tag, 0x8aa69263);
                    rider_wait(rider);
                }
            }
            rider_anim_at_tag(vehicle, &rider, &tag, 0x345eac56);
            rider_wait(rider);
        }
        Scr_Wait(0.05f);
    }
}


}  // namespace game
