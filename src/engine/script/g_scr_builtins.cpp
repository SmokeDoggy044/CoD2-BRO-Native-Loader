#include "g_scr_builtins.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "cg_ents.h"
#include "cg_hud.h"
#include "cg_view.h"
#include "cgame.h"
#include "cl.h"
#include "dobj.h"
#include "fx.h"
#include "g_client.h"
#include "g_combat.h"
#include "g_main.h"
#include "g_mover.h"
#include "g_pathnode.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "movie.h"
#include "ngl_debug.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"
#include "snd.h"
#include "stream.h"
#include "sv.h"
#include "title_crt.h"
#include "title_data.h"
#include "tr_main.h"
#include "xanim.h"
#include "scr_field.h"

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
        own_function<Va>(&va_entry)(&title_global<char>(too_small), x87_widen(time));  // va
    } else if (time > title_global<float>(0x0083eb3c)) {  // 60
        own_function<Va>(&va_entry)(&title_global<char>(too_large), x87_widen(time));  // va
    }
}

// time * 1000 + 0.5, truncated (CVTTSS2SI).
int32_t hud_time_ms(float time) {
    return static_cast<int32_t>(time * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24));
}

// va with only a format (the shipped build's script error messages: formatted, then dropped).
void va_message(uint32_t format) {
    own_function<const char* XB_CDECL(const char*)>(&va_entry)(&title_global<char>(format));  // va
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
        if (own_function<int32_t XB_CDECL(const char*, const char*)>(&title_stricmp)(  // _stricmp
                text, title_text(title_global<uint32_t>(0x008d23a8 + i * 4))) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == 0)
        own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
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
        NameMap_Insert(own_function<uint32_t XB_CDECL(const char*)>(&ScrIface_HashStringNoCase)(text), text);  // ScrIface_HashStringNoCase
        title_global<uint32_t>(row.slot) = hash;
    }
    uint32_t temp;
    for (const NameSlot& row : kConstStrings) {
        own_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(&ScriptString_FromCString)(&temp, title_text(row.text));  // ScriptString_FromCString
        own_function<void XB_THISCALL(uint32_t, uint32_t*)>(&ScriptString_Assign_entry)(row.slot, &temp);  // ScriptString_Assign
        own_function<void XB_THISCALL(uint32_t*)>(&ScriptString_Release_entry)(&temp);                     // ScriptString_Release
    }
    for (const NameSlot& row : kConstCStrings)
        own_function<void XB_THISCALL(uint32_t, const char*)>(&ScriptString_AssignCString)(row.slot, title_text(row.text));  // ScriptString_AssignCString
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
    return own_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_AssignCString)(out, text);  // ScriptString_AssignCString
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
    title_strncpy(buf, text, count);  // _strncpy
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
        own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(  // va
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
    own_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_AssignCString)(out, text);  // ScriptString_AssignCString
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
            own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
                &title_global<char>(0x00831c9c), script_text(weapon_name));  // "Unknown weapon %s."
        const uint32_t kind = field<uint32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x9c);
        const bool mixed_primary = (kind == 1 || kind == 2) && (index == 1 || index == 2);
        if (kind != index && !mixed_primary)
            own_function<const char* XB_CDECL(const char*, const char*, const char*, const char*)>(&va_entry)(  // va
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
        own_function<void XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_Assign_entry)(out, 0x00984cb0);  // ScriptString_Assign
        return;
    }
    own_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_AssignCString)(  // ScriptString_AssignCString
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
    own_function<uint32_t XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_AssignCString)(out, text);  // ScriptString_AssignCString
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
        if (own_function<Stricmp>(&title_stricmp)(script_text(team), title_text(names[i])) == 0) {
            field<uint32_t>(ent, 0x3b4) = i + 1;
            return;
        }
    }
    // The message gets the string's rep pointer, not its text (+6).
    own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(  // va
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
        own_function<const char* XB_CDECL(const char*, double)>(&va_entry)(  // va
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
        own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
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
    own_function<void XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_Assign_entry)(out, stance);  // ScriptString_Assign
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

namespace {

// The entity's turret record (+0x24c); without one, formats "entity type '%s' is not a turret" with the
// classname (+0x25c) and gives null.
uint8_t* turret_or_message(uint8_t* ent) {
    uint8_t* const turret = field<uint8_t*>(ent, 0x24c);
    if (!turret)
        own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
            &title_global<char>(0x00832094), script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)));
    return turret;
}

}  // namespace

void XB_CDECL GScr_ResetHintCache(uint32_t handle, const uint32_t* hint) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint32_t classname = field<uint32_t>(ent, 0x260);
    const uint32_t trigger_use = title_global<uint32_t>(0x0097a81c);  // scr_const_trigger_use
    if (classname != trigger_use && !field<uint8_t*>(ent, 0x240)) return;
    if (*hint == 0) return;
    if (classname == trigger_use)
        field<uint8_t>(ent, 5) = 0xff;
    else
        field<uint16_t>(field<uint8_t*>(ent, 0x240), 0x1380) = 0xffff;
}

void XB_CDECL GScr_SetCursorHint(uint32_t handle, const ScriptString* hint) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (field<uint32_t>(ent, 0x260) == title_global<uint32_t>(0x0097a81c) &&  // scr_const_trigger_use
        Q_stricmpn2(99999, script_text(hint), title_text(0x00832040)) == 0) {  // "HINT_INHERIT"
        field<int32_t>(ent, 0xc0) = -1;
        return;
    }
    for (int32_t i = 1; i < 0x10c; ++i) {
        const char* const name = title_text(title_global<uint32_t>(0x008dae44 + i * 4));  // hint type names
        if (!name) break;
        if (Q_stricmpn2(99999, script_text(hint), name) == 0) {
            field<int32_t>(ent, 0xc0) = i;
            return;
        }
    }
    // (A read-only walk over the table at 0x008dae48 follows: its printing was compiled out.) The message gets
    // the string's rep pointer, not its text.
    own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(  // va
        &title_global<char>(0x00831ff8), *reinterpret_cast<const uint32_t*>(hint));  // "%s is not a valid hint type..."
}

int32_t XB_CDECL GScr_Turret_GetOwner(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const turret = turret_or_message(ent);
    if (!turret) return 0;
    uint8_t* const owner = field<uint8_t*>(turret, 0xc);
    if (!owner || !(field<uint8_t>(turret, 2) & 0x40)) return 0;
    return field<int32_t>(owner, 0x224);
}

long double XB_CDECL GScr_Turret_GetMaxRange(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (ent) {
        if (uint8_t* const turret = turret_or_message(ent)) return platform_x87_sqrt_ext(field<float>(turret, 0x48));
    }
    return title_global<float>(0x0083eafc);  // 0
}

void XB_CDECL GScr_Turret_SetMaxRange(uint32_t handle, float range) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<float>(turret, 0x48) = range * range;
}

void XB_CDECL GScr_Turret_Set54(uint32_t handle, float value) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<float>(turret, 0x54) = value;
}

void XB_CDECL GScr_Turret_MakeUnusable(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<uint8_t>(turret, 3) &= 0xef;
}

void XB_CDECL GScr_Turret_MakeUsable(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<uint8_t>(turret, 3) |= 0x10;
}

void XB_CDECL GScr_Turret_SetTeam(uint32_t handle, const ScriptString* team) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const turret = turret_or_message(ent);
    if (!turret) return;
    using Stricmp = int32_t XB_CDECL(const char*, const char*);
    if (own_function<Stricmp>(&title_stricmp)(script_text(team), title_text(0x0083208c)) == 0) {  // "axis"
        field<uint32_t>(turret, 0x64) = 1;
    } else if (own_function<Stricmp>(&title_stricmp)(script_text(team), title_text(0x00832084)) == 0) {  // "allies"
        field<uint32_t>(turret, 0x64) = 2;
    } else {
        // The string's rep pointer, not its text.
        own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(  // va
            &title_global<char>(0x00832050), *reinterpret_cast<const uint32_t*>(team));  // "unknown team '%s', ..."
    }
}

void XB_CDECL GScr_Turret_ClearTargetEntity(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<uint32_t>(turret, 8) = 0;
}

void XB_CDECL GScr_Turret_SetTargetEntity(uint32_t handle, uint32_t target_handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const target = handle_entity(target_handle);
    if (!target) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<uint8_t*>(turret, 8) = target;
}

int32_t XB_CDECL GScr_GetOwnerHandle(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<int32_t>(ent, 0x1a0) : 0;
}

int32_t XB_CDECL GScr_Turret_GetUser(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !turret_or_message(ent) || !field<uint8_t>(ent, 0x297)) return 0;
    return field<int32_t>(ent, 0x1a0);
}

void XB_CDECL GScr_Turret_SetMode(uint32_t handle, uint32_t mode) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const turret = turret_or_message(ent);
    if (!turret) return;
    uint16_t& flags = field<uint16_t>(turret, 2);
    if (mode == title_global<uint32_t>(0x0097a850))  // scr_const_auto_ai
        flags |= 3;
    else if (mode == title_global<uint32_t>(0x0097a858))  // scr_const_manual
        flags &= 0xfffc;
    else if (mode == title_global<uint32_t>(0x0097a85c))  // scr_const_manual_ai
        flags = static_cast<uint16_t>((flags & 0xfffd) | 1);
    else if (mode == title_global<uint32_t>(0x0097a854))  // scr_const_auto_nonai
        flags = static_cast<uint16_t>((flags & 0xfffe) | 2);
}

void XB_CDECL GScr_Turret_ClearFlag4(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<uint8_t>(turret, 2) &= 0xfb;
}

void XB_CDECL GScr_Turret_SetFlag4(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const turret = turret_or_message(ent)) field<uint8_t>(turret, 2) |= 4;
}

void XB_CDECL GScr_SetEntFlag400(uint32_t handle) { set_ent_flag<0x400>(handle); }
void XB_CDECL GScr_ClearEntFlag400(uint32_t handle) { clear_ent_flag<0x400>(handle); }

void XB_CDECL GScr_SetEntInList960070(uint32_t handle, uint32_t in_list) {
    if (uint8_t* const ent = handle_entity(handle)) G_SetEntityInList960070(0, in_list != 0, ent);
}

void XB_CDECL GScr_SetTakeDamage(uint32_t handle, uint32_t take_damage) {
    if (uint8_t* const ent = handle_entity(handle)) field<uint32_t>(ent, 0x29c) = static_cast<uint8_t>(take_damage);
}

namespace {

// The entity's targetname text (+0x264) for messages, "<not set>" without one.
const char* targetname_or_unset(uint8_t* ent) {
    const uint32_t rep = field<uint32_t>(ent, 0x264);
    if (!rep || rep + 6 == 0) return title_text(0x00832314);  // "<not set>"
    return script_text(reinterpret_cast<const ScriptString*>(ent + 0x264));
}

}  // namespace

void XB_CDECL GScr_SetNormalHealth(uint32_t handle, float fraction) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const float one = title_global<float>(0x0083eb20);
    if (fraction > one) {
        fraction = one;
    } else if (fraction <= 0.0f) {  // comiss 0, fraction; jb: NaN skips the message
        own_function<const char* XB_CDECL(const char*, double, int32_t, const char*)>(&va_entry)(  // va
            &title_global<char>(0x00832168), x87_widen(fraction), field<int32_t>(ent, 0x224),
            targetname_or_unset(ent));  // "setNormalHealth must be greater than 0 (tried to set %g on ent %i, ..."
    }
    float max_health;
    if (uint8_t* const client = field<uint8_t*>(ent, 0x23c)) {
        max_health = static_cast<float>(field<int32_t>(client, 0x580));
    } else {
        const int32_t max = field<int32_t>(ent, 0x354);
        if (max <= 0) {
            own_function<const char* XB_CDECL(const char*, int32_t, const char*)>(&va_entry)(  // va
                &title_global<char>(0x00832110), field<int32_t>(ent, 0x224),
                targetname_or_unset(ent));  // "entity's max health must be greater than 0 to call setNormalHealth ..."
            return;
        }
        max_health = static_cast<float>(max);
    }
    int32_t health = static_cast<int32_t>(max_health * fraction + title_global<float>(0x0083eb24));  // + 0.5
    if (health < 1) health = 1;
    field<int32_t>(ent, 0x350) = health;
}

long double XB_CDECL GScr_GetNormalHealth(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return title_global<float>(0x0083eafc);  // 0
    const int32_t health = field<int32_t>(ent, 0x350);
    if (uint8_t* const client = field<uint8_t*>(ent, 0x23c)) {
        if (health) return static_cast<long double>(health) / static_cast<long double>(field<int32_t>(client, 0x580));
    }
    const int32_t max = field<int32_t>(ent, 0x354);
    if (!max) return title_global<float>(0x0083eafc);
    return static_cast<long double>(field<int32_t>(ent, 0x350)) / static_cast<long double>(max);
}

uint8_t XB_CDECL GScr_DoorIsLocked(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    const uint32_t classname = field<uint32_t>(ent, 0x260);
    if (classname != title_global<uint32_t>(0x0097a6e0) &&  // scr_const_func_door_rotating
        classname != title_global<uint32_t>(0x0097a6dc))    // scr_const_func_door
        return 0;
    return field<uint32_t>(ent, 0x3b4) != 0;
}

void XB_CDECL GScr_GetOrigin(uint32_t handle, uint32_t* out) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint32_t origin[3];
    if (field<uint32_t>(ent, 0xdc)) {
        // A brush model: the centre of its absolute bounds (+0x100 mins, +0x110 maxs).
        const float half = title_global<float>(0x0083eb24);
        for (uint32_t i = 0; i < 3; ++i) {
            const float centre = platform_addss(field<float>(ent, 0x110 + i * 4), field<float>(ent, 0x100 + i * 4)) * half;
            std::memcpy(&origin[i], &centre, 4);
        }
    } else {
        for (uint32_t i = 0; i < 3; ++i) origin[i] = field<uint32_t>(ent, 0x140 + i * 4);
    }
    out[0] = origin[0];
    out[1] = origin[1];
    out[2] = origin[2];
}

void XB_CDECL GScr_EnableLinkTo(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    bool supported = false;
    if (field<uint8_t>(ent, 0) == 0 && field<uint8_t>(ent, 0x294) == 0) {
        supported = (!field<uint32_t>(ent, 0x328) && !field<uint32_t>(ent, 0x32c)) ||
                    own_function<int32_t XB_CDECL(const char*, const char*)>(&title_stricmp)(  // _stricmp
                        script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)),
                        title_text(0x00832270)) == 0;  // "trigger_multiple"
    }
    if (!supported) {
        // The message is formatted into a local buffer and dropped; the link is enabled anyway.
        char message[0x100];
        const uint8_t type = field<uint8_t>(ent, 0);
        const char* const type_name = type < 0x11 ? title_text(title_global<uint32_t>(0x008d5090 + type * 4))
                                                  : title_text(0x00832838);  // "WARNING !! Entity Type Unknown ..."
        title_sprintf(  // _sprintf
            message, title_text(0x00832248), type_name);  // "entity %s:does not support enableLinkTo"
    }
    const uint32_t flags = field<uint32_t>(ent, 0x2a4);
    field<uint32_t>(ent, 0x328) = title_global<uint32_t>(0x0098540c);  // level_time
    field<uint32_t>(ent, 0x32c) = 0x1c;
    field<uint32_t>(ent, 0x2a4) = flags | 0x8000;
}

