#include "bro_scr_halftrack_crew.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_vehicle.h"
#include "bro_scr_vehicle_path.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"
#include "levels_twins.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kPassengers = 0xe2a39a33;  // the halftrack's passengers (array)

inline uint8_t* level() { return title_global<uint8_t*>(0x00922ee0); }

// The array field through ScriptKeyTable_Find, added empty (ScriptKeyTable_SetArray) when missing.
inline DynArray* found_array(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*);
        value = own_function<SetArray>(&ScriptKeyTable_SetArray)(table, key, empty);  // ScriptKeyTable_SetArray
    }
    return pointer<DynArray>(*value);
}
inline uint32_t* element_at(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}
// The inlined erase of element `index` (elements after it moved down one, the count decremented).
inline void erase_at(DynArray* array, uint32_t index) {
    if (index >= array->size) return;
    uint32_t* p = array->data;
    uint32_t* end = p + array->size;
    if (p == end) return;
    for (; index; --index)
        if (++p == end) return;
    --end;
    for (; p != end; ++p) p[0] = p[1];
    --array->size;
}

}  // namespace

void XB_CDECL BroScr_HalftrackSpawnGuys(uint32_t halftrack, uint32_t spawner) {
    for (int32_t n = 1; n <= 5; ++n) {
        uint32_t guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922520)(spawner, 0);  // GScr_ForceSpawn
        {
            ScriptString name;
            ScriptString copy;
            ScriptString_Copy(&copy, ScriptString_FromCStringPlusInt(&name, pointer<const char>(0x00825460), n));  // "halftrackguy_"
            slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(guy, copy);  // ScrEnt_SetTargetname
            scr_release(&name);
        }
        slot<void XB_CDECL(uint32_t)>(0x00922568)(guy);  // GScr_SetEntFlag400
        DynArray_PushBack(level() + 0x4f8, &guy);
        ScriptString tag;
        ScriptString_FromCStringPlusInt(&tag, pointer<const char>(0x007f5c18), n);  // "tag_passenger"
        BroScr_HalftrackSeatGuy(halftrack, guy, tag, n);
    }
    Scr_Wait(as_float(0x3dcccccd));  // 0.1
    for (int32_t n = 5; n > 0; --n) BroScr_HalftrackAttachPassengers(halftrack, n);
}

void XB_CDECL BroScr_HalftrackSeatGuy(uint32_t halftrack, uint32_t guy, ScriptString tag, int32_t seat) {
    uint32_t found = undefined_int();
    uint32_t i = 0;
    DynArray* const list = reinterpret_cast<DynArray*>(level() + 0x4ec);  // the level's passengers
    for (;; ++i) {
        if (!(static_cast<int32_t>(i) < static_cast<int32_t>(found_array(halftrack, kPassengers)->size))) break;
        const uint32_t passenger = *element_at(found_array(halftrack, kPassengers), i);
        const ScriptString* const name = title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
            ent_script_object(passenger), 0xd91e5c5b);  // ScriptKeyTable_GetString
        if (ScriptString_EqualsString(name, &tag)) {
            found = 0;
            if (static_cast<int32_t>(reinterpret_cast<DynArray*>(level() + 0x4ec)->size) > 0) {
                do {
                    const uint32_t* const mine = element_at(found_array(halftrack, kPassengers), i);
                    if (*element_at(reinterpret_cast<DynArray*>(level() + 0x4ec), found) == *mine) break;
                    ++found;
                } while (static_cast<int32_t>(found) < static_cast<int32_t>(reinterpret_cast<DynArray*>(level() + 0x4ec)->size));
            }
            break;
        }
    }
    if (i == found_array(halftrack, kPassengers)->size) i = title_global<uint32_t>(0x00922ebc);
    const uint32_t lane = undefined_lane();
    uint32_t angles[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x00922620)(halftrack, angles, &tag);  // GScr_GetTagAngles
    const uint32_t a[3] = {angles[0], angles[1], angles[2]};
    uint32_t origin[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(halftrack, origin, &tag);  // GScr_GetTagOrigin
    const uint32_t o[3] = {origin[0], origin[1], origin[2]};
    slot<void XB_CDECL(uint32_t, const uint32_t*, const uint32_t*, uint32_t)>(0x009226f8)(guy, o, a,
                                                                                       1);  // GScr_ActorTryTeleport
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &tag);
        BroScr_VehicleGetPassengers(halftrack, guy, copy, static_cast<uint32_t>(seat));
    }
    slot<void XB_CDECL(uint32_t)>(0x00922564)(guy);  // GScr_ClearEntFlag400
    Scr_WaitFrames(0);
    if (found != undefined_int() && found != list->size) {
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*element_at(reinterpret_cast<DynArray*>(level() + 0x4ec), found));  // GScr_Delete
        erase_at(reinterpret_cast<DynArray*>(level() + 0x4ec), found);
    }
    if (i != undefined_int() && i != 0) erase_at(found_array(halftrack, kPassengers), i);
    scr_release(&tag);
}

}  // namespace game
