#pragma once

// The GScr_ builtins: the engine functions compiled level scripts call through the script import
// table (g_scr_imports.cpp). Plain cdecl; script values come in as raw words (entity handles: index in
// the low 12 bits, generation above; vectors by pointer; floats by value).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x000144d0: G_PhysicsLaunch on the handle's entity with copies of point and dir (16-byte aligned,
// the 4th lane = x), hit location 4.
void XB_CDECL GScr_PhysicsLaunch(uint32_t handle, const float* point, const float* dir, float scale,
                                 uint32_t local_point);
// 0x00014590: G_FreeEntityPhysics on the handle's entity (tail jump), nothing when stale.
void XB_CDECL GScr_PhysicsStop(uint32_t handle);
// 0x00234ad0, 0x00234ae0: empty builtins.
void XB_CDECL GScr_EmptyStub174();
void XB_CDECL GScr_EmptyStub173();
// 0x00236a20: the fog colour r_fogColor (0x008db10c..0x008db114).
void XB_CDECL GScr_SetFogColor(float r, float g, float b);
// 0x00236a70: the fog start (0x0095f698) and end (0x008db104) distances.
void XB_CDECL GScr_SetFogDistances(float start, float end);
// 0x00238350: the fog colour from 0-255 ints (times 1/255, 0x0083eb30).
void XB_CDECL GScr_SetFogColorBytes(int32_t r, int32_t g, int32_t b);
// 0x002557c0 (once, byte 0x0098ad59): the script constants: 132 name hashes (scr_const_*, each text also
// entered in the name map with its script hash), 131 script strings from temporaries and 4 assigned from text.
void XB_CDECL GScr_LoadConsts();
// 0x002b74c0: clears flag 0x4000000 of the handle's entity's +0x2a4.
void XB_CDECL GScr_ClearEntFlag4000000(uint32_t handle);
// 0x002cead0: sets flag 0x4000000 of +0x2a4, then clears node 0 of every model's anim tree (the
// pointer list at +0x22c, count byte +0x2e of it; AnimTree_ClearNode_r).
void XB_CDECL GScr_SetEntFlag4000000(uint32_t handle);
// 0x002f4a50 (name in ESI, out in EDI): the objective state of a name (the script strings at
// 0x00984b08, 0x00984ac4, 0x00984b74, 0x00984b04, 0x00984ae0, 0x00984b48 = 0-5); false and 0 for others.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ObjectiveStateFromString(const ScriptString* name, uint32_t* state);
extern "C" void GScr_ObjectiveStateFromString_entry(void);
// 0x002f4b00: g_freezeControls (0x00927314) from a bool.
void XB_CDECL GScr_SetFreezeControls(uint32_t freeze);
// 0x002f4b10, 0x002f4b60, 0x002f4b70: empty builtins.
void XB_CDECL GScr_EmptyStub448();
// 0x002f4b20, 0x002f4b30: builtins returning 0.
uint32_t XB_CDECL GScr_ReturnZero446();
uint32_t XB_CDECL GScr_ReturnZero445();
void XB_CDECL GScr_EmptyStub561();
void XB_CDECL GScr_EmptyStub560();