void XB_CDECL GScr_Unlink(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) G_EntUnlink(ent);
}

uint8_t XB_CDECL GScr_TestEntBit299(uint32_t handle, uint32_t bit) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    return (field<uint8_t>(ent, 0x299) & static_cast<uint8_t>(1u << (bit & 31))) != 0;
}

void XB_CDECL GScr_GetAttachTagName(uint32_t handle, ScriptString* out, int32_t index) {
    if (uint8_t* const ent = handle_entity(handle))
        own_function<void XB_THISCALL(ScriptString*, uint8_t*)>(&ScriptString_Assign_entry)(out, ent + 0x3dc + index * 8);  // ScriptString_Assign
}

void XB_CDECL GScr_GetAttachModelName(uint32_t handle, ScriptString* out, int32_t index) {
    if (uint8_t* const ent = handle_entity(handle))
        own_function<void XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_AssignCString)(  // ScriptString_AssignCString
            out, field<uint32_t>(field<uint8_t*>(ent, 0x3d8 + index * 8), 0x48));  // the model's name
}

int32_t XB_CDECL GScr_GetAttachSize(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    int32_t count = 0;
    while (count < 7 && field<uint32_t>(ent, 0x3d8 + count * 8)) ++count;
    return count;
}

int32_t XB_CDECL GScr_IsSentient(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent && field<uint32_t>(ent, 0x244) ? 1 : 0;
}

int32_t XB_CDECL GScr_IsAI(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent && field<uint32_t>(ent, 0x240) ? 1 : 0;
}

int32_t XB_CDECL GScr_IsPlayer(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent && field<uint32_t>(ent, 0x23c) ? 1 : 0;
}

int32_t XB_CDECL GScr_IsAlive(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    if (uint8_t* const actor = field<uint8_t*>(ent, 0x240)) return field<uint32_t>(actor, 0x2d0) ? 1 : 0;
    return field<int32_t>(ent, 0x350) > 0 ? 1 : 0;
}

void XB_CDECL GScr_SetSoundParam(uint32_t id, uint32_t value) {
    FX_SetEffectParamById(title_global<FxIdList*>(0x0095f5b4), id, value);
}

void XB_CDECL GScr_StopSound(uint32_t id) { FX_StopEffectById(id, 0, title_global<FxIdList*>(0x0095f5b4)); }

int32_t XB_CDECL GScr_GetVehicleNode(const ScriptString* value, const ScriptString* key, int32_t* out) {
    const uint32_t all = (!*reinterpret_cast<const uint32_t*>(value) || !*reinterpret_cast<const uint32_t*>(key)) ? 1 : 0;
    return VehicleNode_FindByKey(all, 0, key, value, out);
}

int32_t XB_CDECL GScr_GetNode(const ScriptString* value, const ScriptString* key, uint32_t* out) {
    using FromCString = ScriptString* XB_THISCALL(ScriptString*, const char*);
    using Release = void XB_THISCALL(ScriptString*);
    ScriptString key_copy, value_copy;  // the original builds the value copy in its first argument slot
    own_function<FromCString>(&ScriptString_FromCString)(&key_copy, script_text(key));
    own_function<FromCString>(&ScriptString_FromCString)(&value_copy, script_text(value));
    const int32_t result = PathNode_FindByKey(&key_copy, title_global<uint8_t*>(0x0095f4f4), &value_copy, out);
    own_function<Release>(&ScriptString_Release_entry)(&value_copy);  // ScriptString_Release
    own_function<Release>(&ScriptString_Release_entry)(&key_copy);
    return result;
}

int32_t XB_CDECL GScr_GetEntByNum(uint32_t number) {
    if (number >= 0x540) return 0;
    uint8_t* const ent = handle_entity(number);
    return ent ? field<int32_t>(ent, 0x224) : 0;
}

void XB_CDECL GScr_EmptyStub38() {}

namespace {

// The weapon (1..last, as a byte) whose name id (+4) is `id`; 0 when none matches (also weapon 0).
uint32_t weapon_by_id(uint32_t id) {
    const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    for (int32_t i = 0; i <= last; ++i)
        if (field<uint32_t>(weapon_def(i), 4) == id) return static_cast<uint32_t>(i) & 0xff;
    return 0;
}

// "unknown weapon" messages skip the unset id (0xfefefefe) and "none".
void unknown_weapon_message(uint32_t id, uint32_t format) {
    if (id != 0xfefefefe && id != title_global<uint32_t>(0x0097a86c))  // scr_const_none
        own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(&title_global<char>(format), id);  // va
}

ScriptString* script_from_text(ScriptString* out, const char* text) {
    own_function<ScriptString* XB_THISCALL(ScriptString*, const char*)>(&ScriptString_FromCString)(out, text);  // ScriptString_FromCString
    return out;
}

// GScr_GetWeaponClassname / GetWeaponClassname2: the weapon's Radiant name (+0x578), following the alternate
// weapon chain (+0x718) while it is empty; messages and "" otherwise.
ScriptString* weapon_classname(ScriptString* out, uint32_t id) {
    const uint32_t weapon = weapon_by_id(id);
    if (!weapon) {
        unknown_weapon_message(id, 0x00832524);  // "unknown weapon '%s' in getWeaponClassname\n"
        return script_from_text(out, title_text(0x0082dfeb));  // ""
    }
    uint32_t current = weapon;
    for (;;) {
        const char* const name = field<const char*>(weapon_def(static_cast<int32_t>(current)), 0x578);
        if (*name) return script_from_text(out, field<const char*>(weapon_def(static_cast<int32_t>(current)), 0x578));
        current = field<uint32_t>(weapon_def(static_cast<int32_t>(current)), 0x718);
        if (!current || current == weapon) break;
    }
    own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(  // va
        &title_global<char>(0x008324d8), id);  // "^3WARNING^7: no Radiant name found for weapon '%d' ..."
    return script_from_text(out, title_text(0x0082dfeb));
}

// The level change request: 0x00985eb0 = 1, the map name into 0x00985ecc (255 bytes, terminated), 0x00985eb8 = 1.
void request_level_change(const ScriptString* name) {
    title_global<uint32_t>(0x00985eb0) = 1;
    title_strncpy(  // _strncpy
        &title_global<char>(0x00985ecc), script_text(name), 0xff);
    title_global<uint8_t>(0x00985fcb) = 0;
    title_global<uint32_t>(0x00985eb8) = 1;
}

}  // namespace

uint32_t XB_CDECL GScr_GetEntScriptObject(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    if (!field<uint32_t>(ent, 0x428)) {
        const uint32_t create = field<uint32_t>(title_global<uint8_t*>(0x0095f49c), 0x804);
        if (create) field<uint32_t>(ent, 0x428) = title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(create)(0, 0);
    }
    return field<uint32_t>(ent, 0x428);
}

int32_t XB_CDECL GScr_GetPlayerHandle() {
    return field<int32_t>(field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4), 0x224);
}

int32_t XB_CDECL GScr_GetLevelEnt8Handle() {
    return field<int32_t>(field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224);
}

void XB_REGS3 XB_CDECL GScr_ReturnObstacleKind(uint32_t, uint32_t, ScriptString* out, uint32_t handle) {
    using Assign = void XB_THISCALL(ScriptString*, uint32_t);
    if (!handle) {
        own_function<Assign>(&ScriptString_Assign_entry)(out, 0x00984cb0);  // ScriptString_Assign(scr_string_none)
        return;
    }
    // A stale handle reads entity 0 (null) as the original does.
    uint8_t* const ent = handle_entity(handle);
    own_function<Assign>(&ScriptString_Assign_entry)(
        out, (field<uint32_t>(ent, 0x2a4) & 0x4000) ? 0x00984cb4 : 0x00984cb8);  // scr_string_obstacle / _world
}

void XB_CDECL GScr_HudClear(const int32_t* index) { HudElem_SetDefaults(hud_elem(*index)); }

uint8_t* XB_CDECL GScr_GetEntZone(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    int32_t id;
    if (!ent) {
        id = field<int32_t>(title_global<uint8_t*>(0x0095f638), 0x34);
    } else {
        id = field<int32_t>(ent, 0x220);
        if (id == -1) id = field<int32_t>(title_global<uint8_t*>(0x0095f638), 0x34);
    }
    return MemContext_FindById(title_global<uint8_t*>(0x0095f638), id);
}

int32_t* XB_CDECL GScr_NewHudElem(int32_t* out) {
    int32_t value = HudElem_Alloc();
    if (value == -1) value = title_global<int32_t>(0x008dda38);
    *out = value;
    return out;
}

void XB_CDECL GScr_WeaponType(ScriptString* out, const ScriptString* weapon_name) {
    using AssignCString = void XB_THISCALL(ScriptString*, const char*);
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!weapon) {
        own_function<AssignCString>(&ScriptString_AssignCString)(out, title_text(0x00832430));  // "none"
        return;
    }
    const uint32_t type = field<uint32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x94);
    own_function<AssignCString>(&ScriptString_AssignCString)(out, title_text(title_global<uint32_t>(0x008d237c + type * 4)));
}

int32_t XB_CDECL GScr_WeaponGet_6d8(const ScriptString* weapon_name) {
    const uint32_t weapon = weapon_from_name(weapon_name);
    return weapon ? field<int32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x6d8) : 0;
}

int32_t XB_CDECL GScr_WeaponGet_6d4(const ScriptString* weapon_name) {
    const uint32_t weapon = weapon_from_name(weapon_name);
    return weapon ? field<int32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x6d4) : 0;
}

int32_t XB_CDECL GScr_WeaponGetTableValue_5a0(const ScriptString* weapon_name) {
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!weapon) return 0;
    return title_global<int32_t>(0x00935338 + field<uint32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x5a0) * 4);
}

long double XB_CDECL GScr_WeaponGetFloat_5dc(const ScriptString* weapon_name) {
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!weapon) return title_global<float>(0x0083eafc);  // 0
    // FILD, FMUL DWORD 0.001 (msec to seconds), left in ST0.
    return static_cast<long double>(field<int32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x5dc)) *
           static_cast<long double>(title_global<float>(0x0083eb4c));
}

void XB_CDECL GScr_ChangeLevel(const ScriptString* name) {
    if (title_global<uint32_t>(0x0095bf9c)) return;
    request_level_change(name);
}

void XB_CDECL GScr_ChangeLevelWithDelay(const ScriptString* name, float delay) {
    if (title_global<uint32_t>(0x0095bf9c)) return;
    const float thousand = title_global<float>(0x0083eb9c), half = title_global<float>(0x0083eb24);
    title_global<int32_t>(0x00985ec8) = static_cast<int32_t>(delay * thousand + half);
    const float forced = title_global<float>(0x0095c028);
    if (forced >= title_global<float>(0x0083eafc))  // comiss; jb: NaN keeps the script's delay
        title_global<int32_t>(0x00985ec8) = static_cast<int32_t>(forced * thousand + half);
    request_level_change(name);
}

void XB_CDECL GScr_MissionSuccess(const ScriptString* name) {
    const uint8_t* const player = G_FindEntityByIntField(&title_global<uint8_t>(0x0097a8c0), 0x260,
                                                         title_global<int32_t>(0x0097a75c));  // scr_const_player
    if (field<int32_t>(const_cast<uint8_t*>(player), 0x350) <= 0 || title_global<uint32_t>(0x0095bf9c)) return;
    const float forced = title_global<float>(0x0095c028);
    title_global<int32_t>(0x00985ec8) = 0;
    title_global<int32_t>(0x00985ec4) = 0;
    if (forced >= title_global<float>(0x0083eafc))
        title_global<int32_t>(0x00985ec8) =
            static_cast<int32_t>(forced * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24));
    const char* const text = script_text(name);
    if (text && Q_stricmpn(text, title_text(0x008324c4), 99999) == 0)  // "gamefinished"
        title_global<uint32_t>(0x00985eb4) = 1;
    title_global<uint32_t>(0x00985eb8) = 1;
    title_global<uint32_t>(0x00985ebc) = 0;
    title_global<uint32_t>(0x00985eb0) = 1;
    title_strncpy(  // _strncpy
        &title_global<char>(0x00985ecc), script_text(name), 0xff);
    title_global<uint8_t>(0x00985fcb) = 0;
}

ScriptString* XB_CDECL GScr_GetWeaponClassname2(ScriptString* out, uint32_t id) { return weapon_classname(out, id); }
ScriptString* XB_CDECL GScr_GetWeaponClassname(ScriptString* out, uint32_t id) { return weapon_classname(out, id); }

ScriptString* XB_CDECL GScr_GetWeaponDisplayName(ScriptString* out, uint32_t id) {
    const uint32_t weapon = weapon_by_id(id);
    if (!weapon) return script_from_text(out, title_text(0x0082dfeb));  // ""
    return script_from_text(out, field<const char*>(weapon_def(static_cast<int32_t>(weapon)), 0x57c));
}

uint32_t XB_CDECL GScr_GetWeaponModel(uint32_t id) {
    const uint32_t weapon = weapon_by_id(id);
    if (!weapon) {
        unknown_weapon_message(id, 0x00832550);  // "unknown weapon '%s' in getWeaponModel\n"
        return 0;
    }
    return Com_HashKeyNoCase(0, field<const char*>(weapon_def(static_cast<int32_t>(weapon)), 0x57c));
}

uint8_t XB_CDECL GScr_EntHasNotifyListener(uint32_t handle, uint32_t name, uint32_t arg) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x230)) return 0;
    // 0x0029acb0 (EDX list, callee pops 8): the listener whose key (+8) is *name; calls its object's slot 1.
    using CallListener = uint8_t XB_REGS2 XB_STDCALL(uint32_t, uint8_t*, const uint32_t*, uint32_t);
    return own_function<CallListener>(&EntNotifyCallbacks_CallByKey)(0, field<uint8_t*>(ent, 0x230), &name, arg);
}

namespace {

// The objective's config string (index + 16; 0x00964ce0, text +6) copied into a 256-byte buffer.
void objective_info(int32_t configstring, char* info) {
    title_strncpy(  // _strncpy
        info, script_text(&title_global<ScriptString>(0x00964ce0 + configstring * 4)), 0xff);
    info[0xff] = 0;
}

// Add_Ammo for the client's missing ammo of the weapon's ammo type (+0x598): room = limit - client +0x110[type].
void give_missing_ammo(uint8_t* ent, uint8_t* client, uint32_t weapon, int32_t limit, uint32_t fill_clip) {
    const uint32_t ammo_type = field<uint32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x598);
    const int32_t room = limit - field<int32_t>(client, 0x110 + ammo_type * 4);
    if (room > 0) Add_Ammo(ent, weapon, room, fill_clip);
}

}  // namespace

