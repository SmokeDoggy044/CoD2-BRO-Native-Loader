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

void XB_CDECL BroScr_NotifyEntsByTargetname(uint32_t, ScriptString name) {
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

void XB_CDECL BroScr_VehicleGunnersByName(ScriptString name) {
    const uint32_t vehicle = find_vehicle(&name);
    if (valid(vehicle)) title_function<void XB_CDECL(uint32_t)>(0x001485f0)(vehicle);  // BroScr_ReleaseGunners
    ScriptString_Release(&name);
}

// The entry hands over ECX: the original builds the stored word from its `push ecx` slot.
PLATFORM_ASM_ENTRY(BroScr_SetupWallHopTrigger_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(BroScr_SetupWallHopTrigger) "addl $8, %esp\n"
                   "ret")

void XB_CDECL BroScr_SetupWallHopTrigger(uint32_t ecx, uint32_t ent) {
    const uint32_t player = pointer<uint32_t>(title_global<uint32_t>(0x00922ee0))[1];
    ScriptKeyTable_Set(BroScr_ScriptObjectOrEmpty(player), 0xddfa2c42, ecx & 0xffffff00u);
    bool defined;
    ScrField_script_mantle_type_IsDefined(&defined, ent);
    if (!defined) return;
    if (!ScriptString_Equals(ScrField_script_mantle_type(ent), pointer<const char>(0x00824da4))) return;
    const uint32_t args[6] = {0, 0, 0, 0, 0x0014fb70, ent};  // BroScr_WallHopTrigger
    Scr_StartThreadFunctor1(args);
}

uint32_t* XB_CDECL BroScr_GetCurrentPathPos(uint32_t* out, uint32_t ent) {
    const uint32_t index = *BroScr_FieldSlot(ent, 0x9aec9c1a);
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    uint32_t* const array = title_function<GetArray>(0x0017c400)(BroScr_ScriptObjectOrEmpty(ent), 0xaeae4e0b);  // ScriptKeyTable_GetOrAddArray_3
    if (index >= array[2]) title_function<void XB_THISCALL(uint32_t*, uint32_t, uint32_t)>(0x0017bd20)(array, index + 1, index + 4);  // DynArray_Resize24
    const uint32_t* const element = pointer<uint32_t>(array[0] + index * 24);
    out[0] = element[0];
    out[1] = element[1];
    out[2] = element[2];
    return out;
}

namespace {

uint32_t* followers(uint32_t leader) {
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetArray>(0x0045cd50)(BroScr_ScriptObjectOrEmpty(leader), 0xdeca7f50);  // ScriptKeyTable_GetArray
}

}  // namespace

void XB_CDECL BroScr_RemoveFollower(uint32_t leader, uint32_t follower) {
    title_function<void XB_CDECL(uint32_t*, uint32_t)>(0x0019ba60)(followers(leader), follower);  // PtrArray_RemoveValue
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(follower, 0x49c98989);  // Scr_QueueNotify
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(followers(leader)[2]); ++i)
        *BroScr_FieldSlot(*BroScr_ArrayElement(followers(leader), i), 0x185cac5b) = i;
}

void XB_CDECL BroScr_RemoveAllFollowers(uint32_t leader) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(leader, 0xcc07a20a);  // Scr_QueueNotify
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(followers(leader)[2]); ++i)
        BroScr_RemoveFollower(leader, *BroScr_ArrayElement(followers(leader), i));
    // The bool field 0xd57e151d = 0; when missing it is added first as a word whose low byte is the
    // undefined bool and whose upper bytes are the original's stack slot (here the `leader` argument).
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(leader);
    uint32_t* value = ScriptKeyTable_Find(table, 0xd57e151d);
    if (!value) value = ScriptKeyTable_Set(table, 0xd57e151d, (leader & 0xffffff00u) | title_global<uint8_t>(0x00922ed0));
    *reinterpret_cast<uint8_t*>(value) = 0;
}

void XB_CDECL BroScr_SetRandomGoalPos(uint32_t* ents, uint32_t* nodes) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents[2]); ++i) {
        const uint32_t pick = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(nodes[2]);  // Scr_RandomInt
        const uint32_t node = *BroScr_ArrayElement(nodes, pick);
        uint32_t buffer[3];
        const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, node);  // ScrEnt_GetOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922684)(*BroScr_ArrayElement(ents, i), origin);  // GScr_SetGoalPos
    }
}

void XB_CDECL BroScr_InitShrimps(uint32_t, ScriptString name) {
    uint32_t array[3] = {0, 0, 0};
    Scr_GetEntArray(address_of(&name), 0x0d786b4c, reinterpret_cast<uint8_t*>(array), 0);
    if (array[1] != 0 && static_cast<int32_t>(array[2]) >= 1) {
        if (*ScrField_using_shrimps(*BroScr_ArrayElement(array, 0)) != 0) {
            slot<void XB_CDECL(uint32_t)>(0x009221f4)(*ScrField_shrimp_id(*BroScr_ArrayElement(array, 0)));  // GScr_FreeSound
        } else {
            const uint32_t level = title_global<uint32_t>(0x00922ee0);
            const uint32_t player = *pointer<uint32_t>(level ? level + 4 : 0);
            using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*);
            title_function<Start>(0x0045a7c0)(0, 0, 0, 0, 0x00135ed0, player, &name);  // Scr_StartThread_ent_str: BroScr_NotifyEntsByTargetname
        }
    }
    if (array[0]) Scr_Free(pointer<void>(array[0]));
    ScriptString_Release(&name);
}

}  // namespace game
