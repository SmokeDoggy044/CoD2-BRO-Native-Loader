#include "g_scr_builtins.h"

#include <cstring>

#include "cl.h"
#include "dobj.h"
#include "g_main.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "actor.h"
#include "bg_misc.h"
#include "cg_hud.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "title_data.h"
#include "xanim.h"

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

// The entity a script handle names: index in the low 12 bits (below 0x540), generation above, checked
// against the slot table at 0x0097a968 {entity, generation}; null when stale.
uint8_t* handle_entity(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
}

// The GScr_SetEntFlag* / GScr_ClearEntFlag* family: one flag of an entity's +0x2a4.
template <uint32_t Flag>
void clear_ent_flag(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) field<uint32_t>(ent, 0x2a4) &= ~Flag;
}

// GScr_LoadConsts: {text, slot} pairs in the original's order (generated from its disassembly).
struct NameSlot {
    uint32_t text;
    uint32_t slot;
};

// scr_const_* (0x0097a650..): Com_HashKeyNoCase of the text; the text is also entered in the name map.
const NameSlot kConstHashes[] = {
    {0x0083aecc, 0x0097a654},  // "active"
    {0x0083aec0, 0x0097a650},  // "activate"
    {0x0083aeb0, 0x0097a658},  // "angle deltas"
    {0x0083aea4, 0x0097a65c},  // "animdone"
    {0x0083ae9c, 0x0097a660},  // "bodyque"
    {0x0083ae94, 0x0097a664},  // "claimed"
    {0x0083ae8c, 0x0097a668},  // "combat"
    {0x0083ae84, 0x0097a66c},  // "count"
    {0x0083ae7c, 0x0097a674},  // "crouch"
    {0x0083ae74, 0x0097a678},  // "crowbar"
    {0x0083ae6c, 0x0097a670},  // "current"
    {0x00836254, 0x0097a67c},  // "damage"
    {0x0083ae60, 0x0097a680},  // "deactivate"
    {0x00831f60, 0x0097a684},  // "death"
    {0x0083ae54, 0x0097a688},  // "deathplant"
    {0x0083ae44, 0x0097a68c},  // "disconnected"
    {0x0083ae3c, 0x0097a690},  // "dlight"
    {0x0083ae34, 0x0097a694},  // "done"
    {0x0083ae2c, 0x0097a698},  // "empty"
    {0x0082dcbc, 0x0097a69c},  // "enemy"
    {0x0083ae20, 0x0097a6a0},  // "enemyhidden"
    {0x0083ae10, 0x0097a6a4},  // "enemyvisible"
    {0x0083aeec, 0x0097a6a8},  // "entity"
    {0x0083ae08, 0x0097a6d8},  // "failed"
    {0x0083adfc, 0x0097a6ac},  // "face angle"
    {0x0083adec, 0x0097a6b0},  // "face current"
    {0x0083addc, 0x0097a6b4},  // "face default"
    {0x0083adcc, 0x0097a6b8},  // "face direction"
    {0x0083adc0, 0x0097a6bc},  // "face enemy"
    {0x0083ada8, 0x0097a6c0},  // "face enemy or motion"
    {0x0083ad90, 0x0097a6c4},  // "face enemy with offset"
    {0x0083ad84, 0x0097a6c8},  // "face goal"
    {0x0083ad78, 0x0097a6cc},  // "face motion"
    {0x0083ad6c, 0x0097a6d0},  // "face point"
    {0x0083ad60, 0x0097a6d4},  // "fraction"
    {0x0083ad54, 0x0097a6dc},  // "func_door"
    {0x0083ad40, 0x0097a6e0},  // "func_door_rotating"
    {0x0083ad30, 0x0097a6e4},  // "func_rotating"
    {0x0083ad20, 0x0097a6e8},  // "func_tramcar"
    {0x0083ad18, 0x0097a6ec},  // "goal"
    {0x0083ad10, 0x0097a6f0},  // "gravity"
    {0x0082b690, 0x0097a6f4},  // "grenade"
    {0x0083ad00, 0x0097a6f8},  // "grenade danger"
    {0x0083acf0, 0x0097a6fc},  // "info_notnull"
    {0x0083acd8, 0x0097a700},  // "info_player_deathmatch"
    {0x0083accc, 0x0097a704},  // "invisible"
    {0x0083acc4, 0x0097a708},  // "key1"
    {0x0083acbc, 0x0097a70c},  // "key2"
    {0x008325dc, 0x0097a710},  // "killanimscript"
    {0x0083b7c4, 0x0097a714},  // "left"
    {0x0083acb0, 0x0097a718},  // "max time"
    {0x0083aca0, 0x0097a71c},  // "menuresponse"
    {0x0083ac94, 0x0097a720},  // "min energy"
    {0x0083ac88, 0x0097a724},  // "min time"
    {0x008367a4, 0x0097a728},  // "mortar"
    {0x0083ac7c, 0x0097a72c},  // "movedone"
    {0x0083ac6c, 0x0097a730},  // "movesuppressed"
    {0x0083ac64, 0x0097a734},  // "noclass"
    {0x0083bb08, 0x0097a738},  // "noclip"
    {0x0083ac5c, 0x0097a73c},  // "noenemy"
    {0x0083ac50, 0x0097a740},  // "nogravity"
    {0x0083ac44, 0x0097a744},  // "noncombat"
    {0x0082d25c, 0x0097a748},  // "normal"
    {0x0083ac38, 0x0097a74c},  // "occupied"
    {0x00831f68, 0x0097a750},  // "pain"
    {0x0083ac2c, 0x0097a754},  // "physicsdone"
    {0x0083ac24, 0x0097a758},  // "pickup"
    {0x0083ac1c, 0x0097a75c},  // "player"
    {0x0083ac10, 0x0097a760},  // "position"
    {0x008367fc, 0x0097a764},  // "prone"
    {0x0083abfc, 0x0097a768},  // "reached_end_node"
    {0x0083abe8, 0x0097a76c},  // "reached_wait_node"
    {0x0083abd4, 0x0097a770},  // "reached_wait_speed"
    {0x0083b7b4, 0x0097a774},  // "right"
    {0x0082b498, 0x0097a778},  // "rocket"
    {0x0083abc8, 0x0097a77c},  // "rotatedone"
    {0x0083abbc, 0x0097a780},  // "sound_blend"
    {0x0083aba8, 0x0097a784},  // "script_brushmodel"
    {0x0083ab98, 0x0097a788},  // "script_model"
    {0x0083ab88, 0x0097a78c},  // "script_origin"
    {0x0083ab78, 0x0097a790},  // "script_vehicle"
    {0x0083ab60, 0x0097a794},  // "script_vehicle_corpse"
    {0x0083ab48, 0x0097a798},  // "script_vehicle_collmap"
    {0x0083ab3c, 0x0097a7c8},  // "front_left"
    {0x0083ab30, 0x0097a7cc},  // "front_right"
    {0x0083ab24, 0x0097a7d0},  // "back_left"
    {0x0083ab18, 0x0097a7d4},  // "back_right"
    {0x0083ab0c, 0x0097a7d8},  // "middle_left"
    {0x0083aafc, 0x0097a7dc},  // "middle_right"
    {0x0083aaec, 0x0097a7e0},  // "scriptcamera"
    {0x0083aae4, 0x0097a7e4},  // "spawned"
    {0x0083680c, 0x0097a7e8},  // "stand"
    {0x0082c0d8, 0x0097a7ec},  // "back"
    {0x0083aadc, 0x0097a7f0},  // "wounded"
    {0x0083aad0, 0x0097a7f4},  // "suppressed"
    {0x0083aac4, 0x0097a7f8},  // "surfacetype"
    {0x0083aab8, 0x0097a7fc},  // "tag_engine1"
    {0x0083aaac, 0x0097a800},  // "tag_engine2"
    {0x0083aa9c, 0x0097a804},  // "target_location"
    {0x0083aa84, 0x0097a808},  // "target_script_trigger"
    {0x0083aa78, 0x0097a80c},  // "tempEntity"
    {0x0083aa68, 0x0097a810},  // "muzzleEntity"
    {0x0083aa60, 0x0097a814},  // "touch"
    {0x0083aa58, 0x0097a818},  // "trigger"
    {0x0083aa4c, 0x0097a81c},  // "trigger_use"
    {0x0083aa3c, 0x0097a824},  // "trigger_damage"
    {0x0083aa2c, 0x0097a828},  // "trigger_lookat"
    {0x0083aa1c, 0x0097a820},  // "trigger_mount"
    {0x0083aa10, 0x0097a82c},  // "truck_cam"
    {0x0083aa04, 0x0097a830},  // "turret_fire"
    {0x0083a9f0, 0x0097a834},  // "turret_on_target"
    {0x0083a9dc, 0x0097a83c},  // "player_on_vehicle"
    {0x0083a9c8, 0x0097a840},  // "player_off_vehicle"
    {0x0083a9b4, 0x0097a838},  // "turret_on_vistarget"
    {0x0083a9a4, 0x0097a848},  // "xmodel/airborne"
    {0x0083a990, 0x0097a84c},  // "xmodel/wehrmacht"
    {0x0083a984, 0x0097a844},  // "worldspawn"
    {0x0083a97c, 0x0097a850},  // "auto_ai"
    {0x0083a970, 0x0097a854},  // "auto_nonai"
    {0x0083a968, 0x0097a858},  // "manual"
    {0x0083a95c, 0x0097a85c},  // "manual_ai"
    {0x0083a948, 0x0097a860},  // "turretstatechange"
    {0x0083a934, 0x0097a864},  // "turretownerchange"
    {0x0083a928, 0x0097a868},  // "nophysics"
    {0x00832430, 0x0097a86c},  // "none"
    {0x0083a91c, 0x0097a870},  // "obstacle"
    {0x0083a914, 0x0097a874},  // "world"
    {0x0083a908, 0x0097a878},  // "zonesloaded"
    {0x0083a8f4, 0x0097a87c},  // "groundEntChanged"
    {0x0083a8e8, 0x0097a880},  // "<custom>"
    {0x0083a8dc, 0x0097a884},  // "bigRotation"
    {0x0083a8d0, 0x0097a888},  // "overheated"
};
// Script strings 0x00984ac0..0x00984ccc, built through a temporary.
const NameSlot kConstStrings[] = {
    {0x0083aec0, 0x00984ac0},  // "activate"
    {0x0083aecc, 0x00984ac4},  // "active"
    {0x0083aeb0, 0x00984ac8},  // "angle deltas"
    {0x0083aea4, 0x00984acc},  // "animdone"
    {0x0083ae9c, 0x00984ad0},  // "bodyque"
    {0x0083ae94, 0x00984ad4},  // "claimed"
    {0x0083ae8c, 0x00984ad8},  // "combat"
    {0x0083ae84, 0x00984adc},  // "count"
    {0x0083ae7c, 0x00984ae4},  // "crouch"
    {0x0083ae74, 0x00984ae8},  // "crowbar"
    {0x0083ae6c, 0x00984ae0},  // "current"
    {0x00836254, 0x00984aec},  // "damage"
    {0x0083ae60, 0x00984af0},  // "deactivate"
    {0x00831f60, 0x00984af4},  // "death"
    {0x0083ae54, 0x00984af8},  // "deathplant"
    {0x0083ae44, 0x00984afc},  // "disconnected"
    {0x0083ae3c, 0x00984b00},  // "dlight"
    {0x0083ae34, 0x00984b04},  // "done"
    {0x0083ae2c, 0x00984b08},  // "empty"
    {0x0082dcbc, 0x00984b0c},  // "enemy"
    {0x0083ae20, 0x00984b10},  // "enemyhidden"
    {0x0083ae10, 0x00984b14},  // "enemyvisible"
    {0x0083aeec, 0x00984b18},  // "entity"
    {0x0083ae08, 0x00984b48},  // "failed"
    {0x0083adfc, 0x00984b1c},  // "face angle"
    {0x0083adec, 0x00984b20},  // "face current"
    {0x0083addc, 0x00984b24},  // "face default"
    {0x0083adcc, 0x00984b28},  // "face direction"
    {0x0083adc0, 0x00984b2c},  // "face enemy"
    {0x0083ada8, 0x00984b30},  // "face enemy or motion"
    {0x0083ad90, 0x00984b34},  // "face enemy with offset"
    {0x0083ad84, 0x00984b38},  // "face goal"
    {0x0083ad78, 0x00984b3c},  // "face motion"
    {0x0083ad6c, 0x00984b40},  // "face point"
    {0x0083ad60, 0x00984b44},  // "fraction"
    {0x0083ad54, 0x00984b4c},  // "func_door"
    {0x0083ad40, 0x00984b50},  // "func_door_rotating"
    {0x0083ad30, 0x00984b54},  // "func_rotating"
    {0x0083ad20, 0x00984b58},  // "func_tramcar"
    {0x0083ad18, 0x00984b5c},  // "goal"
    {0x0083ad10, 0x00984b60},  // "gravity"
    {0x0082b690, 0x00984b64},  // "grenade"
    {0x0083ad00, 0x00984b68},  // "grenade danger"
    {0x0083acf0, 0x00984b6c},  // "info_notnull"
    {0x0083acd8, 0x00984b70},  // "info_player_deathmatch"
    {0x0083accc, 0x00984b74},  // "invisible"
    {0x0083acc4, 0x00984b78},  // "key1"
    {0x0083acbc, 0x00984b7c},  // "key2"
    {0x008325dc, 0x00984b80},  // "killanimscript"
    {0x0083b7c4, 0x00984b84},  // "left"
    {0x0083acb0, 0x00984b88},  // "max time"
    {0x0083aca0, 0x00984b8c},  // "menuresponse"
    {0x0083ac94, 0x00984b90},  // "min energy"
    {0x0083ac88, 0x00984b94},  // "min time"
    {0x008367a4, 0x00984b98},  // "mortar"
    {0x0083ac7c, 0x00984b9c},  // "movedone"
    {0x0083ac6c, 0x00984ba0},  // "movesuppressed"
    {0x0083ac64, 0x00984ba4},  // "noclass"
    {0x0083bb08, 0x00984ba8},  // "noclip"
    {0x0083ac5c, 0x00984bac},  // "noenemy"
    {0x0083ac50, 0x00984bb0},  // "nogravity"
    {0x0083ac44, 0x00984bb4},  // "noncombat"
    {0x0082d25c, 0x00984bb8},  // "normal"
    {0x0083ac38, 0x00984bbc},  // "occupied"
    {0x00831f68, 0x00984bc0},  // "pain"
    {0x0083ac24, 0x00984bc8},  // "pickup"
    {0x0083ac1c, 0x00984bcc},  // "player"
    {0x0083ac10, 0x00984bd0},  // "position"
    {0x008367fc, 0x00984bd4},  // "prone"
    {0x0083abfc, 0x00984bd8},  // "reached_end_node"
    {0x0083abe8, 0x00984bdc},  // "reached_wait_node"
    {0x0083abd4, 0x00984be0},  // "reached_wait_speed"
    {0x0083b7b4, 0x00984be4},  // "right"
    {0x0082b498, 0x00984be8},  // "rocket"
    {0x0083abc8, 0x00984bec},  // "rotatedone"
    {0x0083abbc, 0x00984bf0},  // "sound_blend"
    {0x0083aba8, 0x00984bf4},  // "script_brushmodel"
    {0x0083ab98, 0x00984bf8},  // "script_model"
    {0x0083ab88, 0x00984bfc},  // "script_origin"
    {0x0083ab78, 0x00984c00},  // "script_vehicle"
    {0x0083ab60, 0x00984c04},  // "script_vehicle_corpse"
    {0x0083ab48, 0x00984c08},  // "script_vehicle_collmap"
    {0x0083ab3c, 0x00984c0c},  // "front_left"
    {0x0083ab30, 0x00984c10},  // "front_right"
    {0x0083ab24, 0x00984c14},  // "back_left"
    {0x0083ab18, 0x00984c18},  // "back_right"
    {0x0083ab0c, 0x00984c1c},  // "middle_left"
    {0x0083aafc, 0x00984c20},  // "middle_right"
    {0x0083aaec, 0x00984c24},  // "scriptcamera"
    {0x0083aae4, 0x00984c28},  // "spawned"
    {0x0083680c, 0x00984c2c},  // "stand"
    {0x0082c0d8, 0x00984c30},  // "back"
    {0x0083aadc, 0x00984c34},  // "wounded"
    {0x0083aad0, 0x00984c38},  // "suppressed"
    {0x0083aac4, 0x00984c3c},  // "surfacetype"
    {0x0083aab8, 0x00984c40},  // "tag_engine1"
    {0x0083aaac, 0x00984c44},  // "tag_engine2"
    {0x0083aa9c, 0x00984c48},  // "target_location"
    {0x0083aa84, 0x00984c4c},  // "target_script_trigger"
    {0x0083aa78, 0x00984c50},  // "tempEntity"
    {0x0083aa68, 0x00984c54},  // "muzzleEntity"
    {0x0083aa60, 0x00984c58},  // "touch"
    {0x0083aa58, 0x00984c5c},  // "trigger"
    {0x0083aa4c, 0x00984c60},  // "trigger_use"
    {0x0083aa3c, 0x00984c68},  // "trigger_damage"
    {0x0083aa2c, 0x00984c6c},  // "trigger_lookat"
    {0x0083aa1c, 0x00984c64},  // "trigger_mount"
    {0x0083aa10, 0x00984c70},  // "truck_cam"
    {0x0083aa04, 0x00984c74},  // "turret_fire"
    {0x0083a9f0, 0x00984c78},  // "turret_on_target"
    {0x0083a9dc, 0x00984c80},  // "player_on_vehicle"
    {0x0083a9c8, 0x00984c84},  // "player_off_vehicle"
    {0x0083a9b4, 0x00984c7c},  // "turret_on_vistarget"
    {0x0083a9a4, 0x00984c8c},  // "xmodel/airborne"
    {0x0083a990, 0x00984c90},  // "xmodel/wehrmacht"
    {0x0083a984, 0x00984c88},  // "worldspawn"
    {0x0083a97c, 0x00984c94},  // "auto_ai"
    {0x0083a970, 0x00984c98},  // "auto_nonai"
    {0x0083a968, 0x00984c9c},  // "manual"
    {0x0083a95c, 0x00984ca0},  // "manual_ai"
    {0x0083a948, 0x00984ca4},  // "turretstatechange"
    {0x0083a934, 0x00984ca8},  // "turretownerchange"
    {0x0083a928, 0x00984cac},  // "nophysics"
    {0x00832430, 0x00984cb0},  // "none"
    {0x0083a91c, 0x00984cb4},  // "obstacle"
    {0x0083a914, 0x00984cb8},  // "world"
    {0x0083a908, 0x00984cbc},  // "zonesloaded"
    {0x0083a8f4, 0x00984cc0},  // "groundEntChanged"
    {0x0083a8e8, 0x00984cc4},  // "<custom>"
    {0x0083a8dc, 0x00984cc8},  // "bigRotation"
    {0x0083a8d0, 0x00984ccc},  // "overheated"
};
// Script strings 0x00984cd0..0x00984cdc, assigned from the text.
const NameSlot kConstCStrings[] = {
    {0x0083208c, 0x00984cd0},  // "axis"
    {0x00832084, 0x00984cd4},  // "allies"
    {0x00831f14, 0x00984cd8},  // "neutral"
    {0x0083a8c8, 0x00984cdc},  // "dead"
};