void XB_CDECL GScr_DebugLine(const float* start, const float* end, const uint32_t* color, uint32_t alpha,
                             uint32_t width) {
    const uint32_t rgba[4] = {color[0], color[1], color[2], alpha};
    DebugDraw_AddLine(end, start, reinterpret_cast<const float*>(rgba), width, 0, 1, 0);
}

void XB_CDECL GScr_Print3D(const float* origin, const ScriptString* text, const uint32_t* color, uint32_t alpha,
                           float scale) {
    const uint32_t rgba[4] = {color[0], color[1], color[2], alpha};
    DebugDraw_AddString(reinterpret_cast<const float*>(rgba), origin, scale, script_text(text), 1);
}

void XB_CDECL GScr_ObjectiveToggleRing(int32_t index) {
    if (index < 0 || index >= 0x10) {
        own_function<const char* XB_CDECL(const char*, int32_t, int32_t)>(&va_entry)(  // va
            &title_global<char>(0x00831b38), index, 0xf);  // "index %i is an illegal objective index. ..."
        return;
    }
    // Toggles the objective's "ring" key: "1" when it was absent or 0, else "0".
    const int32_t configstring = index + 0x10;
    char info[0x100];
    objective_info(configstring, info);
    const char* const ring = Info_ValueForKey(info, title_text(0x00831a24));  // "ring"
    const int32_t value = *ring ? title_atol(ring) : 0;  // _atol
    const char* const text = own_function<const char* XB_CDECL(const char*, int32_t)>(&va_entry)(  // va
        title_text(0x0082ded4), value == 0 ? 1 : 0);  // "%i"
    Info_SetValueForKey(text, 0, title_text(0x00831a24), info);
    SV_SetConfigstring(configstring, info);
}

void XB_CDECL GScr_ObjectivePosition(int32_t index, const float* origin) {
    if (index < 0 || index >= 0x10) return;
    const int32_t configstring = index + 0x10;
    char info[0x100];
    objective_info(configstring, info);
    const int32_t z = static_cast<int32_t>(origin[2]);
    const int32_t y = static_cast<int32_t>(origin[1]);
    const int32_t x = static_cast<int32_t>(origin[0]);
    const char* const text = own_function<const char* XB_CDECL(const char*, int32_t, int32_t, int32_t)>(&va_entry)(
        title_text(0x00831a30), x, y, z);  // va("%i %i %i")
    Info_SetValueForKey(text, 0, title_text(0x00831a2c), info);  // "org"
    SV_SetConfigstring(configstring, info);
}

void XB_CDECL GScr_ObjectiveDelete(int32_t index) {
    if (index < 0 || index >= 0x10) {
        own_function<const char* XB_CDECL(const char*, int32_t, int32_t)>(&va_entry)(  // va
            &title_global<char>(0x00831b38), index, 0xf);
        return;
    }
    // The original copies 3 characters into its argument slot and then empties it.
    const int32_t configstring = index + 0x10;
    char info[4];
    title_strncpy(  // _strncpy
        info, script_text(&title_global<ScriptString>(0x00964ce0 + configstring * 4)), 3);
    info[3] = 0;
    info[0] = 0;
    SV_SetConfigstring(configstring, info);
}

void XB_CDECL GScr_SetReverb(uint32_t handle, const ScriptString* name, float a, float b) {
    // The name goes in as the string's rep pointer, not its text.
    const char* const text = own_function<const char* XB_CDECL(const char*, uint32_t, double, double)>(&va_entry)(
        title_text(0x00831d48), *reinterpret_cast<const uint32_t*>(name), x87_widen(a),
        x87_widen(b));  // va("reverb \"%s\" %g %g")
    SV_GameSendServerCommand(text, handle);
}

void XB_CDECL GScr_ToggleNoclip() {
    uint8_t* const player = field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
    if (!player) return;
    uint8_t* const client = field<uint8_t*>(player, 0x23c);
    const uint32_t noclip = field<uint32_t>(client, 0x584);
    uint32_t message;
    if (noclip) {
        message = 0x00831d78;  // "GAME_NOCLIPOFF"
        title_global<uint8_t>(0x0095f679) = 0;
    } else {
        message = 0x00831d68;  // "GAME_NOCLIPON"
        title_global<uint8_t>(0x0095f679) = 1;
    }
    field<uint32_t>(client, 0x584) = noclip == 0;
    const uint32_t handle = field<uint32_t>(player, 0x224);
    SV_GameSendServerCommand(own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
                                 title_text(0x00831d5c), title_text(message)),  // "print \"%s\""
                             handle);
}

void XB_CDECL GScr_ClosePopup(uint32_t handle) {
    SV_GameSendServerCommand(title_text(0x00831d88), handle);  // "popupclose"
}

int32_t XB_CDECL GScr_PlayerGetBool_60(uint32_t handle) {
    uint8_t* const client = handle_client(handle);
    return client && field<uint32_t>(client, 0x60) ? 1 : 0;
}

void XB_CDECL GScr_PlayerSetAngles(uint32_t handle, const float* angles) {
    uint8_t* const ent = handle_entity(handle);
    if (ent && field<uint8_t*>(ent, 0x23c)) SetClientViewAngle(angles, ent);
}

void XB_CDECL GScr_PlayerSetOrigin(uint32_t handle, const float* origin) {
    uint8_t* const ent = handle_entity(handle);
    if (ent && field<uint8_t*>(ent, 0x23c)) TeleportPlayer(ent, 0, origin);
}

void XB_CDECL GScr_PlayerGiveMaxAmmoByAmmoType(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (!client) return;
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!owns_weapon(client, weapon)) return;
    const uint32_t ammo_type = field<uint32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x598);
    give_missing_ammo(ent, client, weapon, title_global<int32_t>(0x00929818 + ammo_type * 4), 0);  // max per ammo type
}

void XB_CDECL GScr_PlayerGiveMaxAmmo(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (!client) return;
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!owns_weapon(client, weapon)) return;
    give_missing_ammo(ent, client, weapon, field<int32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x590), 0);
}

void XB_CDECL GScr_PlayerGiveWeaponWithAmmo(uint32_t handle, const ScriptString* weapon_name, int32_t count) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (!client) return;
    const uint32_t weapon = weapon_from_name(weapon_name);
    const bool owned = owns_weapon(client, weapon);
    BG_GivePlayerWeapon(weapon, client);
    Add_Ammo(ent, weapon, count, !owned);
}

void XB_CDECL GScr_PlayerGiveWeapon(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (!client) return;
    const uint32_t weapon = weapon_from_name(weapon_name);
    const bool owned = owns_weapon(client, weapon);
    BG_GivePlayerWeapon(weapon, client);
    give_missing_ammo(ent, field<uint8_t*>(ent, 0x23c), weapon,
                      field<int32_t>(weapon_def(static_cast<int32_t>(weapon)), 0x590), !owned);
}

void XB_CDECL GScr_ActorGetObstacleKind_2b0(uint32_t handle, ScriptString* out) {
    if (uint8_t* const actor = handle_actor(handle)) GScr_ReturnObstacleKind(0, 0, out, field<uint32_t>(actor, 0x2b0));
}

void XB_CDECL GScr_ActorGetObstacleKind_39c(uint32_t handle, ScriptString* out) {
    if (uint8_t* const actor = handle_actor(handle)) GScr_ReturnObstacleKind(0, 0, out, field<uint32_t>(actor, 0x39c));
}

void XB_CDECL GScr_ActorStartReacquire(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (actor && field<uint32_t>(actor, 8) == 2 && field<uint32_t>(actor, 0x28) == 0xc8)
        Actor_Exposed_StartReacquire(1, actor);
}

uint8_t XB_CDECL GScr_ActorIsStanceAllowed(uint32_t handle, uint32_t stance) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return 0;
    const uint8_t limits = field<uint8_t>(field<uint8_t*>(actor, 4), 0x46);
    uint32_t wanted = 0;
    if (stance == title_global<uint32_t>(0x0097a7e8)) {  // scr_const_stand
        if (limits & 4) return 0;
        wanted = 1;
    } else if (stance == title_global<uint32_t>(0x0097a674)) {  // scr_const_crouch
        if (limits & 2) return 0;
        wanted = 2;
    } else if (stance == title_global<uint32_t>(0x0097a764)) {  // scr_const_prone
        if (limits & 1) return 0;
        wanted = 4;
    } else {
        va_message(0x00831da8);  // "invalid stance in isStanceAllowed()\n"
    }
    uint32_t allowed = 7;
    if (field<int16_t>(actor, 0x740) > 0) {
        allowed = Path_GetAllowedStances(0, actor + 0x3c0);
    } else {
        const uint16_t* const node_handle = reinterpret_cast<const uint16_t*>(field<uint8_t*>(actor, 4) + 0x48);
        if (PathNode_HandleIsValid(node_handle)) {
            uint8_t* const self = field<uint8_t*>(actor, 0);
            const float dx = field<float>(PathNode_FromHandle(node_handle), 0x44) - field<float>(self, 0x140);
            const float dy = field<float>(PathNode_FromHandle(node_handle), 0x48) - field<float>(self, 0x144);
            const float dz = field<float>(PathNode_FromHandle(node_handle), 0x4c) - field<float>(self, 0x148);
            const float xy = platform_addss(dy * dy, dx * dx);
            if (title_global<float>(0x0083100c) > xy && title_global<float>(0x0083eb6c) > dz * dz)  // 4, 6400
                allowed = Actor_GetAllowedStanceMask(field<uint8_t*>(actor, 4));
        }
    }
    const uint32_t actor_stances = field<uint32_t>(actor, 0x244);
    uint32_t stances = actor_stances & allowed;
    if (!stances) stances = actor_stances;
    return (wanted & stances) != 0;
}

void XB_CDECL GScr_ActorBeginCriticalSection(uint32_t handle, const ScriptString* name) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor || title_global<int32_t>(0x0094cbb4) > 0) return;
    const int32_t top = field<int32_t>(actor, 0xbc);
    for (int32_t i = 0; i <= top; ++i) {
        if (field<int32_t>(actor, 0xa4 + i * 4) == 9) {
            own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
                &title_global<char>(0x00831dd0),
                script_text(reinterpret_cast<const ScriptString*>(actor + 0xc8)));  // "enterCriticalSection: ..."
            break;
        }
    }
    Actor_PushState(actor, 9);
    ScriptString* const section = reinterpret_cast<ScriptString*>(actor + 0xc8);
    if (name) {
        own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Assign_entry)(section, name);  // ScriptString_Assign
    } else {
        // Unnamed: the text the int hash map holds for the actor record's (+0x250) 16-bit +4.
        own_function<void XB_THISCALL(ScriptString*, const uint8_t*)>(&ScriptString_AssignCString)(  // ScriptString_AssignCString
            section, IntHashMap_FindValue(field<uint16_t>(field<uint8_t*>(actor, 0x250), 4)));
    }
}

namespace {

using ReleaseFn = void XB_THISCALL(ScriptString*);
using AssignFn = void XB_THISCALL(ScriptString*, const ScriptString*);

// Starts a shake/rumble descriptor on shakeRumbleMgr (0x0095f5c4, if any; strength 1), then releases its two
// channel names (+0x38, +0x18).
void start_shake_rumble(uint8_t* desc) {
    if (uint8_t* const manager = title_global<uint8_t*>(0x0095f5c4)) {
        uint32_t id;  // the original lets ShakeRumble_Start write it into one of its argument slots
        ShakeRumble_Start(desc, manager, &id, 1.0f);
    }
    own_function<ReleaseFn>(&ScriptString_Release_entry)(reinterpret_cast<ScriptString*>(desc + 0x38));  // ScriptString_Release
    own_function<ReleaseFn>(&ScriptString_Release_entry)(reinterpret_cast<ScriptString*>(desc + 0x18));
}

const char* cvar_string_or_empty(uint32_t name) {
    const uint8_t* const cvar = Cvar_FindVar(title_text(name));
    return cvar ? field<const char*>(const_cast<uint8_t*>(cvar), 4) : title_text(0x0082dfeb);  // ""
}

}  // namespace

void XB_CDECL GScr_ShakeRumble(uint32_t rumble_a, float rumble_strength, float rumble_time, uint32_t rumble_b,
                               uint32_t rumble_c, uint32_t shake_a, float shake_strength) {
    alignas(16) uint8_t desc[0x40];
    ShakeRumbleDesc_Init(desc);
    const float one = title_global<float>(0x0083eb20);
    if (shake_strength > 0.0f) {
        field<float>(desc, 0x10) = shake_strength;
        field<float>(desc, 8) = one;
        field<uint32_t>(desc, 4) = shake_a;
    } else {
        field<uint8_t>(desc, 0) = 0;
    }
    if (title_global<float>(0x0083eb94) > rumble_time) {  // 0.01
        field<uint8_t>(desc, 0x20) = 0;
    } else {
        float strength = 0.0f;  // clamped to 0..1; NaN stays
        if (!(0.0f > rumble_strength)) strength = rumble_strength > one ? one : rumble_strength;
        field<float>(desc, 0x28) = strength;
        field<uint32_t>(desc, 0x24) = rumble_a;
        field<uint32_t>(desc, 0x2c) = rumble_b;
        field<float>(desc, 0x30) = rumble_time;
        field<uint32_t>(desc, 0x34) = rumble_c;
    }
    start_shake_rumble(desc);
}

void XB_CDECL GScr_ShakeRumbleNamed(const ScriptString* rumble, float strength, const ScriptString* shake,
                                    uint32_t shake_time) {
    alignas(16) uint8_t desc[0x40];
    ShakeRumbleDesc_Init(desc);
    const float zero = title_global<float>(0x0083eafc);
    if (strength > zero) {
        own_function<AssignFn>(&ScriptString_Assign_entry)(reinterpret_cast<ScriptString*>(desc + 0x18), shake);  // ScriptString_Assign
        field<uint32_t>(desc, 0x10) = shake_time;
        field<float>(desc, 8) = title_global<float>(0x0083eb20);
    } else {
        field<uint8_t>(desc, 0) = 0;
    }
    if (strength > zero) {
        own_function<AssignFn>(&ScriptString_Assign_entry)(reinterpret_cast<ScriptString*>(desc + 0x38), rumble);
        field<float>(desc, 0x30) = strength;
        field<float>(desc, 0x28) = title_global<float>(0x0083eb20);
    } else {
        field<uint8_t>(desc, 0x20) = 0;
    }
    start_shake_rumble(desc);
}

void XB_CDECL GScr_HudSetComponentColor(int32_t component, int32_t color) {
    HudManager_SetComponentColor(hud_component(0x1c), component, color);
}