// 0x002f4b80, 0x002f4b90, 0x002f4ba0: empty builtins.
void XB_CDECL GScr_EmptyStub559();
void XB_CDECL GScr_EmptyStub399();
void XB_CDECL GScr_EmptyStub397();
// 0x002f4be0, 0x002f4bf0, 0x002f4c00: one float global each (0x008db19c, 0x008db198, 0x008d20e0).
void XB_CDECL GScr_Set8db19c(float value);
void XB_CDECL GScr_Set8db198(float value);
void XB_CDECL GScr_Set8d20e0(float value);
// 0x002f4c20: a stick of the active pad (0x008d50d8; pad 0 when -1) as -1..1 floats (the high bytes
// times 1/127, 0x0083ec60). Both outputs are zeroed first.
void XB_CDECL GScr_GetPadStick(int32_t stick, float* x, float* y);
// 0x002f4ea0: the byte at 0x0095f4b4.
void XB_CDECL GScr_Set95f4b4(uint32_t value);
// 0x002f50e0 .. 0x002f5110: the fog pass settings g_fogPassCount (0x00897960), g_fogPassAlpha
// (0x0089795c), g_fogPassStep (0x00897958), g_fogPassFinalAlpha (0x00897954).
void XB_CDECL GScr_SetFogPassCount(int32_t count);
void XB_CDECL GScr_SetFogPassAlpha(float alpha);
void XB_CDECL GScr_SetFogPassStep(float step);
void XB_CDECL GScr_SetFogPassFinalAlpha(float alpha);
// 0x002f5140: the HUD-hidden byte 0x0095f91f = !visible.
void XB_CDECL GScr_SetHudVisible(uint32_t visible);
// 0x002f5190 .. 0x002f51e0: empty builtins.
void XB_CDECL GScr_EmptyStub221();
void XB_CDECL GScr_EmptyStub220();
void XB_CDECL GScr_EmptyStub223();
void XB_CDECL GScr_EmptyStub222();
void XB_CDECL GScr_EmptyStub200();
void XB_CDECL GScr_EmptyStub199();
// 0x002f51f0: g_missileMaxLifetime (0x00985ffc) = seconds * 1000.
void XB_CDECL GScr_SetMissileMaxLifetime(float seconds);
// 0x002f5210: g_compassMarkersEnabled (0x00986008) from a bool.
void XB_CDECL GScr_SetCompassMarkersEnabled(uint32_t enabled);
// 0x002f5220: the radius-damage flag 0x00986010 from a bool.
void XB_CDECL GScr_SetRadiusDamageFlag(uint32_t enabled);
// 0x002f5230, 0x002f5240: empty builtins.
void XB_CDECL GScr_EmptyStub184();
void XB_CDECL GScr_EmptyStub183();

// 0x002f52b0: the difficulty name (the text table 0x008d20d0 by g_skillLevel 0x00950b3c) into out
// (ScriptString_AssignCString; returns its result).
uint32_t XB_CDECL GScr_GetDifficulty(ScriptString* out);
// 0x002f52d0: level.time (0x0098540c).
int32_t XB_CDECL GScr_GetTime();
// 0x002f52e0, 0x002f52f0, 0x002f5300: builtins returning false.
uint8_t XB_CDECL GScr_ReturnFalse511();
uint8_t XB_CDECL GScr_ReturnFalse510();
uint8_t XB_CDECL GScr_ReturnFalse509();
// 0x002f5310, 0x002f5320, 0x002f5330: empty builtins.
void XB_CDECL GScr_EmptyStub21();
void XB_CDECL GScr_EmptyStub22();
void XB_CDECL GScr_EmptyStub14();
// 0x002f5350: the engine text for `id` (engine interface 0x0095f49c, slot +0x870) copied into buf (at most
// size - 1 characters, _strncpy, then terminated); false when the engine has none.
uint8_t XB_CDECL GScr_CopyStringFromEngine(uint32_t id, char* buf, uint32_t size);
// 0x002f53b0: appends a word to the list at 0x0095fdf0 (capacity u16 0x0095fdf4, count s16 0x0095fdf6) while
// there is room.
void XB_CDECL GScr_PushToList95fdf0(uint32_t value);
// 0x002f53e0, 0x002f53f0: empty builtins.
void XB_CDECL GScr_EmptyStub1();
void XB_CDECL GScr_EmptyStub0();
// The persistent values of the owner at 0x0095f4e0 (12-byte records {key, value, count} at +0x560, count
// +0x568; a vector continues in the records after its first, key 0).
// 0x002f5830: the vector stored under key, else three copies of the float at 0x00922eb8; returns out.
float* XB_CDECL GScr_GetPersistentVector(float* out, uint32_t key);
// 0x002f58d0: the float stored under key, else the float at 0x00922eb8.
float XB_CDECL GScr_GetPersistentFloat(uint32_t key);
// 0x002f5920: the int stored under key, else 0xfefefefe.
uint32_t XB_CDECL GScr_GetPersistentInt(uint32_t key);
// 0x002f5960 / 0x002f59a0 / 0x002f59d0: Persistent_SetValue with 3 / 1 / 1 values.
void XB_CDECL GScr_SetPersistentVector(uint32_t key, const uint32_t* value);
void XB_CDECL GScr_SetPersistentFloat(uint32_t key, uint32_t value);
void XB_CDECL GScr_SetPersistentInt(uint32_t key, uint32_t value);
// 0x002f59f0: the float 0x00964b84 and the word 0x00964b88.
void XB_CDECL GScr_Set964b84(float value, uint32_t word);
// 0x002f5a10: the HUD objectives' (g_hudManager 0x0095f8f0 -> +4) +0x13d4 and +0x13d8.
void XB_CDECL GScr_SuppressObjectiveUpdateMessage(uint32_t a, uint32_t b);
// 0x002f5a30: the HUD objectives' 16-bit +0x13d0 and +0x13d2.
void XB_CDECL GScr_HudObjectives_Set13d0(uint32_t a, uint32_t b);