// The persistent records of the owner at 0x0095f4e0: {key, value, count} at +0x560, count +0x568.
const uint32_t* persistent_begin() { return field<const uint32_t*>(title_global<uint8_t*>(0x0095f4e0), 0x560); }
const uint32_t* persistent_end() {
    uint8_t* const owner = title_global<uint8_t*>(0x0095f4e0);
    return field<const uint32_t*>(owner, 0x560) + field<uint32_t>(owner, 0x568) * 3;
}

// A HUD element (0x7c bytes at 0x0094cc88) by index.
uint8_t* hud_elem(int32_t index) { return &title_global<uint8_t>(static_cast<uint32_t>(index * 0x7c + 0x0094cc88)); }

// A float widened to double on the x87 (FLD DWORD / FSTP QWORD), as the title passes %g arguments.
double x87_widen(float value) {
    double result;
    __asm__ volatile("flds %1\n\tfstpl %0" : "=m"(result) : "m"(value));
    return result;
}

// The *OverTime builtins' range check: a time <= 0 or > 60 formats a message (va) and nothing else.
void check_hud_time(float time, uint32_t too_small, uint32_t too_large) {
    using Va = const char* XB_CDECL(const char*, double);
    if (time <= 0.0f) {  // comiss 0, time; jb: NaN goes on to the second test (also false)
        title_function<Va>(0x002a93d0)(&title_global<char>(too_small), x87_widen(time));  // va
    } else if (time > title_global<float>(0x0083eb3c)) {  // 60
        title_function<Va>(0x002a93d0)(&title_global<char>(too_large), x87_widen(time));  // va
    }
}

// time * 1000 + 0.5, truncated (CVTTSS2SI).
int32_t hud_time_ms(float time) {
    return static_cast<int32_t>(time * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24));
}

// va with only a format (the shipped build's script error messages: formatted, then dropped).
void va_message(uint32_t format) {
    title_function<const char* XB_CDECL(const char*)>(0x002a93d0)(&title_global<char>(format));  // va
}

// The vehicle (+0x248) of the handle's entity, or null.
uint8_t* handle_vehicle(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<uint8_t*>(ent, 0x248) : nullptr;
}

// The vehicle of a path vehicle builtin: message when the entity's +0x297 is 2 (used by a player).
uint8_t* path_vehicle(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return nullptr;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return nullptr;
    if (field<uint8_t>(ent, 0x297) == 2) va_message(0x008319f4);  // "Vehicle is invalid on path after it's been used"
    return vehicle;
}