void XB_CDECL GScr_SetGoalNode(uint32_t handle, const uint16_t* node) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const sentient = field<uint8_t*>(ent, 0x244);
    if (!sentient) return;
    const uint16_t node_handle = *node;
    field<uint16_t>(sentient, 0x4a) = node_handle;
    // The node handle goes in the low half of the first argument slot, the handle's upper half stays.
    if (!field<uint8_t*>(ent, 0x23c))
        Sentient_SetGoalNode(field<uint8_t*>(ent, 0x244), (handle & 0xffff0000u) | node_handle);
}

void XB_CDECL GScr_SetRefEntByte_ee(uint32_t handle, uint32_t value) {
    if (uint8_t* const ent = handle_entity(handle))
        field<uint8_t>(CG_GetEntityRefEntity(ent), 0xee) = static_cast<uint8_t>(value);
}

void XB_CDECL GScr_SetRefEntFlag_ef(uint32_t handle, uint32_t value) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint8_t flag = static_cast<uint8_t>(value) != 0;
    field<uint8_t>(CG_GetEntityRefEntity(ent), 0xef) = flag;
}

void XB_CDECL GScr_Delete(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    Scr_Notify(ent, title_global<uint32_t>(0x0097a684));  // scr_const_death
    G_FreeEntity(ent);
}

uint32_t XB_CDECL GScr_CheckScriptAnimTreeCvars() {
    using FromCString = ScriptString* XB_THISCALL(ScriptString*, const char*);
    ScriptString tree, name;
    own_function<FromCString>(&ScriptString_FromCString)(&tree, cvar_string_or_empty(0x008322b4));  // "script_animtree"
    own_function<FromCString>(&ScriptString_FromCString)(&name, cvar_string_or_empty(0x008322a8));  // "script_name"
    const char* const tree_text = script_text(&tree);
    const char* const name_text = script_text(&name);
    if (tree_text && *tree_text && name_text && *name_text) {
        // The callback at +0x86c of the object at 0x0095f49c.
        const uint32_t check = field<uint32_t>(title_global<uint8_t*>(0x0095f49c), 0x86c);
        const uint32_t result = title_function<uint32_t XB_CDECL(const char*, const char*)>(check)(tree_text, name_text);
        own_function<ReleaseFn>(&ScriptString_Release_entry)(&name);  // ScriptString_Release
        own_function<ReleaseFn>(&ScriptString_Release_entry)(&tree);
        return result;
    }
    own_function<ReleaseFn>(&ScriptString_Release_entry)(&name);
    own_function<ReleaseFn>(&ScriptString_Release_entry)(&tree);
    return 0;
}

uint8_t XB_CDECL GScr_IsSoundPlaying(uint32_t id) { return FX_IsEffectActive(title_global<FxIdList*>(0x0095f5b4), id); }

ScriptString* XB_CDECL GScr_GetLocalizedString(ScriptString* out, uint32_t hash) {
    const char* text = nullptr;
    if (const uint8_t* const entry = Zone_FindByHash(title_global<uint8_t*>(0x0095f5d8), hash)) {  // rm_locStrings
        text = field<const char*>(const_cast<uint8_t*>(entry), 8);
        if (!text) text = field<const char*>(const_cast<uint8_t*>(entry), 0xc);
    }
    own_function<ScriptString* XB_THISCALL(ScriptString*, const char*)>(&ScriptString_FromCString)(out, text);  // ScriptString_FromCString
    return out;
}

void XB_CDECL GScr_HudSetTimer7(const int32_t* index, float seconds) { HudElem_SetTimerKind(*index, 7, seconds); }
void XB_CDECL GScr_HudSetTimer6(const int32_t* index, float seconds) { HudElem_SetTimerKind(*index, 6, seconds); }
// Inlined HudElem_SetTimerKind with kind 5 (which formats no message).
void XB_CDECL GScr_HudSetTimer(const int32_t* index, float seconds) { HudElem_SetTimerKind(*index, 5, seconds); }
void XB_CDECL GScr_HudSetTimer4(const int32_t* index, float seconds) { HudElem_SetTimerKind(*index, 4, seconds); }

uint32_t XB_CDECL GScr_FindZone(const ScriptString* name) {
    const char* text = script_text(name);
    uint8_t* const zones = field<uint8_t*>(title_global<uint8_t*>(0x0095f638), 0x18);
    if (!zones) return 0;
    const uint32_t* const found = StringMap_Find(reinterpret_cast<const uint32_t*>(zones + 8), &text);
    if (!found) return 0;
    uint32_t index = *found;
    if (index >= field<uint32_t>(zones, 0x10)) index = 0;
    return field<uint32_t*>(zones, 0x14)[index];
}

void XB_CDECL GScr_SetConfigstring9Float(float value) {
    SV_SetConfigstring(9, own_function<const char* XB_CDECL(const char*, double)>(&va_entry)(  // va
                              title_text(0x008324d4), x87_widen(value)));  // "%g"
}

int32_t XB_CDECL GScr_GetKeyBinding(ScrStringArray* out, const ScriptString* command) {
    using FromCString = ScriptString* XB_THISCALL(ScriptString*, const char*);
    const char* key1 = nullptr;
    const char* key2 = nullptr;
    int32_t result = 0;
    const char* const text = script_text(command);
    if (const uint32_t vm = title_global<uint32_t>(0x0093f7f8)) {  // cgvm
        result = own_function<int32_t XB_REGS2 XB_CDECL(uint32_t, uint32_t, int32_t, const char*, const char**,
                                                          const char**)>(&VM_Call_entry)(0, vm, 0xc, text, &key1, &key2);  // VM_Call
    } else {
        key1 = title_text(0x008344d0);  // "KEY_UNBOUND"
        key2 = title_text(0x0082dfeb);  // ""
    }
    ScriptString temp;  // the original builds it in its second argument slot
    own_function<FromCString>(&ScriptString_FromCString)(&temp, key1);
    ScrStringArray_PushBack(out, &temp);
    own_function<ReleaseFn>(&ScriptString_Release_entry)(&temp);
    own_function<FromCString>(&ScriptString_FromCString)(&temp, key2);
    ScrStringArray_PushBack(out, &temp);
    own_function<ReleaseFn>(&ScriptString_Release_entry)(&temp);
    return result;
}

void XB_CDECL GScr_GetAI(uint8_t* out, const ScriptString* team) {
    DynArray_Resize(out, 0, 0);
    uint32_t mask = 0xe;
    const char* const text = script_text(team);
    if (text && *text) {
        ScriptString context;  // "getai", built in the original's second argument slot
        own_function<ScriptString* XB_THISCALL(ScriptString*, const char*)>(&ScriptString_FromCString)(&context, title_text(0x0083258c));
        mask = Scr_TeamFromString(team);
        own_function<ReleaseFn>(&ScriptString_Release_entry)(&context);
    }
    for (uint8_t* actor = Actor_FirstOfTeams(mask); actor && field<uint8_t*>(actor, 0);
         actor = Actor_NextOfTeams(actor, mask)) {
        const uint32_t number = field<uint32_t>(field<uint8_t*>(actor, 0), 0x224);
        DynArray_PushBack(out, &number);
    }
}

float XB_CDECL GScr_GetCvarFloat(const char* name) {
    const uint8_t* const cvar = Cvar_FindVar(name);
    float value = 0.0f;
    if (cvar) std::memcpy(&value, cvar + 0x1c, 4);
    return value;
}

int32_t XB_CDECL GScr_GetCvarInt(const char* name) {
    const uint8_t* const cvar = Cvar_FindVar(name);
    return cvar ? *reinterpret_cast<const int32_t*>(cvar + 0x20) : 0;
}

namespace {

uint32_t word_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The title CRT's __CIsqrt of a float (FLD DWORD, result stored as float).
float title_ci_sqrt(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// va with any arguments (the shipped build's script messages: formatted, then dropped).
template <typename... A>
const char* title_va(uint32_t format, A... args) {
    return own_function<const char* XB_CDECL(const char*, A...)>(&va_entry)(title_text(format), args...);
}

using StringRelease = void XB_THISCALL(ScriptString*);
using StringAssign = void XB_THISCALL(ScriptString*, const ScriptString*);
using StringFromCString = ScriptString* XB_THISCALL(ScriptString*, const char*);

void string_release(ScriptString* s) { own_function<StringRelease>(&ScriptString_Release_entry)(s); }
ScriptString* string_from_rep(ScriptString* s) {
    return own_function<ScriptString* XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_FromRep)(s, 0);  // ScriptString_FromRep
}
void string_copy(ScriptString* s, const ScriptString* other) {
    own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Copy)(s, other);  // ScriptString_Copy
}
uint32_t rep_word(const ScriptString* s) { return *reinterpret_cast<const uint32_t*>(s); }
uint32_t string_length(const ScriptString* s) {
    return s->rep ? *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(s->rep) + 2) : 0;
}
char* text_of(const ScriptString* s) { return const_cast<char*>(script_text(s)); }

// The objective configstring slot's __break check (it can never fire for 0x10..0x1f) and its info copy.
void objective_slot_info(int32_t slot, char* info) {
    if (slot < 0 || slot >= 0x400)
        title_function<void XB_CDECL(uint32_t, uint32_t, int32_t)>(0x002938f0)(1, 0x00832d34, slot);  // __break
    objective_info(slot, info);
}

void set_objective_state_key(char* info, uint32_t state) {
    Info_SetValueForKey(title_va(0x0082ded4, state), 0, title_text(0x00831b30), info);  // "%i", "state"
}

const char* localized_text(uint32_t hash) {
    const char* text = nullptr;
    if (const uint8_t* const entry = Zone_FindByHash(title_global<uint8_t*>(0x0095f5d8), hash)) {  // rm_locStrings
        text = field<const char*>(const_cast<uint8_t*>(entry), 8);
        if (!text) text = field<const char*>(const_cast<uint8_t*>(entry), 0xc);
    }
    return text;
}

// The *IntText objective builtins: "%d" of the number, checked (alphanumeric or '_' when longer than one
// character; a message per bad character).
void int_text(char* buf, int32_t number) {
    title_sprintf(buf, title_text(0x008325b8), number);
    const int32_t length = static_cast<int32_t>(std::strlen(buf));
    if (length <= 1) return;
    for (int32_t i = 0; i < length; ++i) {
        if (!own_function<int32_t XB_CDECL(int32_t)>(&title_isalnum)(buf[i]) && buf[i] != '_')  // _isalnum
            title_va(0x00831b80, static_cast<const char*>(buf));  // "Illegal localized string reference: %s ..."
    }
}

bool has_strchr(const char* text, int32_t c) {
    return own_function<const char* XB_CDECL(const char*, int32_t)>(&title_strchr)(text, c) != nullptr;  // _strchr
}

// The 31 high characters objective strings may hold (built on the stack per character by the original).
const uint8_t kObjectiveChars[31] = {0xe0, 0xe1, 0xe2, 0xe4, 0xe9, 0xe8, 0xea, 0xeb, 0xc7, 0xee, 0xef,
                                     0xf4, 0xf2, 0xd6, 0xfc, 0xed, 0xf3, 0xfa, 0xf1, 0xa1, 0xba, 0xec,
                                     0xf9, 0xdf, 0xf6, 0xc4, 0xdc, 0xc9, 0xd1, 0xbf, 0xc8};

}  // namespace

uint32_t XB_CDECL GScr_GetCvar(ScriptString* out, const char* name) {
    char buf[0x80];
    const uint8_t* const cvar = Cvar_FindVar(name);
    if (!cvar) {
        buf[0] = 0;
    } else {
        title_strncpy(  // _strncpy
            buf, field<const char*>(const_cast<uint8_t*>(cvar), 4), 0x7f);
        buf[0x7f] = 0;
    }
    return own_function<uint32_t XB_THISCALL(ScriptString*, const char*)>(&ScriptString_AssignCString)(out, buf);  // ScriptString_AssignCString
}

uint32_t XB_CDECL GScr_GetEntVar(uint32_t handle, uint32_t key, uint32_t callback) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const list = field<uint8_t*>(ent, 0x234);
    if (!list) return 0;
    return G_EntityRemoveNotifyCallback(0, list, key, callback);
}

uint32_t XB_CDECL GScr_SetEntVar(uint32_t handle, uint32_t key, uint32_t callback) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    if (!field<uint8_t*>(ent, 0x234)) {
        uint8_t* list = SmallHeap_Alloc(title_global<uint8_t*>(0x0095f568), 0x3c, 0);
        if (list) list = own_function<uint8_t* XB_REGS1 XB_CDECL(uint8_t*)>(&EntNotifyCallbacks_Init)(list);  // NotifyCallbackList_Init
        field<uint8_t*>(ent, 0x234) = list;
    }
    return G_EntityAddNotifyCallback(field<uint8_t*>(ent, 0x234), key, callback);
}

void XB_CDECL GScr_ObjectiveCurrent(int32_t index) {
    if (index < 0 || index >= 0x10) {
        title_va(0x00831b38, index, 0xf);  // "index %i is an illegal objective index. ..."
        return;
    }
    // Every objective's "state" key: 4 (current) for this one, 1 for the others that were current.
    for (int32_t slot = 0x10; slot - 0x10 < 0x10; ++slot) {
        char info[0x100];
        objective_slot_info(slot, info);
        const char* const state = Info_ValueForKey(info, title_text(0x00831b30));  // "state"
        const int32_t value = *state ? title_atol(state) : 0;  // _atol
        if (slot - 0x10 == index) {
            if (value == 4) continue;
            set_objective_state_key(info, 4);
            SV_SetConfigstring(index + 0x10, info);
        } else if (value == 4) {
            set_objective_state_key(info, 1);
            SV_SetConfigstring(slot, info);
        }
    }
    ScriptString state;
    string_from_rep(&state);
    own_function<StringAssign>(&ScriptString_Assign_entry)(&state, &title_global<ScriptString>(0x00984ae0));  // ScriptString_Assign
    G_SendObjectiveUpdate(&state, static_cast<uint32_t>(index));
    string_release(&state);
}

void XB_CDECL GScr_ObjectiveState(int32_t index, const ScriptString* name) {
    ScriptString state;
    string_from_rep(&state);
    if (index < 0 || index >= 0x10) {
        title_va(0x00831b38, index, 0xf);
        string_release(&state);
        return;
    }
    const int32_t slot = index + 0x10;
    char info[0x100];
    objective_slot_info(slot, info);
    own_function<StringAssign>(&ScriptString_Assign_entry)(&state, name);
    uint32_t number;
    if (GScr_ObjectiveStateFromString(&state, &number)) {
        set_objective_state_key(info, number);
        SV_SetConfigstring(slot, info);
        G_SendObjectiveUpdate(&state, static_cast<uint32_t>(index));
    }
    string_release(&state);
}