// 0x002f5a60: a random variant of the spg set named `name` (Com_HashKeyNoCase; Spg_FindRandomVariant over
// rm_spg 0x0095f564): its three string pointers, or three zeros when its first is null.
void XB_CDECL GScr_GetSpgVariant(const char* name, uint32_t* a, uint32_t* b, uint32_t* c);
// 0x002f5c40: value != -1.
uint32_t XB_CDECL GScr_IsNotMinusOne(int32_t value);
// 0x002f5c50: value is neither 0 nor -1.
uint32_t XB_CDECL GScr_IsValidHandle(int32_t value);
// 0x002f5c70 / 0x002f5c90: G_EntList190_Disable / _Enable of scn_zoneScenes (0x0095f4f8) for key.
void XB_CDECL GScr_EntList190_Disable(uint32_t key);
void XB_CDECL GScr_EntList190_Enable(uint32_t key);
// The HUD elements: 0x7c bytes each at 0x0094cc88 (+0 type), named by an index the script holds.
// 0x002f5e40: frees the element (type 0) unless the index is negative.
void XB_CDECL GScr_HudDestroy(const int32_t* hud);
// 0x002f5e60: starts a move over `time` seconds: start time +0x54 = level.time, duration +0x58 = time * 1000
// + 0.5 (truncated), the old x/y (+4, +8) to +0x4c/+0x50. A time <= 0 or > 60 only formats a message (va).
void XB_CDECL GScr_HudMoveOverTime(const int32_t* hud, float time);
// 0x002f5ef0: the same for the size: +0x44 / +0x48, old width/height (+0x30, +0x34) to +0x3c/+0x40, then
// the new width and height.
void XB_CDECL GScr_HudScaleOverTime(const int32_t* hud, float time, uint32_t width, uint32_t height);
// 0x002f5f90: the same for the colour: +0x24 / +0x28, the old colour +0x1c to +0x20.
void XB_CDECL GScr_HudFadeOverTime(const int32_t* hud, float time);
// 0x002f6010: makes the element a value display (type 2): clears its text/timer/scale/move words and sets
// the value +0x64.
void XB_CDECL GScr_HudSetValue(const int32_t* hud, float value);
// 0x002f60f0: a zone's +0x34 = FLT_MAX (0x0082eed0); nothing for null.
void XB_CDECL GScr_ZoneClearField34(uint8_t* zone);
// 0x002f6110: a zone's +0x34 = value and the change counter 0x008d9d38 incremented; nothing for null.
void XB_CDECL GScr_ZoneSetField34(uint8_t* zone, float value);
// 0x002f6130: whether the zone (index +0x28, -1 none) has a stream entry (0x0095f638 +0x38 table) whose +0xdc
// is 0.
uint8_t XB_CDECL GScr_IsZoneLoaded(const uint8_t* zone);
// 0x002f61f0: the frame time (0x008ddaa0).
float XB_CDECL GScr_GetFrameTime();
// 0x002f6200: the music state (0x0095f4cc): +0x14 = value, +0x1c = 2, +0x18 = 0.
void XB_CDECL GScr_SetMusicTrack(float value);

