#include "bro_scr_level.h"

#include "bro_scr_array.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

// The level block's DynArray at `offset` (the block pointer re-read each time, as the original does).
uint32_t* level_list(uint32_t offset) { return pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + offset); }

bool valid(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(ent) != 0; }  // Scr_EntityHandleIsValid

bool targetname_equals(const uint32_t* ent, const ScriptString* name) {
    ScriptString targetname;
    const bool equal = ScriptString_EqualsString(ScrCall_ScrEnt_GetTargetname(ent, &targetname), name);
    ScriptString_Release(&targetname);
    return equal;
}

uint32_t* find_by_targetname(uint32_t* out, ScriptString* name, uint32_t offset) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(level_list(offset)[2]); ++i) {
        if (targetname_equals(BroScr_ArrayElement(level_list(offset), i), name)) {
            *out = *BroScr_ArrayElement(level_list(offset), i);
            ScriptString_Release(name);
            return out;
        }
    }
    *out = title_global<uint32_t>(0x00922e84);
    ScriptString_Release(name);
    return out;
}

// The finder called with a copy of `name` (it releases the copy).
uint32_t find_vehicle(const ScriptString* name) {
    ScriptString copy;
    ScriptString_Copy(&copy, name);
    uint32_t found;
    BroScr_FindLevelVehicleByTargetname(&found, copy);
    return found;
}
uint32_t find_in_798(const ScriptString* name) {
    ScriptString copy;
    ScriptString_Copy(&copy, name);
    uint32_t found;
    BroScr_FindInLevelList798ByTargetname(&found, copy);
    return found;
}

using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);

// Queues `notify` on each entity of `array` (DynArray), then frees its data.
void notify_each(uint32_t* array, uint32_t notify) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array[2]); ++i)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(*BroScr_ArrayElement(array, i), notify);  // Scr_QueueNotify
}

void stop_manager_ents(ScriptString name) {
    ScriptString empty;
    ScriptString_Copy(&empty, &title_global<ScriptString>(0x00922e90));
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    BroScr_StopManagerEnts(copy, empty);
}

}  // namespace

uint32_t* XB_CDECL BroScr_FindLevelVehicleByTargetname(uint32_t* out, ScriptString name) {
    return find_by_targetname(out, &name, 0x784);
}

uint32_t* XB_CDECL BroScr_FindInLevelList798ByTargetname(uint32_t* out, ScriptString name) {
    return find_by_targetname(out, &name, 0x790);
}

void XB_CDECL BroScr_SetVehicleTargetsGroupByName(ScriptString name, ScriptString group) {
    const uint32_t vehicle = find_vehicle(&name);
    if (valid(vehicle)) {
        ScriptString copy;
        ScriptString_Copy(&copy, &group);
        ScrField_targets_groupname_2(vehicle, copy);
    }
    ScriptString_Release(&name);
    ScriptString_Release(&group);
}