void XB_CDECL GScr_ObjectiveAddWithPosition(int32_t index, const ScriptString* state_name, const ScriptString* text,
                                            const float* origin) {
    ScriptString state;
    string_copy(&state, state_name);
    if (index < 0 || index >= 0x10) {
        title_va(0x00831b38, index, 0xf);
        string_release(&state);
        return;
    }
    char info[0x100];
    objective_slot_info(index + 0x10, info);
    uint32_t number;
    if (!GScr_ObjectiveStateFromString(&state, &number)) {
        string_release(&state);
        return;
    }
    set_objective_state_key(info, number);
    const int32_t length = static_cast<int32_t>(string_length(text));
    for (int32_t i = 0; i < length; ++i) {
        if (text_of(text)[i] == 0x14 || text_of(text)[i] == 0x15)
            title_va(0x00831b04, static_cast<int32_t>(text_of(text)[i]));  // "bad escape character (%i) ..."
    }
    if (length >= 0x100) {
        title_va(0x00831ad8, 0xff, rep_word(text));  // "Objective strings is too long (> %i): %s" (the rep pointer)
        string_release(&state);
        return;
    }
    if (has_strchr(text_of(text), '"') || has_strchr(text_of(text), ';') || has_strchr(text_of(text), '\\')) {
        title_va(0x00831a80, static_cast<const char*>(text_of(text)));  // "Objective strings can not have ..."
        string_release(&state);
        return;
    }
    // Control and high characters outside the allowed set become spaces (in the string itself).
    for (int32_t i = 0; i < length; ++i) {
        const uint8_t c = static_cast<uint8_t>(text_of(text)[i]);
        if ((c >= 1 && c <= 0x1f) || c >= 0x7f) {
            char* const chars = text_of(text);
            bool allowed = false;
            for (int32_t k = 0; k < 31; ++k) {
                if (c == kObjectiveChars[k]) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                title_va(0x00831a3c, static_cast<int32_t>(static_cast<int8_t>(c)), static_cast<int32_t>(c),
                         static_cast<const char*>(text_of(text)));  // "Illegal character '%c'(ascii %i) ..."
                chars[i] = ' ';
            }
        }
    }
    Info_SetValueForKey(text_of(text), 0, title_text(0x00831a78), info);  // "str"
    const int32_t z = static_cast<int32_t>(origin[2]);
    const int32_t y = static_cast<int32_t>(origin[1]);
    const int32_t x = static_cast<int32_t>(origin[0]);
    Info_SetValueForKey(title_va(0x00831a30, x, y, z), 0, title_text(0x00831a2c), info);  // "%i %i %i", "org"
    SV_SetConfigstring(index + 0x10, info);
    G_SendObjectiveUpdate(&state, static_cast<uint32_t>(index));
    string_release(&state);
}

void XB_CDECL GScr_ObjectiveAddLocalizedWithPosition(int32_t index, const ScriptString* state, uint32_t hash,
                                                     const float* origin) {
    ScriptString text;
    own_function<StringFromCString>(&ScriptString_FromCString)(&text, localized_text(hash));  // ScriptString_FromCString
    GScr_ObjectiveAddWithPosition(index, state, &text, origin);
    string_release(&text);
}

void XB_CDECL GScr_ObjectiveAddIntTextWithPosition(int32_t index, const ScriptString* state, int32_t number,
                                                   const float* origin) {
    char buf[0x400];
    int_text(buf, number);
    ScriptString text;
    own_function<StringFromCString>(&ScriptString_FromCString)(&text, buf);
    GScr_ObjectiveAddWithPosition(index, state, &text, origin);
    string_release(&text);
}

void XB_CDECL GScr_ObjectiveAdd(int32_t index, const ScriptString* state_name, const ScriptString* text) {
    ScriptString state;
    string_copy(&state, state_name);
    if (index < 0 || index >= 0x10) {
        title_va(0x00831b38, index, 0xf);
        string_release(&state);
        return;
    }
    const int32_t slot = index + 0x10;
    char info[0x100];
    objective_slot_info(slot, info);
    uint32_t number;
    if (!GScr_ObjectiveStateFromString(&state, &number)) {
        string_release(&state);
        return;
    }
    set_objective_state_key(info, number);
    if (text->rep) {
        const int32_t length = static_cast<int32_t>(string_length(text));
        // Escape characters are reported up to the first letter.
        for (int32_t i = 0; i < length; ++i) {
            if (own_function<int32_t XB_CDECL(int32_t)>(&title_isalpha)(text_of(text)[i])) break;  // _isalpha
            if (text_of(text)[i] == 0x14 || text_of(text)[i] == 0x15)
                title_va(0x00831b04, static_cast<int32_t>(text_of(text)[i]));
        }
        if (length >= 0x100) {
            title_va(0x00831ad8, 0xff, rep_word(text));
            string_release(&state);
            return;
        }
        if (has_strchr(text_of(text), '"') || has_strchr(text_of(text), ';') || has_strchr(text_of(text), '\\')) {
            title_va(0x00831a80, rep_word(text));  // the rep pointer, not the text
            string_release(&state);
            return;
        }
        Info_SetValueForKey(text_of(text), 0, title_text(0x00831a78), info);  // "str"
    }
    SV_SetConfigstring(slot, info);
    G_SendObjectiveUpdate(&state, static_cast<uint32_t>(index));
    string_release(&state);
}

void XB_CDECL GScr_ObjectiveAddLocalized(int32_t index, const ScriptString* state, uint32_t hash) {
    ScriptString text;
    own_function<StringFromCString>(&ScriptString_FromCString)(&text, localized_text(hash));
    GScr_ObjectiveAdd(index, state, &text);
    string_release(&text);
}

void XB_CDECL GScr_ObjectiveAddIntText(int32_t index, const ScriptString* state, int32_t number) {
    char buf[0x400];
    int_text(buf, number);
    ScriptString text;
    own_function<StringFromCString>(&ScriptString_FromCString)(&text, buf);
    GScr_ObjectiveAdd(index, state, &text);
    string_release(&text);
}

void XB_CDECL GScr_ObjectiveAddNoText(int32_t index, const ScriptString* state) {
    ScriptString text;
    GScr_ObjectiveAdd(index, state, string_from_rep(&text));
    string_release(&text);
}

// HudText_SetStringByHash runs where the original's call puts it (entry ESP - 8, its callees store and read stack
// words there): the C frame sits below a gap; the original's saved ESI and return address are written too.
PLATFORM_ASM_ENTRY(GScr_HudSetTextByHash_entry,
                   "movl %esi, -4(%esp)\n"
                   "movl $0x002fef90, -8(%esp)\n"
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(GScr_HudSetTextByHash) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL GScr_HudSetTextByHash(uint32_t entry_esp, uint32_t ebx, uint32_t edi) {
    // HudText_SetStringByHash with the HUD manager itself as the element (ESI) and the caller's EBX/EDI.
    const uint32_t hash = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(entry_esp + 4));
    HudText_SetStringByHash(title_global<uint8_t*>(0x0095f8f0), hash, entry_esp - 8, ebx, edi);
}

void XB_CDECL GScr_SetHudMode(uint32_t mode) {
    uint8_t* const manager = title_global<uint8_t*>(0x0095f8f0);  // g_hudManager
    if (mode == field<uint32_t>(manager, 0x5c)) return;
    field<uint32_t>(manager, 0x5c) = mode;
    switch (mode) {
        case 0: HudManager_LayoutDefault(manager); break;
        case 1: HudManager_HideAll(manager); break;
        case 2: HudManager_Layout2(manager); break;
        case 3: HudManager_Layout3(manager); break;
        default: break;
    }
}

float XB_CDECL GScr_GetAnimLength(uint32_t, uint32_t anim) {
    // The anim's node: tree anim >> 16 (0 when out of range) of rm_animTrees +4 {count, entries of 12 bytes
    // {?, count, nodes}}, node anim & 0xffff (0 when out of range), 0x38 bytes each.
    const uint8_t* const trees = field<uint8_t*>(title_global<uint8_t*>(0x0095f524), 4);
    uint32_t tree = anim >> 16;
    if (tree >= field<uint32_t>(const_cast<uint8_t*>(trees), 0)) tree = 0;
    const uint8_t* const entry = field<uint8_t*>(const_cast<uint8_t*>(trees), 4) + tree * 12 + 4;
    uint32_t index = anim & 0xffff;
    if (index >= field<uint32_t>(const_cast<uint8_t*>(entry), 0)) index = 0;
    uint8_t* const node = field<uint8_t*>(const_cast<uint8_t*>(entry), 4) + index * 0x38;
    if (!field<uint8_t*>(node, 0x24)) AnimData_EnsureLoaded(node);
    if (uint8_t* const clip = field<uint8_t*>(node, 0x24)) return field<float>(clip, 0x38);
    return title_global<float>(0x0083eb20);  // 1
}

void XB_CDECL GScr_SetHintString(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (field<uint32_t>(ent, 0x260) != title_global<uint32_t>(0x0097a81c) && !field<uint8_t*>(ent, 0x240))
        return;  // neither a trigger_use nor an actor
    // The text buffer is never filled in the shipped build (stack garbage; tests zero it).
    char text[0x100] = {};
    int32_t index;
    if (!G_FindHintStringIndex(&index, text)) {
        title_va(0x00831fa8, 0x20);  // "Too many different hintstring values. ..."
        return;
    }
    if (field<uint32_t>(ent, 0x260) == title_global<uint32_t>(0x0097a81c))  // scr_const_trigger_use
        field<uint8_t>(ent, 5) = static_cast<uint8_t>(index);
    else
        field<uint16_t>(field<uint8_t*>(ent, 0x240), 0x1380) = static_cast<uint16_t>(index);
}

void XB_CDECL GScr_ConnectPaths(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (!(field<uint32_t>(ent, 0x2a4) & 0x1000)) {
        title_va(0x008320b8, script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)));  // "entity of type ..."
        return;
    }
    Path_ConnectPathsForEntity(ent, title_global<uint8_t*>(0x0095f4f4));  // g_pathNodeBlocks
}

void XB_CDECL GScr_DoDamage(uint32_t handle, float damage, const float* point) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    alignas(16) float at[3] = {point[0], point[1], point[2]};
    // Direction from the point to the client's origin (+0 of the client) or the entity's (+0x140), normalized
    // (none for a zero length).
    const uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    const float* const to = client ? reinterpret_cast<const float*>(client) : &field<float>(ent, 0x140);
    float dir[3];
    dir[0] = to[0] - at[0];
    dir[1] = to[1] - at[1];
    dir[2] = to[2] - at[2];
    const float squared =
        static_cast<float>((platform_x87_ld(dir[2]) * dir[2] + platform_x87_ld(dir[1]) * dir[1]) +
                           platform_x87_ld(dir[0]) * dir[0]);
    const float length = title_ci_sqrt(squared);
    const float* direction = nullptr;
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inv = title_global<float>(0x0083eb20) / length;
        dir[0] = platform_mulss(inv, dir[0]);
        dir[1] = platform_mulss(inv, dir[1]);
        dir[2] = platform_mulss(inv, dir[2]);
        direction = dir;
    }
    // G_Damage(point EAX; targ, no inflictor/attacker, dir, damage, 0, mod 0x1b, hit location 2, no weapon).
    own_function<void XB_REGS1 XB_CDECL(const float*, uint8_t*, uint32_t, uint32_t, const float*, int32_t,
                                          uint32_t, uint32_t, uint32_t, int32_t)>(&G_Damage_entry)(
        at, ent, 0, 0, direction, static_cast<int32_t>(damage), 0, 0x1b, 2, -1);
}

uint32_t XB_CDECL GScr_UseBy(uint32_t handle, uint32_t other_handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const other = handle_entity(other_handle);
    if (!other) return 0;
    // Notify "trigger" with the other entity (G_NotifyWithEntity, entity in EDI; the number goes in the
    // original's first argument slot), then the use callback.
    uint32_t number = field<uint32_t>(other, 0x224);
    const TitleRegs regs{number, 0, word_of(&number), title_global<uint32_t>(0x0097a818), word_of(other), word_of(ent)};
    const uint32_t args[2] = {title_global<uint32_t>(0x0097a818), word_of(&number)};  // scr_const_trigger
    g_ent_call(own_address(&G_NotifyWithEntity_entry), &regs, 2, args);
    if (const uint32_t use = field<uint32_t>(ent, 0x33c))
        title_function<void XB_CDECL(uint8_t*, uint8_t*, uint8_t*)>(title_global<uint32_t>(0x008da840 + use * 4))(
            ent, other, other);  // entityUseFuncs
    return field<uint32_t>(other, 0x224);
}

void XB_CDECL GScr_FreeSound(uint32_t id) { FX_FreeEffectsById(title_global<FxIdList*>(0x0095f5b4), id); }

namespace {

// The GScr_GetSpawner*Array builtins: entity numbers of the spawners (type byte 0xc) in the list at 0x0097d370
// (count 0x0097d36c) whose team bit (+0x3b4) is in the mask, at most 0x200, into out (DynArray).
void spawner_array(uint8_t* out, bool by_team, uint32_t mask) {
    uint32_t numbers[0x200] = {};
    int32_t count = 0;
    const uint32_t* const end = &title_global<uint32_t>(0x0097d370 + title_global<uint32_t>(0x0097d36c) * 4);
    for (const uint32_t* p = &title_global<uint32_t>(0x0097d370); p != end; ++p) {
        uint8_t* const ent = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*p));
        if (!ent || ent[0] != 0xc) continue;
        if (by_team && !(mask & (1u << (field<uint32_t>(ent, 0x3b4) & 0x1f)))) continue;
        if (count >= 0x200) continue;
        numbers[count++] = field<uint32_t>(ent, 0x224);
    }
    DynArray_Resize(out, 0, 0);
    DynArray_Reserve(out, static_cast<uint32_t>(count));
    for (const uint32_t* p = numbers; p != numbers + count; ++p) DynArray_PushBack(out, p);
}

// The SetCvar* builtins' copy of the value with '"' turned into '\'' (at most 0x400 characters, no NUL): the
// copy is never used afterwards.
void quote_copy(const char* value) {
    volatile char copy[0x400];
    for (int32_t i = 0; i < 0x400; ++i) {
        const char c = value[i];
        if (!c) break;
        copy[i] = c == '"' ? '\'' : c;
    }
}

void set_cvar_text(const char* name, char* value) {
    Cvar_Get(name, value, 0x1000);
    Cvar_Set2(name, value, 1);
}

// "gm" / "gmb": the text plus a newline as a game message to everyone.
void send_game_message(uint32_t command, const char* text) {
    char buf[0x2000];
    title_sprintf(buf, title_text(0x008325cc), text);  // "%s\n"
    SV_GameSendServerCommand(title_va(0x008325c4, title_text(command), static_cast<const char*>(buf)), 0);  // "%s \"%s\""
}