// Vehicle builtins: the handle's entity, its vehicle at +0x248 (nothing for a stale handle or no vehicle). Misuse
// only formats a message (va), as the shipped build's script errors do.
// 0x002f68f0: whether the vehicle's +0x180 is <= 0 (message when the entity's +0x297 is not 2: not player
// controlled).
uint8_t XB_CDECL GScr_Vehicle_Is180Clear(uint32_t handle);
// 0x002f6950: no turret target: +0x1c8/+0x1cc = 0, the target point +0x1d0 and offset +0x1dc zeroed.
void XB_CDECL GScr_Vehicle_ClearTurretTarget(uint32_t handle);
// 0x002f69d0: turret target = an entity (+0x1c8 = 1, +0x1cc = its +0x224) with an offset (+0x1dc; zeros when
// Vec3_IsNonZero says none). Message when the entity's health +0x350 is <= 0.
void XB_CDECL GScr_Vehicle_SetTurretTargetEnt(uint32_t handle, uint32_t target, const uint32_t* offset);
// 0x002f6ac0: turret target = a point (+0x1c8 = 1, +0x1cc = 0, +0x1d0), offset zeroed.
void XB_CDECL GScr_Vehicle_SetTurretTargetVec(uint32_t handle, const uint32_t* point);
// 0x002f6b80 / 0x002f6bc0: the vehicle's +0x190 = 0 / {+0x190 = 1, +0x194 = the bool}.
void XB_CDECL GScr_Vehicle_Clear190(uint32_t handle);
void XB_CDECL GScr_Vehicle_Set190(uint32_t handle, uint32_t value);
// 0x002f6c10 / 0x002f6ca0: clears / sets bit 1 of the entity's +0x2a0 and 0x200000 of its contents +0x134
// (MakeUsable: message when the owner handle +0x1a0 is live).
void XB_CDECL GScr_Vehicle_MakeUsable(uint32_t handle);
void XB_CDECL GScr_Vehicle_MakeUnusable(uint32_t handle);
// 0x002f6cf0 / 0x002f6d30: the vehicle's +0x200 = 0 / 1.
void XB_CDECL GScr_Vehicle_Clear200(uint32_t handle);
void XB_CDECL GScr_Vehicle_Set200(uint32_t handle);
// 0x002f6d70: the entity's owner handle (+0x1a0) when it names a live entity, else 0.
uint32_t XB_CDECL GScr_Vehicle_GetOwnerHandle(uint32_t handle);
// 0x002f6de0: the entity's speed (+0x2fc) times 0x0083ee80 (inches/s to mph) on the x87; 0 when stale.
long double XB_CDECL GScr_Vehicle_GetSpeedMPH(uint32_t handle);
// 0x002f6e20: the surface name under a wheel (scr_const_front_left .. middle_right = 0-5, others 0): the
// vehicle's surface +0x150[wheel], "none" for 0, "default" outside 1..0x16, else the surface table 0x008d4d08
// (16 bytes each). Message when the vehicle def (g_vehicleDefs by +0x178) has no wheels (type +0x20 not 1/2).
void XB_CDECL GScr_Vehicle_GetWheelSurface(uint32_t handle, uint32_t wheel, ScriptString* out);
// 0x002f6f40 / 0x002f6fb0 / 0x002f7040: speeds in mph times 17.6 (0x0083ee64): mode 2 with +0x1b8; mode 1 with
// +0x1b4 and +0x1b8; the max speed +0x17c. Message when the entity's +0x297 is 2.
void XB_CDECL GScr_Vehicle_SetSpeedMode2(uint32_t handle, float speed);
void XB_CDECL GScr_Vehicle_SetSpeed(uint32_t handle, float speed, float accel);
void XB_CDECL GScr_Vehicle_SetMaxSpeed(uint32_t handle, float speed);