// Turret builtins: message when the entity's health (+0x350, as a float) is <= 0.
void check_turret_health(uint8_t* ent) {
    if (static_cast<float>(field<int32_t>(ent, 0x350)) <= 0.0f)
        va_message(0x00831938);  // "Vehicle must have health to control the turret"
}

const char* title_text(uint32_t address) { return reinterpret_cast<const char*>(static_cast<uintptr_t>(address)); }

// A script string's text (+6 of its rep), null for the empty string.
const char* script_text(const ScriptString* s) {
    const uint32_t rep = *reinterpret_cast<const uint32_t*>(s);
    return rep ? reinterpret_cast<const char*>(static_cast<uintptr_t>(rep + 6)) : nullptr;
}

// The weapon slot a script string names (_stricmp over 0x008d23a8, 7 names); "none" (0) and unknown names
// format the message and give 0.
uint32_t weapon_slot(const ScriptString* name) {
    const char* const text = script_text(name);
    uint32_t slot = 0;
    for (uint32_t i = 0; i < 7; ++i) {
        if (title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(  // _stricmp
                text, title_text(title_global<uint32_t>(0x008d23a8 + i * 4))) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0)
        title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
            &title_global<char>(0x00831cb0), script_text(name));  // "Unknown weaponslot name %s. ..."
    return slot;
}

// The client (+0x23c) of the handle's entity, or null.
uint8_t* handle_client(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<uint8_t*>(ent, 0x23c) : nullptr;
}

uint8_t* weapon_def(int32_t weapon) { return title_global<uint8_t**>(0x0095f4d8)[weapon]; }  // bg_weaponDefs

// GScr_PlayerGetFlag*: bit Shift of the client byte at Offset (0 without a client).
template <uint32_t Offset, uint32_t Shift>
int32_t client_byte_bit(uint32_t handle) {
    uint8_t* const client = handle_client(handle);
    if (!client) return 0;
    return (static_cast<int32_t>(field<int8_t>(client, Offset)) & (1 << Shift)) >> Shift;
}

// The weapon a script string names: BG_FindWeaponIndexForHash (low byte) of its Com_HashKeyNoCase, 0 for the
// empty string.
uint32_t weapon_from_name(const ScriptString* name) {
    const char* const text = script_text(name);
    if (!text || !*text) return 0;
    return BG_FindWeaponIndexForHash(Com_HashKeyNoCase(0, text)) & 0xff;
}

bool owns_weapon(uint8_t* client, uint32_t weapon) {
    return (field<uint32_t>(client, 0x3f0 + (weapon >> 5) * 4) & (1u << (weapon & 0x1f))) != 0;
}

// The actor (+0x240) of the handle's entity, or null.
uint8_t* handle_actor(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<uint8_t*>(ent, 0x240) : nullptr;
}

// GScr_ActorSetLookRates: the angle reached so far (start minus the remaining msec times the rate, 0..180).
float look_angle_now(float start, int32_t end_time, float rate) {
    int32_t remaining = static_cast<int32_t>(static_cast<uint32_t>(end_time) - title_global<uint32_t>(0x0098540c));
    if (remaining < 0) remaining = 0;
    float angle = start - static_cast<float>(remaining) * rate;
    if (0.0f > angle)
        angle = 0.0f;
    else if (angle > title_global<float>(0x0083eb60))  // 180
        angle = title_global<float>(0x0083eb60);
    return angle;
}

// GScr_PlayerAllowPmFlag*: allow clears the flag of the client's +0x2c, else sets it.
template <uint32_t Flag>
void allow_pm_flag(uint32_t handle, uint32_t allow) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (static_cast<uint8_t>(allow))
        field<uint32_t>(client, 0x2c) &= ~Flag;
    else
        field<uint32_t>(client, 0x2c) |= Flag;
}

// GScr_SetEntFlag* (the 14-instruction ones): sets one flag of an entity's +0x2a4.
template <uint32_t Flag>
void set_ent_flag(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) field<uint32_t>(ent, 0x2a4) |= Flag;
}

uint8_t* hud_component(uint32_t offset) { return field<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), offset); }

}  // namespace

void XB_CDECL GScr_PhysicsLaunch(uint32_t handle, const float* point, const float* dir, float scale,
                                 uint32_t local_point) {
    alignas(16) float point_copy[4] = {point[0], point[1], point[2], point[0]};
    alignas(16) float dir_copy[4] = {dir[0], dir[1], dir[2], dir[0]};
    if (uint8_t* const ent = handle_entity(handle)) G_PhysicsLaunch(ent, point_copy, dir_copy, scale, local_point, 4);
}

void XB_CDECL GScr_PhysicsStop(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) G_FreeEntityPhysics(ent);
}

void XB_CDECL GScr_EmptyStub174() {}
void XB_CDECL GScr_EmptyStub173() {}

void XB_CDECL GScr_SetFogColor(float r, float g, float b) {
    title_global<float>(0x008db10c) = r;
    title_global<float>(0x008db110) = g;
    title_global<float>(0x008db114) = b;
}

void XB_CDECL GScr_SetFogDistances(float start, float end) {
    title_global<float>(0x0095f698) = start;
    title_global<float>(0x008db104) = end;
}

void XB_CDECL GScr_SetFogColorBytes(int32_t r, int32_t g, int32_t b) {
    const float scale = title_global<float>(0x0083eb30);  // 1/255
    title_global<float>(0x008db10c) = static_cast<float>(r) * scale;
    title_global<float>(0x008db110) = static_cast<float>(g) * scale;
    title_global<float>(0x008db114) = static_cast<float>(b) * scale;
}

void XB_CDECL GScr_LoadConsts() {
    uint8_t& loaded = title_global<uint8_t>(0x0098ad59);
    if (loaded) return;
    loaded = 1;
    for (const NameSlot& row : kConstHashes) {
        const char* const text = title_text(row.text);
        const uint32_t hash = Com_HashKeyNoCase(0, text);
        NameMap_Insert(title_function<uint32_t XB_CDECL(const char*)>(0x002f5470)(text), text);  // ScrIface_HashStringNoCase
        title_global<uint32_t>(row.slot) = hash;
    }
    uint32_t temp;
    for (const NameSlot& row : kConstStrings) {
        title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x0011eb50)(&temp, title_text(row.text));  // ScriptString_FromCString
        title_function<void XB_THISCALL(uint32_t, uint32_t*)>(0x0011d600)(row.slot, &temp);  // ScriptString_Assign
        title_function<void XB_THISCALL(uint32_t*)>(0x0011d640)(&temp);                     // ScriptString_Release
    }
    for (const NameSlot& row : kConstCStrings)
        title_function<void XB_THISCALL(uint32_t, const char*)>(0x0011dd20)(row.slot, title_text(row.text));  // ScriptString_AssignCString
}

void XB_CDECL GScr_ClearEntFlag4000000(uint32_t handle) { clear_ent_flag<0x4000000>(handle); }

void XB_CDECL GScr_SetEntFlag4000000(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    field<uint32_t>(ent, 0x2a4) |= 0x4000000;
    if (!field<uint8_t*>(ent, 0x22c)) return;
    for (uint32_t i = 0; i < field<uint8_t>(field<uint8_t*>(ent, 0x22c), 0x2e); ++i) {
        uint8_t* const tree = reinterpret_cast<uint8_t**>(field<uint8_t*>(ent, 0x22c))[i];
        if (tree) AnimTree_ClearNode_r(tree, 0);
    }
}

PLATFORM_ASM_ENTRY(GScr_ObjectiveStateFromString_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(GScr_ObjectiveStateFromString) "addl $8, %esp\n"
                   "ret")

uint8_t XB_CDECL GScr_ObjectiveStateFromString(const ScriptString* name, uint32_t* state) {
    static const uint32_t kNames[] = {0x00984b08, 0x00984ac4, 0x00984b74, 0x00984b04, 0x00984ae0, 0x00984b48};
    for (uint32_t i = 0; i < 6; ++i) {
        if (ScriptString_EqualsString(&title_global<ScriptString>(kNames[i]), name)) {
            *state = i;
            return 1;
        }
    }
    *state = 0;
    return 0;
}

void XB_CDECL GScr_SetFreezeControls(uint32_t freeze) {
    title_global<uint32_t>(0x00927314) = static_cast<uint8_t>(freeze);
}

void XB_CDECL GScr_EmptyStub448() {}
uint32_t XB_CDECL GScr_ReturnZero446() { return 0; }
uint32_t XB_CDECL GScr_ReturnZero445() { return 0; }
void XB_CDECL GScr_EmptyStub561() {}
void XB_CDECL GScr_EmptyStub560() {}

void XB_CDECL GScr_EmptyStub559() {}
void XB_CDECL GScr_EmptyStub399() {}
void XB_CDECL GScr_EmptyStub397() {}

void XB_CDECL GScr_Set8db19c(float value) { title_global<float>(0x008db19c) = value; }
void XB_CDECL GScr_Set8db198(float value) { title_global<float>(0x008db198) = value; }
void XB_CDECL GScr_Set8d20e0(float value) { title_global<float>(0x008d20e0) = value; }

void XB_CDECL GScr_GetPadStick(int32_t stick, float* x, float* y) {
    const int32_t active = title_global<int32_t>(0x008d50d8);
    *x = 0.0f;
    *y = 0.0f;
    const int32_t pad = active == -1 ? 0 : active;
    int32_t ix, iy;
    IN_PadCreate();
    IN_PadGetStick(pad, stick, &ix, &iy);
    const float scale = title_global<float>(0x0083ec60);  // 1/127
    *x = static_cast<float>(ix) * scale;
    *y = static_cast<float>(iy) * scale;
}

void XB_CDECL GScr_Set95f4b4(uint32_t value) { title_global<uint8_t>(0x0095f4b4) = static_cast<uint8_t>(value); }

void XB_CDECL GScr_SetFogPassCount(int32_t count) { title_global<int32_t>(0x00897960) = count; }
void XB_CDECL GScr_SetFogPassAlpha(float alpha) { title_global<float>(0x0089795c) = alpha; }
void XB_CDECL GScr_SetFogPassStep(float step) { title_global<float>(0x00897958) = step; }
void XB_CDECL GScr_SetFogPassFinalAlpha(float alpha) { title_global<float>(0x00897954) = alpha; }

void XB_CDECL GScr_SetHudVisible(uint32_t visible) {
    title_global<uint8_t>(0x0095f91f) = static_cast<uint8_t>(visible) == 0;
}

void XB_CDECL GScr_EmptyStub221() {}
void XB_CDECL GScr_EmptyStub220() {}
void XB_CDECL GScr_EmptyStub223() {}
void XB_CDECL GScr_EmptyStub222() {}
void XB_CDECL GScr_EmptyStub200() {}
void XB_CDECL GScr_EmptyStub199() {}