// The sentient's goal position from a point (a 16-byte block: x, y, z, x).
void set_goal_pos(uint8_t* sentient, const float* point) {
    alignas(16) float pos[4] = {point[0], point[1], point[2], point[0]};
    Sentient_SetGoalPos(pos, sentient);
}

uint8_t* handle_sentient(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<uint8_t*>(ent, 0x244) : nullptr;
}

}  // namespace

uint8_t* XB_CDECL GScr_GetZoneAtPoint(float x, float y, float z) {
    alignas(16) float point[4] = {x, y, z, x};
    const int32_t cell = CM_CellForPoint(0, point);
    if (cell == -1) {
        uint8_t* const pak = title_global<uint8_t*>(0x0095f638);
        return MemContext_FindById(pak, field<int32_t>(pak, 0x34));
    }
    // The cell's zone: world (0x0095f640) cell set +0x1cc -> {.., count +0x1c, cells +0x20}, cell +0x2c -> +0x24.
    uint8_t* const world = title_global<uint8_t*>(0x0095f640);
    uint8_t* const cells = field<uint8_t*>(world, 4 + field<uint32_t>(world, 0x1cc) * 4);
    uint32_t index = static_cast<uint32_t>(cell);
    if (index >= field<uint32_t>(cells, 0x1c)) index = 0;
    return field<uint8_t*>(field<uint8_t*>(field<uint8_t**>(cells, 0x20)[index], 0x2c), 0x24);
}

void XB_CDECL GScr_RandomTracer(const float* to, const float* from) {
    // A tracer from `from` to `to` with chance scale * 32 / 32 (scale 0x0094a8e0, 0.7 when weapon 0's class +0xa8
    // is 3 or 4).
    alignas(16) float start[4] = {from[0], from[1], from[2], from[0]};
    alignas(16) float end[4] = {to[0], to[1], to[2], to[0]};
    float scale = title_global<float>(0x0094a8e0);
    if (const uint8_t* const def = weapon_def(0)) {
        const uint32_t kind = field<uint32_t>(const_cast<uint8_t*>(def), 0xa8);
        if (kind == 3 || kind == 4) scale = title_global<float>(0x0083ed28);
    }
    const int32_t r = own_function<int32_t XB_CDECL()>(&title_rand)() & 0x1f;  // rand
    if (scale * title_global<float>(0x0083eeac) > static_cast<float>(r)) CG_SpawnTracer(start, 0, end, 0);
}

void XB_CDECL GScr_GetSpawnerTeamArray(uint8_t* out, const ScriptString* team) {
    ScriptString context;  // "getspawnerteamarray"
    own_function<StringFromCString>(&ScriptString_FromCString)(&context, title_text(0x00832578));
    const uint32_t mask = Scr_TeamFromString(team);
    string_release(&context);
    spawner_array(out, true, mask);
}

void XB_CDECL GScr_GetSpawnerArray(uint8_t* out) { spawner_array(out, false, 0); }

void XB_CDECL GScr_GetAIAll(uint8_t* out) {
    ScriptString none;
    string_from_rep(&none);
    GScr_GetAI(out, &none);
    string_release(&none);
}

void XB_CDECL GScr_MusicStop() { S_StopSoundHandle(title_global<uint32_t*>(0x0095f4cc)); }

void XB_CDECL GScr_SetCvarFloat(const char* name, float value) {
    char buf[0x400];
    title_sprintf(buf, title_text(0x008325b4),
                                                                             x87_widen(value));  // "%f"
    quote_copy(buf);
    set_cvar_text(name, buf);
}

void XB_CDECL GScr_SetCvarInt(const char* name, int32_t value) {
    char buf[0x400];
    title_sprintf(buf, title_text(0x008325b8), value);  // "%d"
    quote_copy(buf);
    set_cvar_text(name, buf);
}

void XB_CDECL GScr_SetCvar(const char* name, char* value) {
    quote_copy(value);
    set_cvar_text(name, value);
}

void XB_CDECL GScr_SendGameMessageBold(const char* text) { send_game_message(0x008325bc, text); }  // "gmb"
void XB_CDECL GScr_SendGameMessage(const char* text) { send_game_message(0x008325c0, text); }      // "gm"

void XB_CDECL GScr_ObjectiveString(int32_t index, const ScriptString* text) { G_SetObjectiveString(index, text, 0); }
void XB_CDECL GScr_ObjectiveStringLocalized(int32_t index, const ScriptString* text) {
    G_SetObjectiveString(index, text, 1);
}

void XB_CDECL GScr_PlayLocalSound(uint32_t handle, const ScriptString* alias) {
    if (S_FindSoundByHash(script_text(alias)) == -1)
        title_va(0x00831d2c, rep_word(alias));  // "unknown sound alias '%s'" (the rep pointer)
    const int32_t index = G_FindConfigstringIndex(0x40, script_text(alias), 0xa0, 1, nullptr);
    SV_GameSendServerCommand(title_va(0x00831d24, static_cast<uint32_t>(index & 0xff)), handle);  // "ls %i"
}

uint32_t XB_CDECL GScr_PlayerSwitchToWeapon(uint32_t handle, const ScriptString* weapon_name) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return 0;
    const uint32_t weapon = weapon_from_name(weapon_name);
    if (!weapon) title_va(0x00831d94, rep_word(weapon_name));  // "unknown weapon '%s'" (the rep pointer)
    if (!owns_weapon(field<uint8_t*>(ent, 0x23c), weapon)) return 0;
    CG_SelectWeapon(static_cast<int32_t>(weapon));
    return 1;
}

float XB_CDECL GScr_ActorGetObstacleYaw_3a8(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return title_global<float>(0x0083eafc);
    const float zero = 0.0f;
    const float v[3] = {zero - field<float>(actor, 0x3a8), zero - field<float>(actor, 0x3ac), zero};
    return vectoyaw(v);
}

float XB_CDECL GScr_ActorGetMotionYaw(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return title_global<float>(0x0083eafc);
    // Q_fmod of the yaw by the entity's +0x154.
    if (field<int16_t>(actor, 0x740) > 0) {
        // Along the path (+0x75c, +0x760).
        const float v[2] = {field<float>(actor, 0x75c), field<float>(actor, 0x760)};
        const float reference = field<float>(field<uint8_t*>(actor, 0), 0x154);
        return Q_fmod(vectoyaw(v), reference);
    }
    const float* const velocity = &field<float>(actor, 0x2a0);
    const float squared = platform_addss(platform_addss(velocity[0] * velocity[0], velocity[1] * velocity[1]),
                                         velocity[2] * velocity[2]);
    if (squared > title_global<float>(0x0083eb20)) {
        const float reference = field<float>(field<uint8_t*>(actor, 0), 0x154);
        return Q_fmod(vectoyaw(velocity), reference);
    }
    return 0.0f;
}

void XB_CDECL GScr_SetGoalPosFromNode(uint32_t handle, const int32_t* node_handle) {
    uint8_t* const sentient = handle_sentient(handle);
    if (!sentient) return;
    const int32_t value = *node_handle;
    if (value == 0 || value == -1) return;
    const uint16_t node_word = static_cast<uint16_t>(value);
    uint8_t* const node = PathNode_FromHandleInTable(&node_word, title_global<uint8_t*>(0x0095f4f4));  // g_pathNodeBlocks
    if (!node) return;
    set_goal_pos(sentient, &field<float>(node, 0x44));
    if (field<uint8_t>(node, 0x2d) & 0x80) Sentient_SetGoalYaw(sentient, field<float>(node, 0x50));
    title_global<uint32_t>(0x0095f704) = 0;
}

void XB_CDECL GScr_SetGoalPos(uint32_t handle, const float* point) {
    uint8_t* const sentient = handle_sentient(handle);
    if (!sentient) return;
    set_goal_pos(sentient, point);
    title_global<uint32_t>(0x0095f704) = 0;
}

void XB_CDECL GScr_SetGoalPosAndYaw(uint32_t handle, const float* point, float yaw) {
    uint8_t* const sentient = handle_sentient(handle);
    if (!sentient) return;
    set_goal_pos(sentient, point);
    // The yaw wrapped into [0, 360) at +0x20 (NaN stays).
    const float full = title_global<float>(0x0083ebcc);
    if (0.0f > yaw) {
        do yaw = yaw + full;
        while (0.0f > yaw);
    } else if (yaw >= full) {
        do yaw = yaw - full;
        while (yaw >= full);
    }
    field<float>(sentient, 0x20) = yaw;
    title_global<uint32_t>(0x0095f704) = 0;
}

namespace {

// The int map of the owner at 0x0095f4e0 (+0x350): the node of key, inserted as {key, 0} when missing.
uint8_t* int_map_node(int32_t key) {
    uint8_t* const map = title_global<uint8_t*>(0x0095f4e0) + 0x350;
    uint32_t node;
    IntHashMap_Find(&node, &key, map);
    if (node == field<uint32_t>(map, 8)) {
        const uint32_t pair[2] = {static_cast<uint32_t>(key), 0};
        uint32_t out[2];
        node = *IntHashMap_Insert(map, out, pair);
    }
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(node));
}

// repz cmpsb over n bytes: equal when all n match.
bool bytes_equal(const char* a, uint32_t b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i)
        if (a[i] != title_text(b)[i]) return false;
    return true;
}

void notify_entity(uint8_t* ent, uint32_t notify) { Scr_Notify(ent, notify); }

// The animated entity's tree: an actor (type 0xb) gets one created (AnimTree_Create over 0x00987a90) when its
// entity (+0) has none; the tree (+0x254) of the actor's entity or of the entity itself.
uint8_t* anim_tree_of(uint8_t* ent) {
    if (ent[0] == 0xb) {
        uint8_t* const actor = field<uint8_t*>(ent, 0x240);
        uint8_t* const self = field<uint8_t*>(actor, 0);
        if (!field<uint8_t*>(self, 0x254))
            field<uint8_t*>(field<uint8_t*>(actor, 0), 0x254) = AnimTree_Create(self, title_global<uint8_t*>(0x00987a90));
        ent = field<uint8_t*>(actor, 0);
    }
    return field<uint8_t*>(ent, 0x254);
}

// 16-bit angle rounding of the orient-mode yaws: (float)((int)(a * 182.044) & 0xffff) * 0.00549316.
float short_angle(float a) {
    const int32_t s = static_cast<int32_t>(a * title_global<float>(0x0083ebd8)) & 0xffff;
    return static_cast<float>(s) * title_global<float>(0x0083ebd4);
}

}  // namespace

uint32_t XB_CDECL GScr_GetIntMapValue(int32_t key) { return field<uint32_t>(int_map_node(key), 0xc); }

void XB_CDECL GScr_SetIntMapValue(int32_t key, uint32_t value) { field<uint32_t>(int_map_node(key), 0xc) = value; }

void XB_CDECL GScr_SaveCheckpoint(const char* name) { G_SaveCheckpoint(name, title_global<uint8_t*>(0x0095f4e0), 1); }

void XB_CDECL GScr_ShellShock(uint32_t handle, const ScriptString* name, float duration) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint32_t kind = 1;
    ScriptString registered;  // the original builds it in its first argument slot
    string_from_rep(&registered);
    own_function<StringAssign>(&ScriptString_Assign_entry)(&registered, &title_global<ScriptString>(0x009655a4));
    const char* const done = script_text(&registered);
    if (!done || !*done) {
        // Registers the three shell shock names (configstrings 0x230.., at most 2).
        G_FindConfigstringIndex(2, title_text(0x00831f70), 0x230, 1, nullptr);  // "default"
        G_FindConfigstringIndex(2, title_text(0x00831f68), 0x230, 1, nullptr);  // "pain"
        G_FindConfigstringIndex(2, title_text(0x00831f60), 0x230, 1, nullptr);  // "death"
    }
    if (bytes_equal(script_text(name), 0x00831f70, 8)) {
        kind = 1;
    } else if (bytes_equal(script_text(name), 0x00831f68, 5)) {
        kind = 2;
    } else if (bytes_equal(script_text(name), 0x00831f60, 6)) {
        kind = 3;
    }
    const int32_t ms = static_cast<int32_t>(duration * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24));
    if (ms < 0 || ms > 60000) {
        const double seconds = static_cast<double>(platform_x87_ld(ms) * title_global<float>(0x0083eb4c));
        title_va(0x00831f38, seconds);  // "duration %g should be >= 0 and <= 60"
    }
    CG_ReadShellShockCvars(&title_global<uint8_t>(0x00975b44 + kind * 0x7c));
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x4f4) = kind;
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x4f8) = title_global<uint32_t>(0x0098540c);  // level_time
    field<int32_t>(field<uint8_t*>(ent, 0x23c), 0x4fc) = ms;
    string_release(&registered);
}

void XB_CDECL GScr_DebugDrawAnimTrees(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (uint8_t* const obj = field<uint8_t*>(ent, 0x22c)) DObj_DebugDrawAnimTrees(reinterpret_cast<uint8_t* const*>(obj));
}

uint8_t XB_CDECL GScr_StartSubtitle(const ScriptString* name) { return Subtitle_Start(script_text(name), nullptr); }

int32_t XB_CDECL GScr_PlaySoundWithNotify(uint32_t handle, const ScriptString* alias, uint32_t notify, uint32_t force) {
    Subtitle_Start(script_text(alias), nullptr);
    uint8_t* const ent = handle_entity(handle);
    if (!ent) {
        // Notified on the level's entity (+8 of 0x0095f534).
        notify_entity(field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), notify);
        return -1;
    }
    const char* const text = script_text(alias);
    if (!text || !*text) {
        notify_entity(ent, notify);
        return -1;
    }
    // Entities with +0x3c4 play only while 0x0095f4b4 is set or when forced.
    if (field<uint32_t>(ent, 0x3c4) && !title_global<uint8_t>(0x0095f4b4) && !static_cast<uint8_t>(force)) {
        notify_entity(ent, notify);
        return -1;
    }
    const int32_t sound = S_FindSoundByHash(text);
    if (sound == -1) {
        notify_entity(ent, notify);
        return -1;
    }
    alignas(16) uint32_t origin[4];
    std::memcpy(origin, ent + 0x140, 16);
    alignas(16) uint32_t velocity[4] = {title_global<uint32_t>(0x0082e110), title_global<uint32_t>(0x0082e114),
                                        title_global<uint32_t>(0x0082e118), title_global<uint32_t>(0x0082e11c)};
    uint32_t sound_handle;
    S_PlaySoundAlias(static_cast<uint32_t>(sound), 0, title_global<uint8_t*>(0x0095f4c4), &sound_handle, handle, 1, 0,
                     word_of(origin), word_of(velocity), 0xbf800000u, 0xbf800000u, 0xbf800000u, 0xbf800000u);
    if (!SoundHandle_Resolve(&sound_handle)) {
        notify_entity(ent, notify);
        return -1;
    }
    if (SoundHandle_Resolve(&sound_handle) && notify) field<uint32_t>(SoundHandle_Resolve(&sound_handle), 0x2c) = notify;
    return 1;
}

