#include "g_scr_builtins.h"

#include <cstring>

#include "cl.h"
#include "g_main.h"
#include "g_utils.h"
#include "qcommon.h"
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

const char* title_text(uint32_t address) { return reinterpret_cast<const char*>(static_cast<uintptr_t>(address)); }

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

}  // namespace game