void XB_CDECL GScr_SetMissileMaxLifetime(float seconds) {
    title_global<float>(0x00985ffc) = seconds * title_global<float>(0x0083eb9c);  // 1000
}

void XB_CDECL GScr_SetCompassMarkersEnabled(uint32_t enabled) {
    title_global<uint32_t>(0x00986008) = static_cast<uint8_t>(enabled);
}

void XB_CDECL GScr_SetRadiusDamageFlag(uint32_t enabled) {
    title_global<uint32_t>(0x00986010) = static_cast<uint8_t>(enabled);
}

void XB_CDECL GScr_EmptyStub184() {}
void XB_CDECL GScr_EmptyStub183() {}

uint32_t XB_CDECL GScr_GetDifficulty(ScriptString* out) {
    const uint32_t text = title_global<uint32_t>(0x008d20d0 + title_global<uint32_t>(0x00950b3c) * 4);
    return title_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(out, text);  // ScriptString_AssignCString
}

int32_t XB_CDECL GScr_GetTime() { return title_global<int32_t>(0x0098540c); }

uint8_t XB_CDECL GScr_ReturnFalse511() { return 0; }
uint8_t XB_CDECL GScr_ReturnFalse510() { return 0; }
uint8_t XB_CDECL GScr_ReturnFalse509() { return 0; }
void XB_CDECL GScr_EmptyStub21() {}
void XB_CDECL GScr_EmptyStub22() {}
void XB_CDECL GScr_EmptyStub14() {}

uint8_t XB_CDECL GScr_CopyStringFromEngine(uint32_t id, char* buf, uint32_t size) {
    uint8_t* const engine = title_global<uint8_t*>(0x0095f49c);
    const char* const text = title_function<const char* XB_CDECL(uint32_t)>(field<uint32_t>(engine, 0x870))(id);
    if (!text) return 0;
    uint32_t count = static_cast<uint32_t>(std::strlen(text));
    if (count > size - 1) count = size - 1;
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(buf, text, count);  // _strncpy
    buf[count] = 0;
    return 1;
}

void XB_CDECL GScr_PushToList95fdf0(uint32_t value) {
    const int32_t count = title_global<int16_t>(0x0095fdf6);
    if (count >= title_global<uint16_t>(0x0095fdf4)) return;
    title_global<uint32_t*>(0x0095fdf0)[count] = value;
    ++title_global<int16_t>(0x0095fdf6);
}

void XB_CDECL GScr_EmptyStub1() {}
void XB_CDECL GScr_EmptyStub0() {}

float* XB_CDECL GScr_GetPersistentVector(float* out, uint32_t key) {
    const uint32_t* const end = persistent_end();
    const float none = title_global<float>(0x00922eb8);
    uint32_t value[3];
    std::memcpy(&value[0], &none, 4);
    value[1] = value[0];
    value[2] = value[0];
    for (const uint32_t* record = persistent_begin(); record != end; record += 3) {
        if (record[0] == 0 || record[0] != key) continue;
        value[0] = record[1];
        for (uint32_t i = 1; i < 3; ++i) {
            record += 3;
            if (record[0] != 0) break;
            value[i] = record[1];
        }
        break;
    }
    std::memcpy(out, value, 12);
    return out;
}

float XB_CDECL GScr_GetPersistentFloat(uint32_t key) {
    const uint32_t* const end = persistent_end();
    for (const uint32_t* record = persistent_begin(); record != end; record += 3)
        if (record[0] != 0 && record[0] == key) return reinterpret_cast<const float&>(record[1]);
    return title_global<float>(0x00922eb8);
}

uint32_t XB_CDECL GScr_GetPersistentInt(uint32_t key) {
    const uint32_t* const end = persistent_end();
    for (const uint32_t* record = persistent_begin(); record != end; record += 3)
        if (record[0] != 0 && record[0] == key) return record[1];
    return 0xfefefefe;
}

void XB_CDECL GScr_SetPersistentVector(uint32_t key, const uint32_t* value) {
    const uint32_t copy[3] = {value[0], value[1], value[2]};
    Persistent_SetValue(title_global<uint8_t*>(0x0095f4e0), key, 3, copy);
}

void XB_CDECL GScr_SetPersistentFloat(uint32_t key, uint32_t value) {
    Persistent_SetValue(title_global<uint8_t*>(0x0095f4e0), key, 1, &value);
}

void XB_CDECL GScr_SetPersistentInt(uint32_t key, uint32_t value) {
    Persistent_SetValue(title_global<uint8_t*>(0x0095f4e0), key, 1, &value);
}

void XB_CDECL GScr_Set964b84(float value, uint32_t word) {
    title_global<float>(0x00964b84) = value;
    title_global<uint32_t>(0x00964b88) = word;
}

void XB_CDECL GScr_SuppressObjectiveUpdateMessage(uint32_t a, uint32_t b) {
    uint8_t* const objectives = field<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 4);
    field<uint32_t>(objectives, 0x13d4) = a;
    field<uint32_t>(objectives, 0x13d8) = b;
}

void XB_CDECL GScr_HudObjectives_Set13d0(uint32_t a, uint32_t b) {
    uint8_t* const objectives = field<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 4);
    field<uint16_t>(objectives, 0x13d0) = static_cast<uint16_t>(a);
    field<uint16_t>(objectives, 0x13d2) = static_cast<uint16_t>(b);
}

void XB_CDECL GScr_GetSpgVariant(const char* name, uint32_t* a, uint32_t* b, uint32_t* c) {
    uint32_t variant[3] = {0, 0, 0};
    const uint32_t key = Com_HashKeyNoCase(0, name);
    Spg_FindRandomVariant(title_global<uint8_t*>(0x0095f564), variant, key);
    if (!variant[0]) {
        *a = 0;
        *b = 0;
        *c = 0;
        return;
    }
    *a = variant[0];
    *b = variant[1];
    *c = variant[2];
}

uint32_t XB_CDECL GScr_IsNotMinusOne(int32_t value) { return value != -1; }
uint32_t XB_CDECL GScr_IsValidHandle(int32_t value) { return value != 0 && value != -1; }

void XB_CDECL GScr_EntList190_Disable(uint32_t key) { G_EntList190_Disable(title_global<uint8_t*>(0x0095f4f8), key); }
void XB_CDECL GScr_EntList190_Enable(uint32_t key) { G_EntList190_Enable(title_global<uint8_t*>(0x0095f4f8), key); }

void XB_CDECL GScr_HudDestroy(const int32_t* hud) {
    if (*hud >= 0) field<uint32_t>(hud_elem(*hud), 0) = 0;
}

void XB_CDECL GScr_HudMoveOverTime(const int32_t* hud, float time) {
    uint8_t* const elem = hud_elem(*hud);
    check_hud_time(time, 0x00832370, 0x0083235c);  // "move time %g <= 0", "move time %g > 60"
    field<int32_t>(elem, 0x54) = title_global<int32_t>(0x0098540c);
    field<int32_t>(elem, 0x58) = hud_time_ms(time);
    field<uint32_t>(elem, 0x4c) = field<uint32_t>(elem, 0x4);
    field<uint32_t>(elem, 0x50) = field<uint32_t>(elem, 0x8);
}

void XB_CDECL GScr_HudScaleOverTime(const int32_t* hud, float time, uint32_t width, uint32_t height) {
    uint8_t* const elem = hud_elem(*hud);
    check_hud_time(time, 0x00832398, 0x00832384);  // "scale time %g <= 0", "scale time %g > 60"
    field<int32_t>(elem, 0x44) = title_global<int32_t>(0x0098540c);
    field<int32_t>(elem, 0x48) = hud_time_ms(time);
    field<uint32_t>(elem, 0x3c) = field<uint32_t>(elem, 0x30);
    field<uint32_t>(elem, 0x40) = field<uint32_t>(elem, 0x34);
    field<uint32_t>(elem, 0x30) = width;
    field<uint32_t>(elem, 0x34) = height;
}

void XB_CDECL GScr_HudFadeOverTime(const int32_t* hud, float time) {
    uint8_t* const elem = hud_elem(*hud);
    check_hud_time(time, 0x008323c0, 0x008323ac);  // "fade time %g <= 0", "fade time %g > 60"
    field<int32_t>(elem, 0x24) = title_global<int32_t>(0x0098540c);
    field<int32_t>(elem, 0x28) = hud_time_ms(time);
    field<uint32_t>(elem, 0x20) = field<uint32_t>(elem, 0x1c);
}

void XB_CDECL GScr_HudSetValue(const int32_t* hud, float value) {
    uint8_t* const elem = hud_elem(*hud);
    field<float>(elem, 0x64) = 0.0f;
    static const uint32_t kCleared[] = {0x30, 0x34, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x5c, 0x60, 0x68};
    for (uint32_t offset : kCleared) field<uint32_t>(elem, offset) = 0;
    field<uint32_t>(elem, 0) = 2;
    field<float>(elem, 0x64) = value;
}

void XB_CDECL GScr_ZoneClearField34(uint8_t* zone) {
    if (zone) field<uint32_t>(zone, 0x34) = title_global<uint32_t>(0x0082eed0);  // FLT_MAX
}

void XB_CDECL GScr_ZoneSetField34(uint8_t* zone, float value) {
    if (!zone) return;
    field<float>(zone, 0x34) = value;
    ++title_global<uint32_t>(0x008d9d38);
}

uint8_t XB_CDECL GScr_IsZoneLoaded(const uint8_t* zone) {
    if (!zone) return 0;
    const int32_t index = *reinterpret_cast<const int32_t*>(zone + 0x28);
    if (index == -1) return 0;
    uint8_t* const entry = field<uint8_t*>(title_global<uint8_t*>(0x0095f638), 0x38 + index * 4);
    if (!entry) return 0;
    return field<uint32_t>(entry, 0xdc) == 0;
}

float XB_CDECL GScr_GetFrameTime() { return title_global<float>(0x008ddaa0); }

void XB_CDECL GScr_SetMusicTrack(float value) {
    uint8_t* const music = title_global<uint8_t*>(0x0095f4cc);
    field<float>(music, 0x14) = value;
    field<uint32_t>(music, 0x1c) = 2;
    field<float>(music, 0x18) = 0.0f;
}

uint8_t XB_CDECL GScr_Vehicle_Is180Clear(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return 0;
    if (field<uint8_t>(ent, 0x297) != 2) va_message(0x00831888);  // "Must be called on a player controlled vehicle"
    return field<int32_t>(vehicle, 0x180) <= 0;
}

void XB_CDECL GScr_Vehicle_ClearTurretTarget(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    field<uint32_t>(vehicle, 0x1c8) = 0;
    field<uint32_t>(vehicle, 0x1cc) = 0;
    for (uint32_t offset = 0x1d0; offset <= 0x1e4; offset += 4) field<float>(vehicle, offset) = 0.0f;
}