void XB_CDECL GScr_HudSetText(const int32_t* hud, const ScriptString* text) {
    uint8_t* const elem = hud_elem(*hud);
    for (uint32_t offset = 0x30; offset <= 0x48; offset += 4) field<uint32_t>(elem, offset) = 0;
    field<uint32_t>(elem, 0x5c) = 0;
    field<uint32_t>(elem, 0x60) = 0;
    field<float>(elem, 0x64) = 0.0f;
    field<uint32_t>(elem, 0x68) = 0;
    // The empty string's null text is read as well, as in the original.
    const char* const chars = script_text(text);
    int32_t index = 0;
    if (*chars) index = G_FindConfigstringIndex(0x40, chars, 0x293, 1, title_text(0x00832888));  // "localized string"
    field<int32_t>(elem, 0x68) = index;
}

void XB_CDECL GScr_MissionFailed(const ScriptString* reason) {
    const uint8_t* const player = G_FindEntityByIntField(&title_global<uint8_t>(0x0097a8c0), 0x260,
                                                         title_global<int32_t>(0x0097a75c));  // scr_const_player
    if (!player || (field<uint32_t>(const_cast<uint8_t*>(player), 0x2a4) & 1)) return;
    ClientDeathReload();
    title_global<uint32_t>(0x00985eb8) = 0;
    title_global<uint32_t>(0x00985ebc) = 1;
    using AssignCString = uint32_t XB_THISCALL(ScriptString*, const char*);
    ScriptString* const message = &title_global<ScriptString>(0x00985ec0);
    const char* const text = script_text(reason);
    if (!text || bytes_equal(text, 0x008324b8, 10)) {  // "NEED_LOVE"
        own_function<AssignCString>(&ScriptString_AssignCString)(message, title_text(0x008324a4));  // "INGAME_PLAYER_DIED"
        return;
    }
    const char* const localized = Loc_GetString(text);
    own_function<AssignCString>(&ScriptString_AssignCString)(message, localized ? localized : title_text(0x00832484));
}

void XB_CDECL GScr_SoundBankUnload(const ScriptString* name) {
    uint32_t hashed[8];
    HashStr_Init(hashed, script_text(name));
    S_UnloadSoundBank(title_global<uint8_t*>(0x0095f4c0), hashed, 1);  // rm_soundBanks
}

void XB_CDECL GScr_SoundBankLoad(const ScriptString* name) {
    uint32_t hashed[8];
    HashStr_Init(hashed, script_text(name));
    S_LoadSoundBank(title_global<uint8_t*>(0x0095f4c0), hashed, 1);
}

void XB_CDECL GScr_AmbientPlay(const ScriptString* name, uint32_t time_bits) {
    S_AmbientPlay(title_global<uint8_t*>(0x0095f4cc), script_text(name), time_bits);
}

void XB_CDECL GScr_MusicPlay(const ScriptString* name) { Music_Play(title_global<uint8_t*>(0x0095f4cc), script_text(name)); }

void XB_CDECL GScr_ObjectiveStringByHash(int32_t index, uint32_t hash) {
    ScriptString text;
    own_function<StringFromCString>(&ScriptString_FromCString)(&text, localized_text(hash));
    G_SetObjectiveString(index, &text, 1);
    string_release(&text);
}

void XB_CDECL GScr_ActorSetOrientMode(uint32_t handle, const ScriptString* mode, float yaw, const float* vec) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (!actor) return;
    const uint32_t hash = Com_HashKeyNoCase(0, script_text(mode));
    field<float>(actor, 0x150) = 0.0f;
    uint8_t* const orient = actor + 0x140;
    const auto is = [hash](uint32_t global) { return hash == title_global<uint32_t>(global); };
    if (is(0x0097a6ac)) {  // face angle
        field<uint32_t>(actor, 0x140) = 1;
        field<float>(actor, 0x144) = short_angle(0.0f);
        const float angle = short_angle(yaw);
        field<float>(actor, 0x148) = angle;
        field<float>(actor, 0x14c) = angle;
    } else if (is(0x0097a6b0)) {  // face current
        field<uint32_t>(actor, 0x140) = 1;
        field<uint32_t>(actor, 0x14c) = field<uint32_t>(actor, 0x154);
    } else if (is(0x0097a6b8)) {  // face direction
        field<uint32_t>(actor, 0x140) = 1;
        Actor_SetOrientFromDir(orient, vec, 0.0f);
    } else if (is(0x0097a6bc)) {  // face enemy
        field<uint32_t>(actor, 0x140) = 3;
    } else if (is(0x0097a6c0)) {  // face enemy or motion
        field<uint32_t>(actor, 0x140) = 4;
    } else if (is(0x0097a6c4)) {  // face enemy with offset
        field<uint32_t>(actor, 0x140) = 3;
        field<float>(actor, 0x150) = yaw;
    } else if (is(0x0097a6c8)) {  // face goal
        field<uint32_t>(actor, 0x140) = 5;
    } else if (is(0x0097a6cc)) {  // face motion
        field<uint32_t>(actor, 0x140) = 2;
    } else if (is(0x0097a6d0)) {  // face point: the direction from the entity's origin
        field<uint32_t>(actor, 0x140) = 1;
        const uint8_t* const self = field<uint8_t*>(actor, 0);
        const float dir[3] = {vec[0] - field<float>(const_cast<uint8_t*>(self), 0x140),
                              vec[1] - field<float>(const_cast<uint8_t*>(self), 0x144),
                              vec[2] - field<float>(const_cast<uint8_t*>(self), 0x148)};
        Actor_SetOrientFromDir(orient, dir, 0.0f);
    } else if (is(0x0097a6b4)) {  // face default
        field<uint32_t>(actor, 0x140) = 0;
    }
}

void XB_CDECL GScr_ActorSetLookAnims(uint32_t handle, uint32_t straight, uint32_t left, uint32_t right) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (!actor) return;
    uint8_t* const self = field<uint8_t*>(actor, 0);
    if (!field<uint8_t*>(self, 0x254))
        field<uint8_t*>(field<uint8_t*>(actor, 0), 0x254) = AnimTree_Create(self, title_global<uint8_t*>(0x00987a90));
    uint8_t* const tree = field<uint8_t*>(field<uint8_t*>(actor, 0), 0x254);
    field<uint32_t>(actor, 0x218) = right;
    field<uint32_t>(actor, 0x210) = straight;
    field<uint32_t>(actor, 0x214) = left;
    AnimTree_ClearGoalWeight_r(tree, left & 0xffff, 0);
    AnimTree_ClearGoalWeight_r(tree, field<uint16_t>(actor, 0x218), 0);
    AnimTree_SetGoalWeight(field<uint16_t>(actor, 0x210), tree, 0x3f800000u, 0, 0x3f800000u, 0, 0, 0);
    field<float>(actor, 0x220) = 0.0f;
    field<float>(actor, 0x224) = 0.0f;
    field<uint32_t>(actor, 0x21c) = title_global<uint32_t>(0x0098540c);  // level_time
    field<uint32_t>(actor, 0x204) = 1;
}

// G_RestorePlayerLoadout by its title address (its C version has one offline mismatch on record).
void XB_CDECL GScr_RestorePlayerLoadout() {
    own_function<void XB_REGS1 XB_CDECL(const uint8_t*)>(&G_RestorePlayerLoadout)(title_global<uint8_t*>(0x0095f4e0));
}

void XB_CDECL GScr_SetFlaggedAnim(uint32_t handle, uint32_t notify, uint32_t anim, uint32_t goal, uint32_t blend,
                                  uint32_t rate, uint32_t restart) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    AnimTree_SetGoalWeight(anim & 0xffff, anim_tree_of(ent), goal, blend, rate, notify, 0, restart & 0xff);
}

void XB_CDECL GScr_SetFlaggedAnimKnobAll(uint32_t handle, uint32_t notify, uint32_t anim, uint32_t stop, uint32_t goal,
                                         uint32_t blend, uint32_t rate, uint32_t restart) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const tree = anim_tree_of(ent);
    if (!tree) return;
    AnimTree_SetKnobAll(anim & 0xffff, tree, stop & 0xffff, goal, blend, rate, notify, 0, restart & 0xff);
}

void XB_CDECL GScr_SetFlaggedAnimKnob(uint32_t handle, uint32_t notify, uint32_t anim, float goal, uint32_t blend,
                                      uint32_t rate, uint32_t restart) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const tree = anim_tree_of(ent);
    // Goals under 0.001 become 0 (NaN stays); the siblings fade out first.
    if (title_global<float>(0x0083eb4c) > goal) goal = 0.0f;
    AnimTree_ClearSiblingGoalWeights(goal, tree, anim & 0xffff, blend);
    uint32_t goal_bits;
    std::memcpy(&goal_bits, &goal, 4);
    AnimTree_SetGoalWeight(anim & 0xffff, tree, goal_bits, blend, rate, notify, 0, restart & 0xff);
}

namespace {

uint8_t* ent_anim_tree(uint8_t* ent) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(G_GetEntAnimTree(ent))); }

uint32_t float_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The model of that name in the zone's maps (rm_models, memory context `zone`).
uint8_t* register_model(const char* name, uint32_t zone) {
    uint32_t model = 0;
    R_RegisterModel(name, title_global<uint32_t>(0x0095f6ac), &model, zone);
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(model));
}

uint32_t level_zone() { return field<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x34); }

// An entity's memory context (+0x220), the level's for -1.
uint32_t entity_zone(uint8_t* ent) {
    const uint32_t zone = field<uint32_t>(ent, 0x220);
    return zone == 0xffffffffu ? level_zone() : zone;
}

// The script_brushmodel / script_model / script_origin check of the mover builtins: a message for other
// classes; script_origin does nothing.
bool is_solid_mover(uint8_t* ent) {
    const uint32_t name = field<uint32_t>(ent, 0x260);
    const uint32_t origin = title_global<uint32_t>(0x0097a78c);
    if (name == title_global<uint32_t>(0x0097a784) || name == title_global<uint32_t>(0x0097a788)) return name != origin;
    if (name != origin) va_message(0x00831e60);  // "entity is not a script_brushmodel, script_model, or script_origin"
    return false;
}

}  // namespace

float XB_CDECL GScr_GetAnimTime(uint32_t handle, uint32_t anim) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return title_global<float>(0x0083eafc);
    // The anim node's info (+0x18 per node, 0x30-byte infos at 0x00965d00): its time +0.
    const uint16_t info = field<uint16_t>(anim_tree_of(ent), 0x18 + (anim & 0xffff) * 2);
    if (!info) return 0.0f;
    return title_global<float>(0x00965d00 + info * 0x30u);
}

void XB_CDECL GScr_SetAnim(uint32_t handle, uint32_t anim, uint32_t goal, uint32_t blend, uint32_t rate,
                           uint32_t restart) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    AnimTree_SetGoalWeight(anim & 0xffff, anim_tree_of(ent), goal, blend, rate, 0, 0, restart & 0xff);
}

void XB_CDECL GScr_SetAnimKnobAll(uint32_t handle, uint32_t anim, uint32_t stop, uint32_t goal, uint32_t blend,
                                  uint32_t rate, uint32_t restart) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    AnimTree_SetKnobAll(anim & 0xffff, anim_tree_of(ent), stop & 0xffff, goal, blend, rate, 0, 0, restart & 0xff);
}

void XB_CDECL GScr_SetAnimKnob(uint32_t handle, uint32_t anim, float goal, uint32_t blend, uint32_t rate,
                               uint32_t restart) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || anim == 0xffffffffu) return;
    uint8_t* const tree = ent_anim_tree(ent);
    if (title_global<float>(0x0083eb4c) > goal) goal = 0.0f;  // under 0.001 (NaN stays)
    AnimTree_ClearSiblingGoalWeights(goal, tree, anim & 0xffff, blend);
    AnimTree_SetGoalWeight(anim & 0xffff, tree, float_bits(goal), blend, rate, 0, 0, restart & 0xff);
}

void XB_CDECL GScr_ClearAnim(uint32_t handle, uint32_t anim, float blend) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const tree = anim_tree_of(ent);
    if (title_global<float>(0x0083eb4c) > blend) blend = 0.0f;
    AnimTree_ClearGoalWeight_r(tree, anim & 0xffff, float_bits(blend));
}

void XB_CDECL GScr_StopAnimScripted(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    // An actor in the scripted-anim state (10) leaves it.
    if (uint8_t* const actor = field<uint8_t*>(ent, 0x240))
        if (field<int32_t>(actor, 0xa4 + field<int32_t>(actor, 0xbc) * 4) == 10) Actor_PopState(actor);
    uint8_t* const scripted = field<uint8_t*>(ent, 0x3d4);
    if (!scripted) return;
    if (field<uint16_t>(scripted, 0x40)) {
        // The scripted anim (+0x40) to goal 1, then 0; entity flag 0x1000000 cleared.
        uint8_t* const tree = ent_anim_tree(ent);
        field<uint32_t>(ent, 0x2a4) &= 0xfeffffffu;
        AnimTree_SetGoalWeight(field<uint16_t>(scripted, 0x40), tree, 0x3f800000u, 0, 0x3f800000u, 0, 0, 0);
        AnimTree_SetGoalWeight(field<uint16_t>(scripted, 0x40), tree, 0, 0, 0x3f800000u, 0, 0, 0);
    }
    Z_Free(scripted);
    field<uint8_t*>(ent, 0x3d4) = nullptr;
}

void XB_CDECL GScr_HudSetShader(const int32_t* hud, const ScriptString* shader, int32_t width, int32_t height) {
    const int32_t index = *hud;
    if (index < 0 || static_cast<uint32_t>(index) >= 0x10) return;
    uint8_t* const elem = hud_elem(index);
    uint8_t* const material = RE_RegisterShader(0, script_text(shader));
    if (width < 0) title_va(0x008323e4, width);    // "width %i < 0"
    if (height < 0) title_va(0x008323d4, height);  // "height %i < 0"
    field<int32_t>(elem, 0x34) = height;
    field<int32_t>(elem, 0x30) = width;
    for (uint32_t offset = 0x3c; offset <= 0x48; offset += 4) field<uint32_t>(elem, offset) = 0;
    field<uint32_t>(elem, 0x5c) = 0;
    field<uint32_t>(elem, 0x60) = 0;
    field<float>(elem, 0x64) = 0.0f;
    field<uint32_t>(elem, 0x68) = 0;
    field<uint32_t>(elem, 0) = 3;  // shader element
    field<uint8_t*>(elem, 0x38) = material;
}

void XB_CDECL GScr_PlayerSetViewModel(uint32_t handle, const ScriptString* model) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !field<uint8_t*>(ent, 0x23c)) return;
    uint8_t* const found = register_model(script_text(model), entity_zone(ent));
    field<uint8_t*>(field<uint8_t*>(ent, 0x23c), 0x98) = found;
}