// 0x002f70b0: the vehicle's wait node (+0x17a, 16 bits) and +0x2f8 = 0; nothing for node -1 (path message
// as SetSpeed).
void XB_CDECL GScr_Vehicle_SetWaitNode(uint32_t handle, const int32_t* node);
// 0x002f7120: Vehicle_SetSwitchNodes(from, vehicle, to) unless either node is -1.
void XB_CDECL GScr_Vehicle_SetSwitchNode(uint32_t handle, const int32_t* from, const int32_t* to);
// 0x002f71a0: starts the path: the entity's +0x297 = 1 (message when the vehicle's node, 16 bits at +0, is
// negative: not attached).
void XB_CDECL GScr_Vehicle_StartPath(uint32_t handle);
// Weapon slot builtins on a player (the entity's client +0x248 -> +0x23c, nothing / 0 without one). The slot is
// named by a script string ("none", "primary", "primaryb", "pistol", "grenade", "satchel", "special": the table
// 0x008d23a8); "none" and unknown names give slot 0 and a message. The slot's weapon is the byte +0x3f8[slot].
// 0x002f7210: the weapon's clip size (def +0x5a8), 0 for no weapon.
int32_t XB_CDECL GScr_PlayerGetWeaponSlotClipSize(uint32_t handle, const ScriptString* slot);
// 0x002f72d0: the clip ammo (+0x280[def +0x5a0], when the weapon has a clip) = count clamped to 0 below and
// to the clip's max (0x00935338) above.
void XB_CDECL GScr_PlayerSetWeaponSlotClipAmmo(uint32_t handle, const ScriptString* slot, int32_t count);
// 0x002f73c0: the clip ammo, 0 without a weapon or clip.
int32_t XB_CDECL GScr_PlayerGetWeaponSlotClipAmmo(uint32_t handle, const ScriptString* slot);
// 0x002f74a0: the ammo: clip ammo for clip-only weapons (def +0x6ec; max 0x00935338), else the reserve
// +0x110[def +0x598] (max 0x00929818); negative counts store 0.
void XB_CDECL GScr_PlayerSetWeaponSlotAmmo(uint32_t handle, const ScriptString* slot, int32_t count);
// 0x002f75e0: the ammo as SetWeaponSlotAmmo picks it.
int32_t XB_CDECL GScr_PlayerGetWeaponSlotAmmo(uint32_t handle, const ScriptString* slot);
// 0x002f76c0: puts a weapon (named, "none" = empty) in a slot: the slot's old weapon removed
// (BG_RemoveWeaponFromSlot), the new one given (BG_GivePlayerWeapon); a primaryb weapon while primary is empty
// lands in primary and is moved over. Messages for an unknown weapon or one of another slot kind (def +0x9c;
// primary and primaryb mix). Writes the entity into its handle argument slot.
void XB_CDECL GScr_CheckWeaponSlot(uint32_t handle, const ScriptString* slot, const ScriptString* weapon);
// 0x002f78a0: the slot's weapon name (def +8) into out, scr_string_none (0x00984cb0) for none.
void XB_CDECL GScr_PlayerGetWeaponSlotWeapon(uint32_t handle, const ScriptString* slot, ScriptString* out);
// 0x002f7980: the client's +0x630 = !value.
void XB_CDECL GScr_PlayerSetNotFlag_630(uint32_t handle, uint32_t value);
// 0x002f79d0: whether the client's look-at entity (+0x5f4) is the target's entity.
uint8_t XB_CDECL GScr_PlayerIsLookingAt(uint32_t handle, uint32_t target);
// 0x002f7a50: the client's +0x58c from a bool.
void XB_CDECL GScr_PlayerSetInt_58c(uint32_t handle, uint32_t value);
// 0x002f7aa0 .. 0x002f7c20: allow = clear, else set, one flag of the client's pm_flags +0x2c.
void XB_CDECL GScr_PlayerAllowPmFlag800000(uint32_t handle, uint32_t allow);
void XB_CDECL GScr_PlayerAllowPmFlag400000(uint32_t handle, uint32_t allow);
void XB_CDECL GScr_PlayerAllowPmFlag200000(uint32_t handle, uint32_t allow);
void XB_CDECL GScr_PlayerAllowPmFlag100000(uint32_t handle, uint32_t allow);
void XB_CDECL GScr_PlayerAllowPmFlag80000(uint32_t handle, uint32_t allow);
// 0x002f7c80: the same for flag 0x100000 of the entity's +0x2a4 (players only).
void XB_CDECL GScr_PlayerAllowEFlag100000(uint32_t handle, uint32_t allow);

}  // namespace game