void XB_CDECL GScr_Vehicle_SetTurretTargetEnt(uint32_t handle, uint32_t target, const uint32_t* offset) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    check_turret_health(ent);
    uint8_t* const other = handle_entity(target);
    if (!other) return;
    field<uint32_t>(vehicle, 0x1c8) = 1;
    field<uint32_t>(vehicle, 0x1cc) = field<uint32_t>(other, 0x224);
    if (Vec3_IsNonZero(0, 0, reinterpret_cast<const float*>(offset))) {
        field<uint32_t>(vehicle, 0x1dc) = offset[0];
        field<uint32_t>(vehicle, 0x1e0) = offset[1];
        field<uint32_t>(vehicle, 0x1e4) = offset[2];
    } else {
        field<float>(vehicle, 0x1dc) = 0.0f;
        field<float>(vehicle, 0x1e0) = 0.0f;
        field<float>(vehicle, 0x1e4) = 0.0f;
    }
}

void XB_CDECL GScr_Vehicle_SetTurretTargetVec(uint32_t handle, const uint32_t* point) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    check_turret_health(ent);
    field<uint32_t>(vehicle, 0x1c8) = 1;
    field<uint32_t>(vehicle, 0x1cc) = 0;
    field<uint32_t>(vehicle, 0x1d0) = point[0];
    field<uint32_t>(vehicle, 0x1d4) = point[1];
    field<uint32_t>(vehicle, 0x1d8) = point[2];
    field<float>(vehicle, 0x1dc) = 0.0f;
    field<float>(vehicle, 0x1e0) = 0.0f;
    field<float>(vehicle, 0x1e4) = 0.0f;
}

void XB_CDECL GScr_Vehicle_Clear190(uint32_t handle) {
    if (uint8_t* const vehicle = handle_vehicle(handle)) field<uint32_t>(vehicle, 0x190) = 0;
}

void XB_CDECL GScr_Vehicle_Set190(uint32_t handle, uint32_t value) {
    uint8_t* const vehicle = handle_vehicle(handle);
    if (!vehicle) return;
    field<uint32_t>(vehicle, 0x190) = 1;
    field<uint32_t>(vehicle, 0x194) = static_cast<uint8_t>(value);
}

void XB_CDECL GScr_Vehicle_MakeUsable(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (handle_entity(field<uint32_t>(ent, 0x1a0))) va_message(0x0083197c);  // "Vehicle is already in use"
    field<uint32_t>(ent, 0x2a0) &= ~1u;
    field<uint32_t>(ent, 0x134) &= ~0x200000u;
}

void XB_CDECL GScr_Vehicle_MakeUnusable(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    field<uint32_t>(ent, 0x2a0) |= 1;
    field<uint32_t>(ent, 0x134) |= 0x200000;
}

void XB_CDECL GScr_Vehicle_Clear200(uint32_t handle) {
    if (uint8_t* const vehicle = handle_vehicle(handle)) field<uint32_t>(vehicle, 0x200) = 0;
}

void XB_CDECL GScr_Vehicle_Set200(uint32_t handle) {
    if (uint8_t* const vehicle = handle_vehicle(handle)) field<uint32_t>(vehicle, 0x200) = 1;
}

uint32_t XB_CDECL GScr_Vehicle_GetOwnerHandle(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    const uint32_t owner = field<uint32_t>(ent, 0x1a0);
    return handle_entity(owner) ? owner : 0;
}

long double XB_CDECL GScr_Vehicle_GetSpeedMPH(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return title_global<float>(0x0083eafc);
    return platform_x87_ld(field<float>(ent, 0x2fc)) * title_global<float>(0x0083ee80);
}

void XB_CDECL GScr_Vehicle_GetWheelSurface(uint32_t handle, uint32_t wheel, ScriptString* out) {
    uint8_t* const vehicle = handle_vehicle(handle);
    if (!vehicle) return;
    const uint32_t def = title_global<uint32_t>(0x00950a28 + field<int16_t>(vehicle, 0x178) * 4);  // g_vehicleDefs
    const uint16_t type = field<uint16_t>(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(def)), 0x20);
    if (type != 2 && type != 1)
        title_function<const char* XB_CDECL(const char*, uint32_t)>(0x002a93d0)(  // va
            &title_global<char>(0x00831998), def);  // "Vehicle type [%s] has no wheels\n"
    static const uint32_t kWheels[] = {0x0097a7c8, 0x0097a7cc, 0x0097a7d0, 0x0097a7d4, 0x0097a7d8, 0x0097a7dc};
    uint32_t index = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        if (wheel == title_global<uint32_t>(kWheels[i])) {
            index = i;
            break;
        }
    }
    const int32_t surface = field<int32_t>(vehicle, 0x150 + index * 4);
    uint32_t text;
    if (surface == 0)
        text = 0x00832430;  // "none"
    else if (surface < 0 || surface >= 0x17)
        text = 0x00831f70;  // "default"
    else
        text = title_global<uint32_t>(0x008d4d08 + surface * 16);
    title_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(out, text);  // ScriptString_AssignCString
}

void XB_CDECL GScr_Vehicle_SetSpeedMode2(uint32_t handle, float speed) {
    uint8_t* const vehicle = path_vehicle(handle);
    if (!vehicle) return;
    const float scaled = speed * title_global<float>(0x0083ee64);
    field<uint32_t>(vehicle, 0x1b0) = 2;
    field<float>(vehicle, 0x1b8) = scaled;
}

void XB_CDECL GScr_Vehicle_SetSpeed(uint32_t handle, float speed, float accel) {
    uint8_t* const vehicle = path_vehicle(handle);
    if (!vehicle) return;
    const float k = title_global<float>(0x0083ee64);
    field<float>(vehicle, 0x1b4) = speed * k;
    const float scaled = accel * k;
    field<uint32_t>(vehicle, 0x1b0) = 1;
    field<float>(vehicle, 0x1b8) = scaled;
}

void XB_CDECL GScr_Vehicle_SetMaxSpeed(uint32_t handle, float speed) {
    uint8_t* const vehicle = path_vehicle(handle);
    if (!vehicle) return;
    field<float>(vehicle, 0x17c) = speed * title_global<float>(0x0083ee64);
}

void XB_CDECL GScr_Vehicle_SetWaitNode(uint32_t handle, const int32_t* node) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || *node == -1) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    if (field<uint8_t>(ent, 0x297) == 2) va_message(0x008319f4);  // "Vehicle is invalid on path after it's been used"
    field<uint16_t>(vehicle, 0x17a) = *reinterpret_cast<const uint16_t*>(node);
    field<uint32_t>(vehicle, 0x2f8) = 0;
}

void XB_CDECL GScr_Vehicle_SetSwitchNode(uint32_t handle, const int32_t* from, const int32_t* to) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const int32_t a = *from;
    if (a == -1) return;
    const int32_t b = *to;
    if (b == -1) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    if (field<uint8_t>(ent, 0x297) == 2) va_message(0x008319f4);  // "Vehicle is invalid on path after it's been used"
    Vehicle_SetSwitchNodes(static_cast<uint32_t>(a), vehicle, static_cast<uint32_t>(b));
}

void XB_CDECL GScr_Vehicle_StartPath(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    if (field<uint8_t>(ent, 0x297) == 2) va_message(0x008319f4);  // "Vehicle is invalid on path after it's been used"
    if (field<int16_t>(vehicle, 0) < 0) va_message(0x008319bc);  // "Can't start path on a vehicle that hasn't been attached"
    field<uint8_t>(ent, 0x297) = 1;
}

int32_t XB_CDECL GScr_PlayerGetWeaponSlotClipSize(uint32_t handle, const ScriptString* slot) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return 0;
    const uint32_t index = weapon_slot(slot);
    const int32_t weapon = field<int8_t>(field<uint8_t*>(ent, 0x23c), 0x3f8 + index);
    if (!weapon) return 0;
    return field<int32_t>(weapon_def(weapon), 0x5a8);
}

void XB_CDECL GScr_PlayerSetWeaponSlotClipAmmo(uint32_t handle, const ScriptString* slot, int32_t count) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    const uint32_t index = weapon_slot(slot);
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    const int32_t weapon = field<int8_t>(client, 0x3f8 + index);
    if (!weapon) return;
    const uint32_t clip = field<uint32_t>(weapon_def(weapon), 0x5a0);
    if (!clip) return;
    int32_t value = count < 0 ? 0 : count;
    const int32_t max = title_global<int32_t>(0x00935338 + clip * 4);
    if (value > max) value = max;
    field<int32_t>(client, 0x280 + clip * 4) = value;
}

int32_t XB_CDECL GScr_PlayerGetWeaponSlotClipAmmo(uint32_t handle, const ScriptString* slot) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return 0;
    const uint32_t index = weapon_slot(slot);
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    const int32_t weapon = field<int8_t>(client, 0x3f8 + index);
    if (!weapon) return 0;
    const uint32_t clip = field<uint32_t>(weapon_def(weapon), 0x5a0);
    if (!clip) return 0;
    return field<int32_t>(client, 0x280 + clip * 4);
}

void XB_CDECL GScr_PlayerSetWeaponSlotAmmo(uint32_t handle, const ScriptString* slot, int32_t count) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    const uint32_t index = weapon_slot(slot);
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    const int32_t weapon = field<int8_t>(client, 0x3f8 + index);
    if (!weapon) return;
    uint8_t* const def = weapon_def(weapon);
    uint32_t ammo, base, maxes;
    if (field<uint32_t>(def, 0x6ec)) {
        ammo = field<uint32_t>(def, 0x5a0), base = 0x280, maxes = 0x00935338;
    } else {
        ammo = field<uint32_t>(def, 0x598), base = 0x110, maxes = 0x00929818;
    }
    if (!ammo) return;
    if (count < 0) {
        field<int32_t>(client, base + ammo * 4) = 0;
        return;
    }
    const int32_t max = title_global<int32_t>(maxes + ammo * 4);
    field<int32_t>(client, base + ammo * 4) = count > max ? max : count;
}

int32_t XB_CDECL GScr_PlayerGetWeaponSlotAmmo(uint32_t handle, const ScriptString* slot) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return 0;
    const uint32_t index = weapon_slot(slot);
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    const int32_t weapon = field<int8_t>(client, 0x3f8 + index);
    if (!weapon) return 0;
    uint8_t* const def = weapon_def(weapon);
    if (field<uint32_t>(def, 0x6ec)) return field<int32_t>(client, 0x280 + field<uint32_t>(def, 0x5a0) * 4);
    return field<int32_t>(client, 0x110 + field<uint32_t>(def, 0x598) * 4);
}