void XB_CDECL BroScr_SetVehicleTargetsByName(ScriptString name, uint32_t target) {
    const uint32_t vehicle = find_vehicle(&name);
    if (valid(vehicle)) BroScr_SetTargetsGroupAndEnt(vehicle, target);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_SetTargetsByTargetname(uint32_t ent, ScriptString name) {
    using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    const uint32_t found = slot<GetEnt>(0x009221c4)(&name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
    if (valid(found)) BroScr_SetTargetsGroupAndEnt(ent, found);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_SetVehicleUsableByName(ScriptString name, uint32_t unusable) {
    const uint32_t vehicle = find_vehicle(&name);
    if (valid(vehicle)) {
        if (static_cast<uint8_t>(unusable))
            slot<void XB_CDECL(uint32_t)>(0x009228b4)(vehicle);  // GScr_Vehicle_MakeUnusable
        else
            slot<void XB_CDECL(uint32_t)>(0x009228b8)(vehicle);  // GScr_Vehicle_MakeUsable
    }
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_SetVehicleCanShootLowByName(ScriptString name, uint32_t value) {
    const uint32_t vehicle = find_vehicle(&name);
    if (valid(vehicle)) *title_function<uint8_t* XB_CDECL(uint32_t)>(0x0012e6a0)(vehicle) = static_cast<uint8_t>(value);  // ScrField_can_shoot_low
    ScriptString_Release(&name);
}

uint32_t* XB_CDECL BroScr_GetVehicleSecondsSince_h05903940(uint32_t* out, ScriptString name) {
    const uint32_t vehicle = find_vehicle(&name);
    title_function<void XB_CDECL(uint32_t*, uint32_t)>(0x0012ff00)(out, vehicle);  // BroScr_GetEntSecondsSince_h05903940
    ScriptString_Release(&name);
    return out;
}

void XB_CDECL BroScr_NotifyEntsByTargetname(ScriptString name) {
    uint32_t array[3] = {0, 0, 0};
    Scr_GetEntArray(address_of(&name), 0x0d786b4c, reinterpret_cast<uint8_t*>(array), 0);
    notify_each(array, 0x78b19de2);
    if (array[0]) Scr_Free(pointer<void>(array[0]));
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_StopManagerEnts(ScriptString name, ScriptString suffix) {
    uint32_t array[3] = {0, 0, 0};
    if (suffix.rep) {
        ScriptString full;
        ScriptString_ConcatCString(&full, &name, pointer<const char>(0x00824304));
        using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
        const uint32_t ent = slot<GetEnt>(0x009221c4)(&full, 0x2d36ed4e, 0, 0, 0);  // GScr_GetEntArray
        ScriptString_Release(&full);
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, 0x0c34930d);  // Scr_QueueNotify
        Scr_GetEntArray(address_of(&name), 0x2d36ed4e, reinterpret_cast<uint8_t*>(array), 0);
        notify_each(array, 0x0c34930d);
        slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    } else {
        Scr_GetEntArray(address_of(&name), 0x2d36ed4e, reinterpret_cast<uint8_t*>(array), 0);
        notify_each(array, 0x0c34930d);
    }
    if (array[0]) Scr_Free(pointer<void>(array[0]));
    ScriptString_Release(&name);
    ScriptString_Release(&suffix);
}

void XB_CDECL BroScr_StopManagerEntsByName(ScriptString name) {
    stop_manager_ents(name);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_StopManagerEntsByName_2(uint32_t, ScriptString name, ScriptString suffix) {
    ScriptString suffix_copy;
    ScriptString_Copy(&suffix_copy, &suffix);
    ScriptString name_copy;
    ScriptString_Copy(&name_copy, &name);
    BroScr_StopManagerEnts(name_copy, suffix_copy);
    ScriptString_Release(&name);
    ScriptString_Release(&suffix);
}

void XB_CDECL BroScr_StopManagerEntsByName_3(uint32_t, ScriptString name) {
    stop_manager_ents(name);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_InList798ByNameCall(ScriptString name, uint32_t value) {
    const uint32_t found = find_in_798(&name);
    if (valid(found)) BroScr_EntArrayRemove_he104fd5c(found, value);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_AddToLevelList798EntArray(ScriptString name, uint32_t value) {
    const uint32_t found = find_in_798(&name);
    if (valid(found)) {
        const uint32_t item = value;
        uint32_t* const array = title_function<GetArray>(0x0045cd50)(BroScr_ScriptObjectOrEmpty(found), 0xe104fd5c);  // ScriptKeyTable_GetArray
        DynArray_PushBack(reinterpret_cast<uint8_t*>(array), &item);
    }
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_DeleteFromLevelList784ByTargetname(ScriptString name) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(level_list(0x784)[2]); ++i) {
        if (!valid(*BroScr_ArrayElement(level_list(0x784), i))) continue;
        if (!targetname_equals(BroScr_ArrayElement(level_list(0x784), i), &name)) continue;
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*BroScr_ArrayElement(level_list(0x784), i));  // GScr_Delete
        BroScr_ArrayEraseAt(level_list(0x784), i);
        break;
    }
    ScriptString_Release(&name);
}

}  // namespace game