void XB_CDECL GScr_SwapEntField_dc(uint32_t handle_a, uint32_t handle_b) {
    uint8_t* const a = handle_entity(handle_a);
    uint8_t* const b = handle_entity(handle_b);
    if (!a || !b) return;
    if (entity_zone(a) != entity_zone(b)) return;
    // Swaps the brush models (+0xdc) of two entities of the same memory context, bounds (+0xe0/+0xf0) from
    // each model, radius +0x12c cleared, both relinked.
    const uint32_t model = field<uint32_t>(b, 0xdc);
    field<uint32_t>(b, 0xdc) = field<uint32_t>(a, 0xdc);
    field<uint32_t>(a, 0xdc) = model;
    using CopyBounds = void XB_REGS3 XB_CDECL(uint8_t* maxs, uint32_t model, uint8_t* mins);
    title_function<CopyBounds>(0x002b2790)(a + 0xf0, field<uint32_t>(a, 0xdc), a + 0xe0);  // BrushModel_CopyBounds
    title_function<CopyBounds>(0x002b2790)(b + 0xf0, field<uint32_t>(b, 0xdc), b + 0xe0);
    field<float>(a, 0x12c) = 0.0f;
    field<float>(b, 0x12c) = 0.0f;
    SV_LinkEntity(a);
    SV_LinkEntity(b);
}

void XB_CDECL GScr_LocalToWorldCoords(uint32_t handle, const float* local, float* out) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    float right[3], forward[3], up[3];
    uint32_t args[2] = {word_of(ent + 0x150), word_of(forward)};
    AnglesToAxis(args, right, up);
    // out = origin + forward * x - right * y + up * z (each row re-reads local: out may be local).
    const float zero = 0.0f;
    const auto row = [&](int i) {
        return platform_addss(platform_addss(platform_mulss(local[0], forward[i]), platform_mulss(up[i], local[2])),
                              platform_mulss(zero - right[i], local[1]));
    };
    out[0] = row(0);
    out[1] = row(1);
    out[2] = row(2);
    out[0] = platform_addss(field<float>(ent, 0x140), out[0]);
    out[1] = platform_addss(field<float>(ent, 0x144), out[1]);
    out[2] = platform_addss(field<float>(ent, 0x148), out[2]);
}

void XB_CDECL GScr_HudSetShaderTimer9(const int32_t* hud, float seconds, float period, const ScriptString* shader,
                                      int32_t width, int32_t height) {
    HudElem_SetShaderTimer(*hud, 9, script_text(shader), seconds, period, width, height);
}

void XB_CDECL GScr_HudSetShaderTimer8(const int32_t* hud, float seconds, float period, const ScriptString* shader,
                                      int32_t width, int32_t height) {
    HudElem_SetShaderTimer(*hud, 8, script_text(shader), seconds, period, width, height);
}

uint32_t XB_CDECL GScr_GetModelBoneName(ScriptString* out, const ScriptString* model_name, uint32_t index) {
    using AssignCString = uint32_t XB_THISCALL(ScriptString*, const char*);
    const uint8_t* const model = register_model(script_text(model_name), level_zone());
    const uint32_t count = XModel_GetBoneCount(reinterpret_cast<const XModel*>(model));
    if (index >= count) title_va(0x00832438, count - 1);  // "index out of range (0 - %d)"
    // The bone's name hash in the first LOD (+0x24.. first non-null) -> parts +8 -> {.., count +0x10, bones +0x14
    // of 12 bytes, hash +4}; index 0 when out of range. Its text from the name map 0x0095fe00.
    uint32_t lod = 0;
    while (!field<uint32_t>(const_cast<uint8_t*>(model), 0x24 + lod * 4)) ++lod;
    uint8_t* const parts = field<uint8_t*>(field<uint8_t*>(const_cast<uint8_t*>(model), 0x24 + lod * 4), 8);
    const uint32_t bone = index < field<uint32_t>(parts, 0x10) ? index : 0;
    const int32_t hash = field<int32_t*>(parts, 0x14)[bone * 3 + 1];
    uint32_t node;
    IntHashMap_Find(&node, &hash, &title_global<uint8_t>(0x0095fe00));
    const char* const text = node != title_global<uint32_t>(0x0095fe08)
                                 ? reinterpret_cast<const char*>(static_cast<uintptr_t>(node + 0xc))
                                 : nullptr;
    return own_function<AssignCString>(&ScriptString_AssignCString)(out, text);  // ScriptString_AssignCString
}

uint32_t XB_CDECL GScr_GetModelBoneCount(const ScriptString* model_name) {
    const uint8_t* const model = register_model(script_text(model_name), level_zone());
    return XModel_GetBoneCount(reinterpret_cast<const XModel*>(model));
}

void XB_CDECL GScr_Earthquake(float scale, float duration, const float* origin, float radius) {
    const int32_t ms = static_cast<int32_t>(duration * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24));
    CG_StartShakeCamera(origin, scale, ms, radius);
}

void XB_CDECL GScr_MoverNotSolid(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_solid_mover(ent)) return;
    field<uint32_t>(ent, 0x134) = 0;
    field<uint32_t>(ent, 0x10) |= 2;
    SV_LinkEntity(ent);
}

void XB_CDECL GScr_MoverSolid(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_solid_mover(ent)) return;
    field<uint32_t>(ent, 0x134) = 1;
    field<uint32_t>(ent, 0x10) &= ~2u;
    SV_LinkEntity(ent);
}

namespace {

// The ScriptMover_* builtins' check: a message for classes other than script_brushmodel / script_model /
// script_origin; entities with flag 0x10 (+0x2a4) are left alone too.
bool is_script_mover(uint8_t* ent) {
    const uint32_t name = field<uint32_t>(ent, 0x260);
    if (name != title_global<uint32_t>(0x0097a784) && name != title_global<uint32_t>(0x0097a788) &&
        name != title_global<uint32_t>(0x0097a78c)) {
        va_message(0x00831e60);  // "entity is not a script_brushmodel, script_model, or script_origin"
        return false;
    }
    return !(field<uint8_t>(ent, 0x2a4) & 0x10);
}

// The linkTo builtins' message for entities without flag 0x8000 (+0x2a4), formatted into a local buffer only.
void link_message(uint8_t* ent) {
    if (field<uint32_t>(ent, 0x2a4) & 0x8000) return;
    char buf[0x100];
    const uint8_t type = ent[0];
    const uint32_t name = type < 0x11 ? title_global<uint32_t>(0x008d5090 + type * 4u) : 0x00832838;  // entityTypeNames
    title_sprintf(buf, title_text(0x00832284),
                                                                                 title_text(name));
}

void link_to(uint8_t* ent, uint8_t* parent, const char* tag) {
    if (G_EntLinkTo(parent, ent, tag, 0)) G_CalcTagLinkOffset(ent, 0);
}

}  // namespace

void XB_CDECL GScr_ScriptMover_SetActorBox(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || ent[0] != 7 || !(field<uint32_t>(ent, 0x2a4) & 0x20000)) return;
    // The actor bounds (g_actorMins 0x0098a560 / g_actorMaxs 0x0098a550) on a script mover, contents bit 0x8000.
    field<uint32_t>(ent, 0xd4) &= 0xffffffe7u;
    field<uint32_t>(ent, 0x10) |= 0x10;
    std::memcpy(ent + 0xe0, &title_global<uint8_t>(0x0098a560), 12);
    std::memcpy(ent + 0xf0, &title_global<uint8_t>(0x0098a550), 12);
    field<uint32_t>(ent, 0x134) |= 0x8000;
    SV_LinkEntity(ent);
}

uint32_t XB_CDECL GScr_SetContents(uint32_t handle, uint32_t contents) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    const uint32_t old = field<uint32_t>(ent, 0x134);
    field<uint32_t>(ent, 0x134) = contents;
    SV_LinkEntity(ent);
    return old;
}

void XB_CDECL GScr_Vehicle_PushFromPoint(uint32_t handle, const float* point, float scale, float spin, float speed) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const float speed_scaled = speed * title_global<float>(0x0083ee64);  // 17.6
    const float d2 = field<float>(ent, 0x148) - point[2];
    const float d1 = field<float>(ent, 0x144) - point[1];
    const float d0 = field<float>(ent, 0x140) - point[0];
    const float squared = platform_addss(platform_addss(d2 * d2, d1 * d1), d0 * d0);
    const float length = title_ci_sqrt(squared);
    alignas(16) float dir[4];
    if (!(length == title_global<float>(0x0083eafc))) {
        const float inv = title_global<float>(0x0083eb20) / length;
        dir[0] = platform_mulss(inv, d0);
        dir[1] = platform_mulss(inv, d1);
        dir[2] = platform_mulss(inv, d2);
    } else {
        dir[0] = d0;
        dir[1] = d1;
        dir[2] = d2;
    }
    dir[3] = dir[0];
    Vehicle_ApplyPush(ent, dir, scale, spin, float_bits(speed_scaled));
}

uint8_t XB_CDECL GScr_ActorAdvanceAnimTime(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (!actor) return 0;
    // Both passes by one frame (0x008dda9c ms * 0.001); the second up to the next notetrack (for a non-zero handle).
    if (const uint8_t* const model = field<uint8_t*>(field<uint8_t*>(actor, 0), 0x22c)) {
        const float dt = static_cast<float>(title_global<int32_t>(0x008dda9c)) * title_global<float>(0x0083eb4c);
        AnimTree_AdvanceTimeForModel(0, model, float_bits(dt), 0);
    }
    const uint8_t* const model = field<uint8_t*>(field<uint8_t*>(actor, 0), 0x22c);
    if (!model) return 0;
    const float dt = static_cast<float>(title_global<int32_t>(0x008dda9c)) * title_global<float>(0x0083eb4c);
    return AnimTree_AdvanceTimeForModel(0, model, float_bits(dt), handle != 0);
}

void XB_CDECL GScr_RotateVelocity(uint32_t handle, const float* velocity, uint32_t time, uint32_t accel,
                                  uint32_t decel) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_script_mover(ent)) return;
    alignas(16) float v[4] = {velocity[0], velocity[1], velocity[2], velocity[0]};
    ScriptMover_RotateVelocity(ent, word_of(v), time, accel, decel);
}

void XB_CDECL GScr_RotateTo(uint32_t handle, const float* angles, uint32_t time, uint32_t accel, uint32_t decel) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_script_mover(ent)) return;
    // Each target angle as Q_fmod(target, current) + current.
    alignas(16) float target[4] = {};
    target[0] = platform_addss(Q_fmod(angles[0], field<float>(ent, 0x150)), field<float>(ent, 0x150));
    target[1] = platform_addss(Q_fmod(angles[1], field<float>(ent, 0x154)), field<float>(ent, 0x154));
    target[2] = platform_addss(Q_fmod(angles[2], field<float>(ent, 0x158)), field<float>(ent, 0x158));
    ScriptMover_RotateTo(ent, word_of(target), time, accel, decel);
}

void XB_CDECL GScr_MoverMoveGravity(uint32_t handle, const uint32_t* velocity, float time) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_script_mover(ent)) return;
    ScriptMover_MoveGravity(velocity, ent, time);
}

void XB_CDECL GScr_MoveTo(uint32_t handle, const float* point, uint32_t time, uint32_t accel, uint32_t decel) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_script_mover(ent)) return;
    alignas(16) float v[4] = {point[0], point[1], point[2], point[0]};
    ScriptMover_MoveTo(ent, word_of(v), time, accel, decel);
}

void XB_REGS3 XB_CDECL GScr_RotateAxis(uint32_t, uint32_t, uint32_t handle, int32_t axis, float delta, uint32_t time,
                                       uint32_t accel, uint32_t decel) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_script_mover(ent)) return;
    alignas(16) float target[4] = {field<float>(ent, 0x150), field<float>(ent, 0x154), field<float>(ent, 0x158), 0.0f};
    target[axis] = platform_addss(target[axis], delta);
    ScriptMover_RotateTo(ent, word_of(target), time, accel, decel);
}

void XB_REGS3 XB_CDECL GScr_MoveAxis(uint32_t, uint32_t, uint32_t handle, int32_t axis, float delta, uint32_t time,
                                     uint32_t accel, uint32_t decel) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent || !is_script_mover(ent)) return;
    alignas(16) float target[4] = {field<float>(ent, 0x140), field<float>(ent, 0x144), field<float>(ent, 0x148), 0.0f};
    target[axis] = platform_addss(target[axis], delta);
    ScriptMover_MoveTo(ent, word_of(target), time, accel, decel);
}

void XB_CDECL GScr_LinkToUnchecked(uint32_t handle, uint32_t parent_handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const parent = handle_entity(parent_handle);
    if (!parent) return;
    link_to(ent, parent, title_text(0x0082dfeb));  // ""
}

void XB_CDECL GScr_LinkToTagUnchecked(uint32_t handle, uint32_t parent_handle, const ScriptString* tag) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const parent = handle_entity(parent_handle);
    if (!parent) return;
    link_to(ent, parent, script_text(tag));
}

void XB_CDECL GScr_PlayerLinkTo(uint32_t handle, uint32_t parent_handle, const ScriptString* tag, const uint32_t* offset) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const parent = handle_entity(parent_handle);
    if (!parent) return;
    // The client's link view offset (+0x614).
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x614) = offset[0];
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x618) = offset[1];
    field<uint32_t>(field<uint8_t*>(ent, 0x23c), 0x61c) = offset[2];
    link_to(ent, parent, script_text(tag));
}

void XB_CDECL GScr_LinkTo(uint32_t handle, uint32_t parent_handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const parent = handle_entity(parent_handle);
    if (!parent) return;
    link_message(ent);
    link_to(ent, parent, title_text(0x0082dfeb));
}

void XB_CDECL GScr_LinkToTag(uint32_t handle, uint32_t parent_handle, const ScriptString* tag) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const parent = handle_entity(parent_handle);
    if (!parent) return;
    link_message(ent);
    link_to(ent, parent, script_text(tag));
}

void XB_CDECL GScr_LinkToTagWithOffset(uint32_t handle, uint32_t parent_handle, const ScriptString* tag,
                                       const float* origin, const float* angles, uint32_t flags) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const parent = handle_entity(parent_handle);
    if (!parent) return;
    link_message(ent);
    // The flag byte goes in AL over the origin pointer the original loaded into EAX before.
    const uint32_t eax = (word_of(origin) & 0xffffff00u) | (flags & 0xff);
    if (G_EntLinkToWithOffset(eax, ent, script_text(tag), parent, origin, angles)) return;
    // Not linked: the tag's index is looked up (result unused).
    if (!field<uint8_t*>(parent, 0x22c)) return;
    const uint32_t hash = Com_HashKeyNoCase(0, script_text(tag));
    if (tag->rep) G_EntGetTagIndex(parent, hash);
}

}  // namespace game