void XB_CDECL GScr_CheckWeaponSlot(uint32_t handle, const ScriptString* slot, const ScriptString* weapon_name) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    // (The original keeps the entity in its handle argument slot: callee scratch, not compared.)
    if (!field<uint8_t*>(ent, 0x23c)) return;
    const uint32_t index = weapon_slot(slot);
    uint32_t weapon = 0;
    const char* const text = script_text(weapon_name);
    const bool none = text && Q_stricmpn(text, title_text(0x00832430), 99999) == 0;  // "none"
    if (!none) {
        const char* const name = script_text(weapon_name);
        if (name && *name) weapon = BG_FindWeaponIndexForHash(Com_HashKeyNoCase(0, name)) & 0xff;
        if (!weapon)
            title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
                &title_global<char>(0x00831c9c), script_text(weapon_name));  // "Unknown weapon %s."
        const uint32_t kind = field<uint32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x9c);
        const bool mixed_primary = (kind == 1 || kind == 2) && (index == 1 || index == 2);
        if (kind != index && !mixed_primary)
            title_function<const char* XB_CDECL(const char*, const char*, const char*, const char*)>(0x002a93d0)(  // va
                &title_global<char>(0x00831c60), script_text(weapon_name),  // "Weapon %s goes in the %s weaponslot, ..."
                title_text(title_global<uint32_t>(0x008d23a8 + kind * 4)),
                title_text(title_global<uint32_t>(0x008d23a8 + index * 4)));
    }
    const int32_t old = field<int8_t>(field<uint8_t*>(ent, 0x23c), 0x3f8 + index);
    if (old) BG_RemoveWeaponFromSlot(static_cast<uint32_t>(old), field<uint8_t*>(ent, 0x23c));
    if (!weapon) return;
    const bool move_to_b = index == 2 && field<uint8_t>(field<uint8_t*>(ent, 0x23c), 0x3f9) == 0;
    BG_GivePlayerWeapon(weapon, field<uint8_t*>(ent, 0x23c));
    if (!move_to_b) return;
    field<uint8_t>(field<uint8_t*>(ent, 0x23c), 0x3fa) = field<uint8_t>(field<uint8_t*>(ent, 0x23c), 0x3f9);
    field<uint8_t>(field<uint8_t*>(ent, 0x23c), 0x3f9) = 0;
}

void XB_CDECL GScr_PlayerGetWeaponSlotWeapon(uint32_t handle, const ScriptString* slot, ScriptString* out) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    const uint32_t index = weapon_slot(slot);
    const int32_t weapon = field<int8_t>(field<uint8_t*>(ent, 0x23c), 0x3f8 + index);
    if (!weapon) {
        title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d600)(out, 0x00984cb0);  // ScriptString_Assign
        return;
    }
    title_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(  // ScriptString_AssignCString
        out, field<uint32_t>(weapon_def(weapon), 0x8));
}

void XB_CDECL GScr_PlayerSetNotFlag_630(uint32_t handle, uint32_t value) {
    if (uint8_t* const client = handle_client(handle)) field<uint32_t>(client, 0x630) = static_cast<uint8_t>(value) == 0;
}

uint8_t XB_CDECL GScr_PlayerIsLookingAt(uint32_t handle, uint32_t target) {
    uint8_t* const client = handle_client(handle);
    if (!client) return 0;
    uint8_t* const other = handle_entity(target);
    if (!other) return 0;
    return field<uint8_t*>(client, 0x5f4) == other;
}

void XB_CDECL GScr_PlayerSetInt_58c(uint32_t handle, uint32_t value) {
    if (uint8_t* const client = handle_client(handle)) field<uint32_t>(client, 0x58c) = static_cast<uint8_t>(value);
}

void XB_CDECL GScr_PlayerAllowPmFlag800000(uint32_t handle, uint32_t allow) { allow_pm_flag<0x800000>(handle, allow); }
void XB_CDECL GScr_PlayerAllowPmFlag400000(uint32_t handle, uint32_t allow) { allow_pm_flag<0x400000>(handle, allow); }
void XB_CDECL GScr_PlayerAllowPmFlag200000(uint32_t handle, uint32_t allow) { allow_pm_flag<0x200000>(handle, allow); }
void XB_CDECL GScr_PlayerAllowPmFlag100000(uint32_t handle, uint32_t allow) { allow_pm_flag<0x100000>(handle, allow); }
void XB_CDECL GScr_PlayerAllowPmFlag80000(uint32_t handle, uint32_t allow) { allow_pm_flag<0x80000>(handle, allow); }

void XB_CDECL GScr_PlayerAllowEFlag100000(uint32_t handle, uint32_t allow) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    if (static_cast<uint8_t>(allow))
        field<uint32_t>(ent, 0x2a4) &= ~0x100000u;
    else
        field<uint32_t>(ent, 0x2a4) |= 0x100000;
}

int32_t XB_CDECL GScr_PlayerGetFlag20_2c(uint32_t handle) { return client_byte_bit<0x2c, 5>(handle); }
int32_t XB_CDECL GScr_PlayerGetFlag20_594(uint32_t handle) { return client_byte_bit<0x594, 5>(handle); }
int32_t XB_CDECL GScr_PlayerGetFlag1_594(uint32_t handle) { return client_byte_bit<0x594, 0>(handle); }
int32_t XB_CDECL GScr_PlayerGetFlag40_594(uint32_t handle) { return client_byte_bit<0x594, 6>(handle); }

uint32_t* XB_CDECL GScr_PlayerGetAngles(uint32_t* out, uint32_t handle) {
    uint8_t* const client = handle_client(handle);
    if (!client) {
        const uint32_t none = title_global<uint32_t>(0x00922eb8);
        out[0] = none;
        out[1] = none;
        out[2] = none;
        return out;
    }
    out[0] = field<uint32_t>(client, 0x9c);
    out[1] = field<uint32_t>(client, 0xa0);
    out[2] = field<uint32_t>(client, 0xa4);
    return out;
}

void XB_CDECL GScr_PlayerSetVec_10(uint32_t handle, const uint32_t* value) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x10) = value[0];
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x14) = value[1];
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x18) = value[2];
}

long double XB_CDECL GScr_PlayerGetWeaponMaxAmmoFraction(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const client = handle_client(handle);
    if (!client) return title_global<float>(0x0083eafc);  // 0
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!owns_weapon(client, weapon)) return title_global<float>(0x0083eb20);  // 1
    uint8_t* const def = weapon_def(static_cast<int32_t>(weapon));
    uint32_t ammo_offset, max_table, index;
    if (field<uint32_t>(def, 0x6ec))
        index = field<uint32_t>(def, 0x5a0), ammo_offset = 0x280, max_table = 0x00935338;
    else
        index = field<uint32_t>(def, 0x598), ammo_offset = 0x110, max_table = 0x00929818;
    const int32_t max = title_global<int32_t>(max_table + index * 4);
    if (max < 1) return title_global<float>(0x0083eb20);
    const int32_t ammo = field<int32_t>(client, ammo_offset + index * 4);
    if (ammo < 1) return title_global<float>(0x0083eafc);
    return static_cast<long double>(ammo) / max;  // FILD, FIDIV
}

long double XB_CDECL GScr_GetWeaponAmmoFraction(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const client = handle_client(handle);
    if (!client) return title_global<float>(0x0083eafc);  // 0
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!owns_weapon(client, weapon)) return title_global<float>(0x0083eb20);  // 1
    uint8_t* const def = weapon_def(static_cast<int32_t>(weapon));
    const int32_t max = field<int32_t>(def, 0x590);
    if (max < 1) return title_global<float>(0x0083eb20);
    const int32_t ammo = field<int32_t>(client, 0x110 + field<uint32_t>(def, 0x598) * 4);
    if (ammo < 1) return title_global<float>(0x0083eafc);
    return static_cast<long double>(ammo) / max;  // FILD, FIDIV
}

uint8_t XB_CDECL GScr_PlayerHasWeapon(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const client = handle_client(handle);
    if (!client) return 0;
    return owns_weapon(client, weapon_from_name(weapon_name));
}

uint8_t XB_CDECL GScr_PlayerGetWeaponTotalAmmo(uint32_t handle, const ScriptString* weapon_name, int32_t* total) {
    *total = 0;
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return 0;
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!owns_weapon(field<uint8_t*>(ent, 0x23c), weapon)) return 0;
    uint8_t* const def = weapon_def(static_cast<int32_t>(weapon));
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    *total = field<int32_t>(client, 0x280 + field<uint32_t>(def, 0x5a0) * 4) +
             field<int32_t>(client, 0x110 + field<uint32_t>(def, 0x598) * 4);
    return 1;
}

void XB_CDECL GScr_PlayerGetCurrentWeapon(uint32_t handle, ScriptString* out) {
    uint8_t* const client = handle_client(handle);
    if (!client) return;
    const int32_t weapon = field<int32_t>(client, 0x80);
    const uint32_t text = weapon > 0 ? field<uint32_t>(weapon_def(weapon), 0x8) : 0x00832430;  // "none"
    title_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(out, text);  // ScriptString_AssignCString
}

void XB_CDECL GScr_PlayerTakeAllWeapons(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x80) = 0;
    uint8_t** const defs = title_global<uint8_t**>(0x0095f4d8);
    for (int32_t weapon = 1; weapon <= last; ++weapon) {
        field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x110 + field<uint32_t>(defs[weapon], 0x598) * 4) = 0;
        field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x280 + field<uint32_t>(defs[weapon], 0x5a0) * 4) = 0;
        BG_RemoveWeaponFromSlot(static_cast<uint32_t>(weapon), field<uint8_t*>(ent, 0x23c));
    }
}

void XB_CDECL GScr_PlayerTakeWeapon(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    const uint32_t weapon = weapon_from_name(weapon_name);
    uint8_t** const defs = title_global<uint8_t**>(0x0095f4d8);
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x110 + field<uint32_t>(defs[weapon], 0x598) * 4) = 0;
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x280 + field<uint32_t>(defs[weapon], 0x5a0) * 4) = 0;
    BG_RemoveWeaponFromSlot(weapon, field<uint8_t*>(ent, 0x23c));
}

void XB_CDECL GScr_ActorSetFlag2000000(uint32_t handle, uint32_t allow) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    if (static_cast<uint8_t>(allow))
        field<uint32_t>(actor, 0x310) &= ~0x2000000u;
    else
        field<uint32_t>(actor, 0x310) |= 0x2000000;
}

void XB_CDECL GScr_ActorGetPathNodeOrOrigin(uint32_t handle, uint32_t* out) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    const uint32_t* from;
    const int16_t point = field<int16_t>(actor, 0x746);
    if (field<int16_t>(actor, 0x740) <= 0 || point == 0) {
        from = reinterpret_cast<const uint32_t*>(field<uint8_t*>(actor, 0) + 0x140);
    } else {
        const uint8_t* const node = PathNode_FromHandleInTable(
            reinterpret_cast<const uint16_t*>(actor + 0x3bc + point * 0x1c), title_global<uint8_t*>(0x0095f4f4));
        from = reinterpret_cast<const uint32_t*>(node + 0x44);
    }
    out[0] = from[0];
    out[1] = from[1];
    out[2] = from[2];
}

void XB_CDECL GScr_ActorSetLookRates(uint32_t handle, float yaw, float pitch, float time) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (yaw == field<float>(actor, 0x208) && pitch == field<float>(actor, 0x20c)) return;
    const int32_t end_time = field<int32_t>(actor, 0x21c);
    const float yaw_now = look_angle_now(field<float>(actor, 0x208), end_time, field<float>(actor, 0x220));
    field<float>(actor, 0x208) = yaw;
    const float yaw_delta = yaw - yaw_now;
    const float pitch_now = look_angle_now(field<float>(actor, 0x20c), end_time, field<float>(actor, 0x224));
    field<float>(actor, 0x20c) = pitch;
    field<int32_t>(actor, 0x21c) =
        title_global<int32_t>(0x0098540c) - static_cast<int32_t>(time * title_global<float>(0x0083ec74));  // -1000
    if (time == 0.0f) {
        field<float>(actor, 0x220) = title_global<float>(0x0083ece4);  // 9999
        field<float>(actor, 0x224) = title_global<float>(0x0083ece4);
        return;
    }
    const float inverse = title_global<float>(0x0083eb4c) / time;  // 0.001
    field<float>(actor, 0x220) = platform_mulss(inverse, yaw_delta);
    field<float>(actor, 0x224) = platform_mulss(pitch - pitch_now, inverse);
}

void XB_CDECL GScr_ActorStopLookAt(uint32_t handle, float range) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (range > title_global<float>(0x0083eb5c)) field<float>(actor, 0x200) = range;  // 20
    field<uint32_t>(actor, 0x1e8) = 0;
}

void XB_CDECL GScr_ActorLookAt(uint32_t handle, const uint32_t* point, float range) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    const float minimum = title_global<float>(0x0083eb5c);  // 20
    field<uint32_t>(actor, 0x1ec) = point[0];
    field<uint32_t>(actor, 0x1f0) = point[1];
    field<uint32_t>(actor, 0x1f4) = point[2];
    if (range > minimum) field<float>(actor, 0x200) = range;
    if (minimum > field<float>(actor, 0x200)) field<float>(actor, 0x200) = minimum;
    field<uint32_t>(actor, 0x1e8) = 1;
}

void XB_CDECL GScr_EmptyStub398() {}

void XB_CDECL GScr_ActorTeleport(uint32_t handle, const uint32_t* origin, const uint32_t* angles) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    const float to_short = title_global<float>(0x0083ebd8);  // 65536 / 360
    field<uint32_t>(ent, 0x150) = angles[0];
    field<uint32_t>(ent, 0x154) = angles[1];
    field<uint32_t>(ent, 0x158) = angles[2];
    const float y = field<float>(ent, 0x154) * to_short;
    const float x = field<float>(ent, 0x150) * to_short;
    const float to_degrees = title_global<float>(0x0083ebd4);  // 360 / 65536
    const int32_t short_x = static_cast<int32_t>(x) & 0xffff;
    const int32_t short_y = static_cast<int32_t>(y) & 0xffff;
    field<float>(actor, 0x130) = static_cast<float>(short_x) * to_degrees;
    const float yaw = static_cast<float>(short_y) * to_degrees;
    field<float>(actor, 0x134) = yaw;
    field<float>(actor, 0x138) = yaw;
    const float* const to = reinterpret_cast<const float*>(origin);
    field<float>(actor, 0x2a0) = to[0] - field<float>(ent, 0x140);
    field<float>(actor, 0x2a4) = to[1] - field<float>(ent, 0x144);
    field<float>(actor, 0x2a8) = to[2] - field<float>(ent, 0x148);
    const float thousand = title_global<float>(0x0083eb9c);
    for (uint32_t offset = 0x2a0; offset <= 0x2a8; offset += 4) {
        const float per_second = thousand / static_cast<float>(title_global<int32_t>(0x008dda9c));
        field<float>(actor, offset) = platform_mulss(per_second, field<float>(actor, offset));
    }
    field<float>(actor, 0x2cc) = 0.0f;
    field<float>(actor, 0x2c8) = 0.0f;
    field<float>(actor, 0x2c4) = 0.0f;
    field<uint32_t>(ent, 0x140) = origin[0];
    field<uint32_t>(ent, 0x144) = origin[1];
    field<uint32_t>(ent, 0x148) = origin[2];
    field<uint32_t>(actor, 0x2b0) = 0;
}

uint32_t XB_CDECL GScr_ActorGetTurret(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor || !Actor_IsUsingTurret(actor)) return 0;
    return field<uint32_t>(field<uint8_t*>(actor, 0x136c), 0x224);
}

void XB_CDECL GScr_ActorSetAnimMode(uint32_t handle, const ScriptString* mode) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    const uint32_t hash = Com_HashKeyNoCase(0, script_text(mode));
    if (hash == title_global<uint32_t>(0x0097a868)) {  // scr_const_nophysics
        field<uint32_t>(actor, 0x280) = 8;
        field<uint32_t>(field<uint8_t*>(actor, 0), 0x2a4) |= 0x10000;
        return;
    }
    field<uint32_t>(field<uint8_t*>(actor, 0), 0x2a4) &= ~0x10000u;
    if (hash == title_global<uint32_t>(0x0097a6f0))  // gravity
        field<uint32_t>(actor, 0x280) = 4;
    else if (hash == title_global<uint32_t>(0x0097a740))  // nogravity
        field<uint32_t>(actor, 0x280) = 6;
    else if (hash == title_global<uint32_t>(0x0097a658))  // angle_deltas
        field<uint32_t>(actor, 0x280) = 3;
    else if (hash == title_global<uint32_t>(0x0097a86c))  // none
        field<uint32_t>(actor, 0x280) = 0;
}

void XB_CDECL GScr_ActorSetTraverseMode(uint32_t handle, const ScriptString* mode) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    static const uint32_t kModes[] = {0x00984b60, 0x00984bb0, 0x00984ba8};
    for (uint32_t i = 0; i < 3; ++i) {
        if (ScriptString_EqualsString(mode, &title_global<ScriptString>(kModes[i]))) {
            field<uint32_t>(actor, 0x268) = i + 1;
            return;
        }
    }
}

int32_t XB_CDECL GScr_ActorHasPendingSentientSlot(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return 0;
    for (uint32_t i = 0; i < 4; ++i)
        if (field<uint32_t>(actor, 0x12a8 + i * 0x18) && field<uint32_t>(actor, 0x12bc + i * 0x18) == 0) return 1;
    return 0;
}

void XB_CDECL GScr_ActorAllowedStances(uint32_t handle, uint32_t a, uint32_t b, uint32_t c) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    const uint32_t crouch = 0x0097a674, prone = 0x0097a764;  // scr_const_crouch, scr_const_prone
    uint32_t& stances = field<uint32_t>(actor, 0x244);
    stances = 0;
    if (a == title_global<uint32_t>(0x0097a7e8))  // scr_const_stand
        stances = 1;
    else if (a == title_global<uint32_t>(crouch))
        stances = 2;
    else if (a == title_global<uint32_t>(prone))
        stances = 4;
    const uint32_t others[] = {b, c};
    for (uint32_t other : others) {
        if (other == title_global<uint32_t>(0x0097a7e8))
            stances |= 1;
        else if (other == title_global<uint32_t>(crouch))
            stances |= 2;
        else if (other == title_global<uint32_t>(prone))
            stances |= 4;
    }
    if (!stances) stances = 7;
}

int32_t XB_CDECL GScr_ActorPathHasFlag1(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor || field<int16_t>(actor, 0x740) <= 0) return 0;
    return field<int32_t>(actor, 0x77c) & 1;
}

float XB_CDECL GScr_GetPathRemainingLength(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return title_global<float>(0x0083eafc);  // 0
    return Path_GetRemainingLength(actor + 0x3c0);
}

int32_t XB_CDECL GScr_ActorInCriticalSection(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return 0;
    const int32_t top = field<int32_t>(actor, 0xbc);
    for (int32_t i = 0; i <= top; ++i)
        if (field<int32_t>(actor, 0xa4 + i * 4) == 9) return 1;
    return 0;
}

void XB_CDECL GScr_ActorEndCriticalSection(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor || title_global<int32_t>(0x0094cbb4) > 0) return;
    if (field<int32_t>(actor, 0xa4 + field<int32_t>(actor, 0xbc) * 4) == 9) Actor_PopState(actor);
}

void XB_CDECL GScr_ActorSetPitchBlendAnims(uint32_t handle, float a, float b, uint32_t c, uint32_t d, uint32_t e) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    const float one = title_global<float>(0x0083eb20);
    const float inverse_a = one / a;
    const float inverse_b = one / b;
    field<uint32_t>(actor, 0x178) = c;
    field<float>(actor, 0x188) = inverse_a;
    field<float>(actor, 0x18c) = inverse_b;
    field<uint32_t>(actor, 0x17c) = d;
    field<uint32_t>(actor, 0x180) = e;
}

void XB_CDECL GScr_ActorExitProne(uint32_t handle, float time) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    Actor_ExitProne(actor, static_cast<int32_t>(time * title_global<float>(0x0083eb9c)));  // 1000
}

void XB_CDECL GScr_ActorSetAimParams(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                                     uint32_t f) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    field<uint32_t>(actor, 0x160) = a;
    field<uint32_t>(actor, 0x164) = b;
    field<uint32_t>(actor, 0x168) = c;
    field<uint32_t>(actor, 0x16c) = d;
    field<uint32_t>(actor, 0x170) = e;
    field<uint32_t>(actor, 0x174) = f;
}

float XB_CDECL GScr_SentientNearestEnemyDist(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return title_global<float>(0x0083eafc);  // 0
    uint8_t* const sentient = field<uint8_t*>(ent, 0x244);
    if (!sentient) return title_global<float>(0x0083eafc);
    static const uint32_t kEnemyTeam[] = {0, 2, 1, 0, 0};
    const uint32_t enemy = kEnemyTeam[field<uint32_t>(sentient, 4)];
    const uint32_t mask = 1u << enemy;
    if (!Sentient_FirstOfTeams(mask)) return title_global<float>(0x0083ee84);  // 1e8
    float best = title_global<float>(0x0083ee84);
    if (enemy == 0) return title_global<float>(0x0083eafc);
    uint8_t* const self = field<uint8_t*>(sentient, 0);
    const float x = field<float>(self, 0x140), y = field<float>(self, 0x144), z = field<float>(self, 0x148);
    for (uint8_t* other = Sentient_FirstOfTeams(mask); other; other = Sentient_NextOfTeams(other, mask)) {
        uint8_t* const actor = field<uint8_t*>(self, 0x240);
        const int32_t index = static_cast<int32_t>(other - title_global<uint8_t*>(0x00985378)) / 0x60;  // g_sentients
        if (field<int32_t>(actor, 0x868 + index * 0x50) <= 0) continue;
        uint8_t* const other_ent = field<uint8_t*>(other, 0);
        if (field<uint8_t>(other_ent, 0x2a4) & 2) continue;
        if (field<uint32_t>(other, 0x30)) continue;
        const float dx = field<float>(other_ent, 0x140) - x;
        const float dy = field<float>(other_ent, 0x144) - y;
        const float dz = field<float>(other_ent, 0x148) - z;
        const float distance = platform_addss(platform_addss(dz * dz, dy * dy), dx * dx);
        if (best > distance) best = distance;
    }
    return best;
}

void XB_CDECL GScr_SetEnt250Field_f4(uint32_t handle, float value) {
    uint8_t* const ent = handle_entity(handle);
    if (ent && field<uint8_t*>(ent, 0x250)) field<float>(field<uint8_t*>(ent, 0x250), 0xf4) = value;
}

void XB_CDECL GScr_SetEntFlag20000000(uint32_t handle, uint32_t set, uint32_t value) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (set)
        field<uint32_t>(ent, 0x2a4) |= 0x20000000;
    else
        field<uint32_t>(ent, 0x2a4) &= ~0x20000000u;
    if (uint8_t* const record = field<uint8_t*>(ent, 0x250)) field<uint16_t>(record, 0xf8) = static_cast<uint16_t>(value);
}

void XB_CDECL GScr_HudComp1c_Off() {
    uint8_t* const comp = hud_component(0x1c);
    field<int32_t>(comp, 0x44) = -1;
    field<float>(comp, 0x54) = 0.0f;
}

void XB_CDECL GScr_HudComp1c_SetMode2() {
    uint8_t* const comp = hud_component(0x1c);
    field<int32_t>(comp, 0x44) = 2;
    field<float>(comp, 0x54) = title_global<float>(0x0083eb20);
}

void XB_CDECL GScr_HudComp1c_SetMode1() {
    uint8_t* const comp = hud_component(0x1c);
    field<int32_t>(comp, 0x44) = 1;
    field<float>(comp, 0x54) = title_global<float>(0x0083eb20);
}

void XB_CDECL GScr_HudComp1c_SetMode0() {
    uint8_t* const comp = hud_component(0x1c);
    field<int32_t>(comp, 0x44) = 0;
    field<float>(comp, 0x54) = title_global<float>(0x0083eb20);
}

void XB_CDECL GScr_HudComp20_EnableWith(uint32_t value) {
    uint8_t* const comp = hud_component(0x20);
    field<uint8_t>(comp, 0x14) = 1;
    field<uint32_t>(comp, 0x18) = value;
}

void XB_CDECL GScr_HudComp20_Disable() { field<uint8_t>(hud_component(0x20), 0x14) = 0; }
void XB_CDECL GScr_HudComp20_Enable() { field<uint8_t>(hud_component(0x20), 0x14) = 1; }
void XB_CDECL GScr_HudComp20_Set10(uint32_t value) { field<uint32_t>(hud_component(0x20), 0x10) = value; }

void XB_CDECL GScr_HudComp24_Clear() {
    uint8_t* const comp = hud_component(0x24);
    field<uint32_t>(comp, 0xe4) = 0;
    field<uint32_t>(comp, 0xe8) = 0;
}

void XB_CDECL GScr_HudComp24_Push(uint32_t value) {
    uint8_t* const comp = hud_component(0x24);
    if (value == 0 || value == 1) {
        const int32_t count = field<int32_t>(comp, 0xe4) + 1;
        field<int32_t>(comp, 0xe4) = count;
        if (count <= 3) field<uint32_t>(comp, 0xa0 + count * 4) = value;
    } else {
        const int32_t count = field<int32_t>(comp, 0xe8) + 1;
        field<int32_t>(comp, 0xe8) = count;
        if (count <= 0xc) field<uint32_t>(comp, 0xac + count * 4) = value;
    }
}

void XB_CDECL GScr_HudComp24_SetEcTwo() { field<float>(hud_component(0x24), 0xec) = title_global<float>(0x0083eba8); }

void XB_CDECL GScr_HudComp24_SetE0(uint32_t value) {
    uint8_t* const comp = hud_component(0x24);
    field<uint8_t>(comp, 0xe0) = static_cast<uint8_t>(value);
    field<float>(comp, 0xec) = 0.0f;
}

void XB_CDECL GScr_HudSetMeterCount(uint32_t count) { HudCompMeter_SetCount(hud_component(0x24), count); }

uint32_t XB_CDECL GScr_HasVisibleModel(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const model = field<uint8_t*>(ent, 0x22c);
    return model && !(field<uint8_t>(model, 0x9c) & 1);
}

void XB_CDECL GScr_SetEntFlag2000000(uint32_t handle) { set_ent_flag<0x2000000>(handle); }

namespace {

// The turret record (+0x24c) of the handle's entity, or null.
uint8_t* handle_turret(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<uint8_t*>(ent, 0x24c) : nullptr;
}

}  // namespace

void XB_CDECL GScr_SetForcedLod(uint32_t handle, int32_t lod) {
    uint8_t* const ent = handle_entity(handle);
    if (ent && field<DObj*>(ent, 0x22c)) DObj_SetForcedLod(lod, field<DObj*>(ent, 0x22c));
}

void XB_CDECL GScr_Turret_Set40(uint32_t handle, float value) {
    if (uint8_t* const turret = handle_turret(handle)) field<float>(turret, 0x40) = value;
}

void XB_CDECL GScr_Turret_Set3c(uint32_t handle, float value) {
    if (uint8_t* const turret = handle_turret(handle)) field<float>(turret, 0x3c) = value;
}

void XB_CDECL GScr_Turret_SetArc_34(uint32_t handle, float value) {
    uint8_t* const turret = handle_turret(handle);
    if (!turret) return;
    field<float>(turret, 0x34) = value;
    if (0.0f > value) field<float>(turret, 0x34) = 0.0f;
}

void XB_CDECL GScr_Turret_SetArc_2c(uint32_t handle, float value) {
    if (uint8_t* const turret = handle_turret(handle)) field<float>(turret, 0x2c) = 0.0f - value;
}

void XB_CDECL GScr_Turret_SetArc_38(uint32_t handle, float value) {
    uint8_t* const turret = handle_turret(handle);
    if (!turret) return;
    field<float>(turret, 0x38) = value;
    if (0.0f > value) field<float>(turret, 0x38) = 0.0f;
}

void XB_CDECL GScr_Turret_SetArc_30(uint32_t handle, float value) {
    uint8_t* const turret = handle_turret(handle);
    if (!turret) return;
    const float negated = 0.0f - value;
    field<float>(turret, 0x30) = negated;
    if (negated > 0.0f) field<float>(turret, 0x30) = 0.0f;
}

void XB_CDECL GScr_SetSpawnerTeam(uint32_t handle, const ScriptString* team) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || field<uint8_t>(ent, 0) != 0xc) return;
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    const uint32_t names[] = {0x0083208c, 0x00832084, 0x00831f14};  // "axis", "allies", "neutral"
    for (uint32_t i = 0; i < 3; ++i) {
        if (title_function<Stricmp>(0x001f1443)(script_text(team), title_text(names[i])) == 0) {
            field<uint32_t>(ent, 0x3b4) = i + 1;
            return;
        }
    }
    // The message gets the string's rep pointer, not its text (+6).
    title_function<const char* XB_CDECL(const char*, uint32_t)>(0x002a93d0)(  // va
        &title_global<char>(0x00831edc), *reinterpret_cast<const uint32_t*>(team));  // "unknown team '%s', ..."
}

void XB_CDECL GScr_SetEntFlag80000(uint32_t handle) { set_ent_flag<0x80000>(handle); }
void XB_CDECL GScr_ClearEntFlag80000(uint32_t handle) { clear_ent_flag<0x80000>(handle); }
void XB_CDECL GScr_ClearEntFlag40000(uint32_t handle) { clear_ent_flag<0x40000>(handle); }
void XB_CDECL GScr_SetEntFlag40000(uint32_t handle) { set_ent_flag<0x40000>(handle); }

int32_t XB_CDECL GScr_GetEntityNumber(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<int32_t>(ent, 0x224) : 0;
}

void XB_CDECL GScr_MoveGravity(uint32_t handle, const uint32_t* velocity) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    field<uint32_t>(ent, 0x14) = 5;  // gravity trajectory
    field<uint32_t>(ent, 0x18) = title_global<uint32_t>(0x0098540c);  // level_time
    field<uint32_t>(ent, 0x2c) = velocity[0];
    field<uint32_t>(ent, 0x30) = velocity[1];
    field<uint32_t>(ent, 0x34) = velocity[2];
    field<uint8_t>(ent, 0x294) = 1;
}

void XB_CDECL GScr_ClearEFlag8000(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) field<uint32_t>(ent, 0x10) &= ~0x8000u;
}

void XB_CDECL GScr_SetEFlag8000(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) field<uint32_t>(ent, 0x10) |= 0x8000;
}

void XB_CDECL GScr_ViewKick(uint32_t handle, int32_t damage, const float* point) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const int32_t scaled =
        static_cast<int32_t>(field<uint32_t>(ent, 0x354) * static_cast<uint32_t>(damage) + 50u);
    field<int32_t>(field<uint8_t*>(ent, 0x23c), 0x5d4) = scaled / 100;
    if (field<int32_t>(field<uint8_t*>(ent, 0x23c), 0x5d4) < 0)
        title_function<const char* XB_CDECL(const char*, double)>(0x002a93d0)(  // va
            &title_global<char>(0x00831f1c), static_cast<double>(damage));  // "viewkick: damage %g < 0\n"
    for (uint32_t i = 0; i < 3; ++i) {
        uint8_t* const client = field<uint8_t*>(ent, 0x23c);
        field<float>(client, 0x5d8 + i * 4) = field<float>(client, i * 4) - point[i];
    }
}

void XB_CDECL GScr_ClearClientVec4f4(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x4f4) = 0;
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x4f8) = 0;
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x4fc) = 0;
}

uint8_t XB_CDECL GScr_Turret_GetFlag1(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const turret = field<uint8_t*>(ent, 0x24c);
    if (!turret) {
        title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
            &title_global<char>(0x00832094), script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)));
        return 0;  // "entity type '%s' is not a turret"
    }
    return field<uint8_t>(turret, 1) != 0;
}

void XB_CDECL GScr_GetStance(uint32_t handle, ScriptString* out) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (!client) return;
    const uint32_t flags = field<uint32_t>(client, 0x2c);
    uint32_t stance = 0x00984c2c;                 // "stand"
    if (flags & 1)
        stance = 0x00984bd4;                      // "prone"
    else if (flags & 2)
        stance = 0x00984ae4;                      // "crouch"
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d600)(out, stance);  // ScriptString_Assign
}

uint8_t XB_CDECL GScr_AnimHasNotetrack(uint32_t, uint32_t anim, const uint32_t* name) {
    return XAnim_HasNotetrack(anim, name);
}

void XB_CDECL GScr_SetHintString28c(uint32_t handle, uint32_t hint) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint32_t classname = field<uint32_t>(ent, 0x260);
    field<uint32_t>(ent, 0x28c) = hint;
    if (classname == title_global<uint32_t>(0x0097a81c))  // scr_const_trigger_use
        field<uint8_t>(ent, 5) = 0xff;
    else
        field<uint16_t>(field<uint8_t*>(ent, 0x240), 0x1380) = 0xffff;
}

}  // namespace game
