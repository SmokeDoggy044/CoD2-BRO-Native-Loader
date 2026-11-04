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

// 0x002f7ce0 / 0x002f7d30 / 0x002f7d80 / 0x002f7dd0: one bit of a client byte (+0x2c bit 5; +0x594 bits 5, 0,
// 6) as 0/1; 0 without a client.
int32_t XB_CDECL GScr_PlayerGetFlag20_2c(uint32_t handle);
int32_t XB_CDECL GScr_PlayerGetFlag20_594(uint32_t handle);
int32_t XB_CDECL GScr_PlayerGetFlag1_594(uint32_t handle);
int32_t XB_CDECL GScr_PlayerGetFlag40_594(uint32_t handle);
// 0x002f7e20: the client's view angles (+0x9c) into out, else three copies of 0x00922eb8; returns out.
uint32_t* XB_CDECL GScr_PlayerGetAngles(uint32_t* out, uint32_t handle);
// 0x002f7e90: the client's +0x10 vector (the client pointer re-read for each word).
void XB_CDECL GScr_PlayerSetVec_10(uint32_t handle, const uint32_t* value);
// Weapon builtins naming a weapon by script string (BG_FindWeaponIndexForHash of Com_HashKeyNoCase; 0 for the
// empty string); ownership = bit `weapon` of the client's words at +0x3f0.
// 0x002f7ef0: ammo / max ammo (x87 FILD/FIDIV) of an owned weapon: clip ammo (+0x280, max 0x00935338) for
// clip-only weapons (def +0x6ec), else the reserve (+0x110, max 0x00929818); 1 when not owned or max < 1, 0 when
// ammo < 1 or no client.
long double XB_CDECL GScr_PlayerGetWeaponMaxAmmoFraction(uint32_t handle, const ScriptString* weapon);
// 0x002f8000: reserve ammo / the def's max ammo (+0x590), as above.
long double XB_CDECL GScr_GetWeaponAmmoFraction(uint32_t handle, const ScriptString* weapon);
// 0x002f80d0: whether the player owns the weapon.
uint8_t XB_CDECL GScr_PlayerHasWeapon(uint32_t handle, const ScriptString* weapon);
// 0x002f8160: *total = clip + reserve ammo of an owned weapon (0 first); whether owned.
uint8_t XB_CDECL GScr_PlayerGetWeaponTotalAmmo(uint32_t handle, const ScriptString* weapon, int32_t* total);
// 0x002f8230: the current weapon's (+0x80) name (def +8) into out, "none" when <= 0.
void XB_CDECL GScr_PlayerGetCurrentWeapon(uint32_t handle, ScriptString* out);
// 0x002f82a0: current weapon 0, then for every weapon 1..bg_lastWeaponIndex: reserve and clip ammo 0 and
// BG_RemoveWeaponFromSlot.
void XB_CDECL GScr_PlayerTakeAllWeapons(uint32_t handle);
// 0x002f8360: the same for one weapon (tail jump to BG_RemoveWeaponFromSlot).
void XB_CDECL GScr_PlayerTakeWeapon(uint32_t handle, const ScriptString* weapon);

// Actor builtins: the handle's entity's actor (+0x240; some do not check it for null, as the original).
// 0x002f8420: allow clears flag 0x2000000 of the actor's +0x310, else sets it.
void XB_CDECL GScr_ActorSetFlag2000000(uint32_t handle, uint32_t allow);
// 0x002f8480: the position of the actor's current path node (+0x746 indexes the 0x1c-byte path points at
// +0x3bc; PathNode_FromHandleInTable, node +0x44) while it has a path (+0x740 > 0), else its entity's origin.
void XB_CDECL GScr_ActorGetPathNodeOrOrigin(uint32_t handle, uint32_t* out);
// 0x002f8540: new look yaw/pitch targets (+0x208/+0x20c) reached in `time` seconds: the current angles from the
// running blend (end time +0x21c, rates +0x220/+0x224, clamped 0..180), the new end time and rates (9999 for 0).
void XB_CDECL GScr_ActorSetLookRates(uint32_t handle, float yaw, float pitch, float time);
// 0x002f86b0: stops looking (+0x1e8 = 0); a range over 20 is kept in +0x200.
void XB_CDECL GScr_ActorStopLookAt(uint32_t handle, float range);
// 0x002f8710: looks at a point (+0x1ec, +0x1e8 = 1) with a range of at least 20 (+0x200).
void XB_CDECL GScr_ActorLookAt(uint32_t handle, const uint32_t* point, float range);
// 0x002f87a0: empty builtin.
void XB_CDECL GScr_EmptyStub398();
// 0x002f87f0: moves the actor: entity angles +0x150 (and the actor's +0x130.. as short-angle rounded yaw,
// pitch, pitch), the move delta per second (+0x2a0, from the frame msec 0x008dda9c), +0x2c4.. and +0x2b0
// cleared, origin +0x140.
void XB_CDECL GScr_ActorTeleport(uint32_t handle, const uint32_t* origin, const uint32_t* angles);
// 0x002f89a0: the entity handle (+0x224) of the turret the actor uses (+0x136c; Actor_IsUsingTurret), else 0.
uint32_t XB_CDECL GScr_ActorGetTurret(uint32_t handle);
// 0x002f8a40: the anim mode +0x280 from a name hash: nophysics 8 (entity flag 0x10000 of +0x2a4 set; cleared
// for the others), gravity 4, nogravity 6, angle_deltas 3, none 0.
void XB_CDECL GScr_ActorSetAnimMode(uint32_t handle, const ScriptString* mode);
// 0x002f8b20: the traverse mode +0x268 = 1 / 2 / 3 for the script strings 0x00984b60 / 0x00984bb0 /
// 0x00984ba8.
void XB_CDECL GScr_ActorSetTraverseMode(uint32_t handle, const ScriptString* mode);
// 0x002f8bc0: whether one of the 4 sentient slots (0x18 bytes at +0x12a8) has a claimant and no +0x14 word.
int32_t XB_CDECL GScr_ActorHasPendingSentientSlot(uint32_t handle);
// 0x002f8c20: the allowed stances +0x244 (stand 1, crouch 2, prone 4; the first sets, the others add; 7 when
// none).
void XB_CDECL GScr_ActorAllowedStances(uint32_t handle, uint32_t a, uint32_t b, uint32_t c);
// 0x002f8d50: bit 0 of the path flags +0x77c while the actor has a path.
int32_t XB_CDECL GScr_ActorPathHasFlag1(uint32_t handle);
// 0x002f8da0: Path_GetRemainingLength of the actor's path (+0x3c0), 0 without an actor.
float XB_CDECL GScr_GetPathRemainingLength(uint32_t handle);
// 0x002f8e00: whether state 9 (critical section) is on the actor's state stack (+0xa4, top +0xbc).
int32_t XB_CDECL GScr_ActorInCriticalSection(uint32_t handle);
// 0x002f8e70: pops the top state when it is 9, unless 0x0094cbb4 is positive (Actor_PopState).
void XB_CDECL GScr_ActorEndCriticalSection(uint32_t handle);

// 0x002f8ed0: the pitch blend anims: +0x178 = c, +0x17c = d, +0x180 = e, +0x188 = 1 / a, +0x18c = 1 / b.
void XB_CDECL GScr_ActorSetPitchBlendAnims(uint32_t handle, float a, float b, uint32_t c, uint32_t d, uint32_t e);
// 0x002f8f50: Actor_ExitProne over `time` seconds (msec truncated).
void XB_CDECL GScr_ActorExitProne(uint32_t handle, float time);
// 0x002f8fa0: the six aim words +0x160..+0x174.
void XB_CDECL GScr_ActorSetAimParams(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                                     uint32_t f);
// 0x002f9080: the squared distance from the sentient's (+0x244) entity to the nearest enemy sentient (enemy
// team of team +4: 1 -> 2, 2 -> 1, else none = 0) that the sentient's actor knows (+0x868 per sentient, 0x50
// bytes) and that is not flagged (entity +0x2a4 bit 2, sentient +0x30); 1e8 when no enemy exists.
float XB_CDECL GScr_SentientNearestEnemyDist(uint32_t handle);
// 0x002f9260: the float +0xf4 of the entity's +0x250 record.
void XB_CDECL GScr_SetEnt250Field_f4(uint32_t handle, float value);
// 0x002f92b0: sets / clears flag 0x20000000 of +0x2a4, then the 16-bit +0xf8 of the +0x250 record.
void XB_CDECL GScr_SetEntFlag20000000(uint32_t handle, uint32_t set, uint32_t value);
// HUD components of g_hudManager (0x0095f8f0): +0x1c, +0x20, +0x24 (the meter).
// 0x002f9320 .. 0x002f9380: component +0x1c: mode +0x44 (-1 off with +0x54 = 0; else 2 / 1 / 0 with 1).
void XB_CDECL GScr_HudComp1c_Off();
void XB_CDECL GScr_HudComp1c_SetMode2();
void XB_CDECL GScr_HudComp1c_SetMode1();
void XB_CDECL GScr_HudComp1c_SetMode0();
// 0x002f93a0 .. 0x002f93e0: component +0x20: enabled byte +0x14 (EnableWith also +0x18 = value), +0x10.
void XB_CDECL GScr_HudComp20_EnableWith(uint32_t value);
void XB_CDECL GScr_HudComp20_Disable();
void XB_CDECL GScr_HudComp20_Enable();
void XB_CDECL GScr_HudComp20_Set10(uint32_t value);
// 0x002f93f0: component +0x24: both list counts (+0xe4, +0xe8) = 0.
void XB_CDECL GScr_HudComp24_Clear();
// 0x002f9410: appends a value: 0 and 1 to the first list (count +0xe4, words +0xa4, max 3), others to the
// second (count +0xe8, words +0xb0, max 12); the count grows even when full.
void XB_CDECL GScr_HudComp24_Push(uint32_t value);
// 0x002f9460: component +0x24's +0xec = 2.
void XB_CDECL GScr_HudComp24_SetEcTwo();
// 0x002f9480: component +0x24's byte +0xe0 = value, +0xec = 0.
void XB_CDECL GScr_HudComp24_SetE0(uint32_t value);
// 0x002f94a0: HudCompMeter_SetCount on component +0x24.
void XB_CDECL GScr_HudSetMeterCount(uint32_t count);
// 0x002f94c0: whether the entity has a model (+0x22c) not hidden (bit 0 of its +0x9c).
uint32_t XB_CDECL GScr_HasVisibleModel(uint32_t handle);
// 0x002f9510: sets flag 0x2000000 of the entity's +0x2a4.
void XB_CDECL GScr_SetEntFlag2000000(uint32_t handle);

// 0x002f9550: DObj_SetForcedLod on the entity's model (+0x22c), if any (tail jump).
void XB_CDECL GScr_SetForcedLod(uint32_t handle, int32_t lod);
// Turret builtins: the turret record at the entity's +0x24c (nothing without one).
// 0x002f9710, 0x002f9760: the float +0x40 / +0x3c.
void XB_CDECL GScr_Turret_Set40(uint32_t handle, float value);
void XB_CDECL GScr_Turret_Set3c(uint32_t handle, float value);
// 0x002f97b0, 0x002f9850: the arc +0x34 / +0x38 (stored, then 0 when negative).
void XB_CDECL GScr_Turret_SetArc_34(uint32_t handle, float value);
void XB_CDECL GScr_Turret_SetArc_38(uint32_t handle, float value);
// 0x002f9800: the arc +0x2c = 0 - value.
void XB_CDECL GScr_Turret_SetArc_2c(uint32_t handle, float value);
// 0x002f98a0: the arc +0x30 = 0 - value, then 0 when positive.
void XB_CDECL GScr_Turret_SetArc_30(uint32_t handle, float value);
// 0x002f9900: a spawner's (type byte 0xc) team +0x3b4 from "axis" / "allies" / "neutral" (1-3, _stricmp);
// others format a message (va, handed the string's rep pointer) and leave it.
void XB_CDECL GScr_SetSpawnerTeam(uint32_t handle, const ScriptString* team);
// 0x002f99d0 .. 0x002f9a90: set / clear flag 0x80000 / 0x40000 of the entity's +0x2a4.
void XB_CDECL GScr_SetEntFlag80000(uint32_t handle);
void XB_CDECL GScr_ClearEntFlag80000(uint32_t handle);
void XB_CDECL GScr_ClearEntFlag40000(uint32_t handle);
void XB_CDECL GScr_SetEntFlag40000(uint32_t handle);
// 0x002f9ad0: the entity's number (+0x224), 0 when stale.
int32_t XB_CDECL GScr_GetEntityNumber(uint32_t handle);
// 0x002f9b10: a gravity trajectory: type +0x14 = 5, start time +0x18 = level_time, velocity +0x2c..+0x34,
// byte +0x294 = 1.
void XB_CDECL GScr_MoveGravity(uint32_t handle, const uint32_t* velocity);
// 0x002f9b70, 0x002f9bb0: clear / set flag 0x8000 of the entity's +0x10.
void XB_CDECL GScr_ClearEFlag8000(uint32_t handle);
void XB_CDECL GScr_SetEFlag8000(uint32_t handle);
// 0x002f9bf0: the client's (+0x23c) kick +0x5d4 = (health +0x354 * damage + 50) / 100 (message when negative),
// kick origin +0x5d8..+0x5e0 = client origin (+0) - point.
void XB_CDECL GScr_ViewKick(uint32_t handle, int32_t damage, const float* point);
// 0x002f9cd0: zeroes the client's (+0x23c, not checked) +0x4f4..+0x4fc.
void XB_CDECL GScr_ClearClientVec4f4(uint32_t handle);
// 0x002f9d30: whether the turret's byte +1 is set; message (the classname +0x25c) and false without a turret.
uint8_t XB_CDECL GScr_Turret_GetFlag1(uint32_t handle);
// 0x002f9d90: the client's stance (+0x2c bit 0 "prone", bit 1 "crouch", else "stand") into out.
void XB_CDECL GScr_GetStance(uint32_t handle, ScriptString* out);
// 0x002f9e00: XAnim_HasNotetrack(anim, name); the first argument is not read.
uint8_t XB_CDECL GScr_AnimHasNotetrack(uint32_t unused, uint32_t anim, const uint32_t* name);
// 0x002f9e20: the hint string +0x28c; a trigger_use (+0x260) gets byte +5 = 0xff, others the actor's
// (+0x240) +0x1380 = 0xffff.
void XB_CDECL GScr_SetHintString28c(uint32_t handle, uint32_t hint);

// 0x002f9e80: with a hint (*hint != 0) on a trigger_use or an entity with an actor (+0x240): resets the hint
// cache (trigger_use: byte +5 = 0xff; else the actor's +0x1380 = 0xffff).
void XB_CDECL GScr_ResetHintCache(uint32_t handle, const uint32_t* hint);
// 0x002f9ef0: the cursor hint +0xc0: -1 for "HINT_INHERIT" on a trigger_use, else the index (1..0x10b) of the
// name in the hint type table 0x008dae44 (Q_stricmpn2); unknown names format a message (the rep pointer).
void XB_CDECL GScr_SetCursorHint(uint32_t handle, const ScriptString* hint);
// Turret builtins with the "entity type '%s' is not a turret" message (va, the classname +0x25c) when the
// entity has no turret record (+0x24c).
// 0x002f9ff0: the number (+0x224) of the turret's owner (+0xc) while flag 0x40 of its +2 is set, else 0.
int32_t XB_CDECL GScr_Turret_GetOwner(uint32_t handle);
// 0x002fa070: sqrt of the squared range +0x48 (x87, ST0), 0 without a turret.
long double XB_CDECL GScr_Turret_GetMaxRange(uint32_t handle);
// 0x002fa0e0: the squared range +0x48 = range * range.
void XB_CDECL GScr_Turret_SetMaxRange(uint32_t handle, float range);
// 0x002fa160: the float +0x54.
void XB_CDECL GScr_Turret_Set54(uint32_t handle, float value);
// 0x002fa1e0, 0x002fa250: clear / set the usable flag 0x10 of byte +3.
void XB_CDECL GScr_Turret_MakeUnusable(uint32_t handle);
void XB_CDECL GScr_Turret_MakeUsable(uint32_t handle);
// 0x002fa2c0: the team +0x64 from "axis" (1) / "allies" (2) (_stricmp); others format a message (rep pointer).
void XB_CDECL GScr_Turret_SetTeam(uint32_t handle, const ScriptString* team);
// 0x002fa3b0, 0x002fa420: the target entity +8 (null / the second handle's entity; nothing when it is stale).
void XB_CDECL GScr_Turret_ClearTargetEntity(uint32_t handle);
void XB_CDECL GScr_Turret_SetTargetEntity(uint32_t handle, uint32_t target_handle);
// 0x002fa4c0: the entity's +0x1a0 (owner handle), 0 when stale.
int32_t XB_CDECL GScr_GetOwnerHandle(uint32_t handle);
// 0x002fa500: the turret's user: the entity's +0x1a0 while its byte +0x297 is set, else 0.
int32_t XB_CDECL GScr_Turret_GetUser(uint32_t handle);
// 0x002fa580: the mode bits 0-1 of the turret's 16-bit +2: auto_ai 3, manual 0, manual_ai 1, auto_nonai 2.
void XB_CDECL GScr_Turret_SetMode(uint32_t handle, uint32_t mode);
// 0x002fa650, 0x002fa6c0: clear / set flag 4 of the turret's byte +2.
void XB_CDECL GScr_Turret_ClearFlag4(uint32_t handle);
void XB_CDECL GScr_Turret_SetFlag4(uint32_t handle);
// 0x002fa730, 0x002fa770: set / clear flag 0x400 of the entity's +0x2a4.
void XB_CDECL GScr_SetEntFlag400(uint32_t handle);
void XB_CDECL GScr_ClearEntFlag400(uint32_t handle);
// 0x002fa7b0: G_SetEntityInList960070(in_list != 0) on the entity (tail jump).
void XB_CDECL GScr_SetEntInList960070(uint32_t handle, uint32_t in_list);
// 0x002fa7f0: the take-damage word +0x29c = the argument's low byte.
void XB_CDECL GScr_SetTakeDamage(uint32_t handle, uint32_t take_damage);

// 0x002fa830: health +0x350 = max(1, trunc(max health * fraction + 0.5)), the max from the client's +0x580 or
// the entity's +0x354; fraction above 1 becomes 1, <= 0 formats a message and goes on; no max formats another
// and stops.
void XB_CDECL GScr_SetNormalHealth(uint32_t handle, float fraction);
// 0x002fa960: health / max health on the x87 (FILD/FIDIV, ST0): the client's max (+0x580) while health is
// non-zero, else the entity's +0x354; 0 without a max.
long double XB_CDECL GScr_GetNormalHealth(uint32_t handle);
// 0x002fa9d0: a func_door / func_door_rotating with its +0x3b4 (locked) set.
uint8_t XB_CDECL GScr_DoorIsLocked(uint32_t handle);
// 0x002faa30: the entity's origin (+0x140), or for brush models (+0xdc) the centre of +0x100 / +0x110.
void XB_CDECL GScr_GetOrigin(uint32_t handle, uint32_t* out);
// 0x002fab10: enables linking: +0x328 = level_time, +0x32c = 0x1c, flag 0x8000 of +0x2a4. Unsupported
// entities (type byte or +0x294 set, or a link time / content set on other than trigger_multiple) format a
// message into a local buffer (_sprintf) first.
void XB_CDECL GScr_EnableLinkTo(uint32_t handle);
// 0x002fabf0: G_EntUnlink on the entity (tail jump).
void XB_CDECL GScr_Unlink(uint32_t handle);
// 0x002fac30: bit `bit` of the entity's byte +0x299 (AL; false when stale).
uint8_t XB_CDECL GScr_TestEntBit299(uint32_t handle, uint32_t bit);
// 0x002fac80, 0x002facd0: attachment `index` (+0x3d8 + index * 8: {model, tag}): the tag string / the
// model's name (+0x48) into out.
void XB_CDECL GScr_GetAttachTagName(uint32_t handle, ScriptString* out, int32_t index);
void XB_CDECL GScr_GetAttachModelName(uint32_t handle, ScriptString* out, int32_t index);
// 0x002fad20: the number of attachments (models before the first null, at most 7).
int32_t XB_CDECL GScr_GetAttachSize(uint32_t handle);
// 0x002fad70, 0x002fadb0, 0x002fadf0: whether the entity has a sentient (+0x244) / actor (+0x240) / client
// (+0x23c).
int32_t XB_CDECL GScr_IsSentient(uint32_t handle);
int32_t XB_CDECL GScr_IsAI(uint32_t handle);
int32_t XB_CDECL GScr_IsPlayer(uint32_t handle);
// 0x002fae30: an actor's +0x2d0 set, or else health +0x350 > 0.
int32_t XB_CDECL GScr_IsAlive(uint32_t handle);
// 0x002faed0, 0x002faef0: FX_SetEffectParamById / FX_StopEffectById on the list at 0x0095f5b4.
void XB_CDECL GScr_SetSoundParam(uint32_t id, uint32_t value);
void XB_CDECL GScr_StopSound(uint32_t id);
// 0x002faf10: VehicleNode_FindByKey(key, value, out), all nodes when either string is empty.
int32_t XB_CDECL GScr_GetVehicleNode(const ScriptString* value, const ScriptString* key, int32_t* out);
// 0x002faf40: PathNode_FindByKey over g_pathNodeBlocks with copies of key and value (ScriptString_FromCString
// of their texts, released after).
int32_t XB_CDECL GScr_GetNode(const ScriptString* value, const ScriptString* key, uint32_t* out);
// 0x002fafb0: the number (+0x224) of entity `number` (below 0x540, generation 0), 0 otherwise.
int32_t XB_CDECL GScr_GetEntByNum(uint32_t number);
// 0x002faf00: an empty builtin (a bare ret).
void XB_CDECL GScr_EmptyStub38();

// 0x002fb230: the entity's script object +0x428, created on first use by the callback at +0x804 of the object
// at 0x0095f49c (called with 0, 0) when there is one; 0 when stale.
uint32_t XB_CDECL GScr_GetEntScriptObject(uint32_t handle);
// 0x002fb290, 0x002fb2a0: the number (+0x224) of level entity +4 (the player) / +8 of the record at 0x0095f534.
int32_t XB_CDECL GScr_GetPlayerHandle();
int32_t XB_CDECL GScr_GetLevelEnt8Handle();
// 0x002fb2b0 (ECX = out, cdecl handle): "none" for handle 0, else "obstacle" when flag 0x4000 of the entity's
// +0x2a4 is set, "world" otherwise (a stale handle reads entity null).
void XB_REGS3 XB_CDECL GScr_ReturnObstacleKind(uint32_t unused_eax, uint32_t unused_edx, ScriptString* out,
                                               uint32_t handle);
// 0x002fb4e0: HudElem_SetDefaults on HUD element *index (tail jump).
void XB_CDECL GScr_HudClear(const int32_t* index);
// 0x002fb5a0: MemContext_FindById on the context at 0x0095f638 with the entity's zone (+0x220), or the
// context's own id (+0x34) for stale entities and zone -1.
uint8_t* XB_CDECL GScr_GetEntZone(uint32_t handle);
// 0x002fb600: *out = HudElem_Alloc(), or 0x008dda38 when none is free. Returns out.
int32_t* XB_CDECL GScr_GetCurrentZoneId(int32_t* out);
// 0x002fb620: the weapon's type name (0x008d237c[def +0x94]) into out, "none" for unknown weapons.
void XB_CDECL GScr_WeaponType(ScriptString* out, const ScriptString* weapon_name);
// 0x002fb680, 0x002fb6c0: the weapon def's +0x6d8 / +0x6d4, 0 for unknown weapons.
int32_t XB_CDECL GScr_WeaponGet_6d8(const ScriptString* weapon_name);
int32_t XB_CDECL GScr_WeaponGet_6d4(const ScriptString* weapon_name);
// 0x002fb700: the table value 0x00935338[def +0x5a0], 0 for unknown weapons.
int32_t XB_CDECL GScr_WeaponGetTableValue_5a0(const ScriptString* weapon_name);
// 0x002fb750: def +0x5dc (msec) * 0.001 on the x87 (ST0), 0 for unknown weapons.
long double XB_CDECL GScr_WeaponGetFloat_5dc(const ScriptString* weapon_name);
// 0x002fb7a0: unless a change is pending (0x0095bf9c): requests the level change (0x00985eb0 = 1, map name into
// 0x00985ecc, 0x00985eb8 = 1).
void XB_CDECL GScr_ChangeLevel(const ScriptString* name);
// 0x002fb7f0: as ChangeLevel, with the delay 0x00985ec8 = delay * 1000 + 0.5 (msec), overridden by the float at
// 0x0095c028 when that is >= 0.
void XB_CDECL GScr_ChangeLevelWithDelay(const ScriptString* name, float delay);
// 0x002fb890: while the player lives and no change is pending: the mission-success level change (delay 0 or
// 0x0095c028, 0x00985eb4 = 1 for "gamefinished", 0x00985ebc = 0).
void XB_CDECL GScr_MissionSuccess(const ScriptString* name);
// 0x002fb970, 0x002fba20 (identical): the weapon's Radiant name (+0x578), following the alternate weapons
// (+0x718) while it is empty; "" and a message otherwise. The weapon is found by its name id (+4). Returns out.
ScriptString* XB_CDECL GScr_GetWeaponClassname2(ScriptString* out, uint32_t id);
ScriptString* XB_CDECL GScr_GetWeaponClassname(ScriptString* out, uint32_t id);
// 0x002fbad0: the weapon's display name (+0x57c) into out, "" for unknown ids. Returns out.
ScriptString* XB_CDECL GScr_GetWeaponDisplayName(ScriptString* out, uint32_t id);
// 0x002fbb30: Com_HashKeyNoCase of the weapon's +0x57c (tail jump), 0 and a message for unknown ids.
uint32_t XB_CDECL GScr_GetWeaponModel(uint32_t id);
// 0x002fbec0: whether the entity's notify list (+0x230) has a listener for `name` (0x0029acb0, which also
// calls it with arg); false without a list.
uint8_t XB_CDECL GScr_EntHasNotifyListener(uint32_t handle, uint32_t name, uint32_t arg);

// 0x002fbf20: DebugDraw_AddLine(end, start, {color, alpha}, width, 0 frames, grouped, no fade).
void XB_CDECL GScr_DebugLine(const float* start, const float* end, const uint32_t* color, uint32_t alpha,
                             uint32_t width);
// 0x002fbf80: DebugDraw_AddString({color, alpha}, origin, scale, text, grouped).
void XB_CDECL GScr_Print3D(const float* origin, const ScriptString* text, const uint32_t* color, uint32_t alpha,
                           float scale);
// Objective builtins: objective `index` (0-15) is config string index + 16, an info string.
// 0x002fc710: toggles the objective's "ring" key ("1" when absent or 0, else "0"); message for bad indexes.
// (Proposed name: GScr_ObjectiveToggleRing.)
void XB_CDECL GScr_ValidateObjectiveIndex(int32_t index);
// 0x002fc7f0: the objective's "org" key = "%i %i %i" of the truncated origin; nothing for bad indexes.
void XB_CDECL GScr_ObjectivePosition(int32_t index, const float* origin);
// 0x002fc8a0: empties the objective's config string; message for bad indexes.
void XB_CDECL GScr_ObjectiveDelete(int32_t index);
// 0x002fc9d0: sends `reverb "%s" %g %g` to the handle's client (the name goes in as its rep pointer).
void XB_CDECL GScr_SetReverb(uint32_t handle, const ScriptString* name, float a, float b);
// 0x002fca10: toggles the player's noclip (client +0x584; byte 0x0095f679 = on) and prints GAME_NOCLIPON/OFF.
void XB_CDECL GScr_ToggleNoclip();
// 0x002fca80: SV_GameSendServerCommand("popupclose", handle) (tail jump). (Proposed name: GScr_ClosePopup.)
void XB_CDECL GScr_SendServerCommand(uint32_t handle);
// 0x002fca90: whether the client's +0x60 is set (0 without a client).
int32_t XB_CDECL GScr_PlayerGetBool_60(uint32_t handle);
// 0x002fcae0, 0x002fcb20: SetClientViewAngle / TeleportPlayer on players (tail jumps).
void XB_CDECL GScr_PlayerSetAngles(uint32_t handle, const float* angles);
void XB_CDECL GScr_PlayerSetOrigin(uint32_t handle, const float* origin);
// 0x002fcb60, 0x002fcc20: an owned weapon's ammo type (+0x598) filled up (Add_Ammo, no clip) to the ammo type's
// maximum (0x00929818) / the weapon's max ammo (+0x590).
void XB_CDECL GScr_PlayerGiveMaxAmmoByAmmoType(uint32_t handle, const ScriptString* weapon_name);
void XB_CDECL GScr_PlayerGiveMaxAmmo(uint32_t handle, const ScriptString* weapon_name);
// 0x002fcce0: BG_GivePlayerWeapon, then Add_Ammo(count), filling the clip for a weapon not owned before.
void XB_CDECL GScr_PlayerGiveWeaponWithAmmo(uint32_t handle, const ScriptString* weapon_name, int32_t count);
// 0x002fcda0: BG_GivePlayerWeapon, then the ammo up to max ammo (+0x590), clip filled when new.
void XB_CDECL GScr_PlayerGiveWeapon(uint32_t handle, const ScriptString* weapon_name);
// 0x002fce80, 0x002fced0: GScr_ReturnObstacleKind of the actor's +0x2b0 / +0x39c entity handle.
void XB_CDECL GScr_ActorGetObstacleKind_2b0(uint32_t handle, ScriptString* out);
void XB_CDECL GScr_ActorGetObstacleKind_39c(uint32_t handle, ScriptString* out);
// 0x002fcf20: Actor_Exposed_StartReacquire(1) for an actor in state 2 with +0x28 == 0xc8.
void XB_CDECL GScr_ActorStartReacquire(uint32_t handle);
// 0x002fcf80: whether the stance (stand/crouch/prone; others: message, false) is allowed: not barred by the
// actor's limits (+4 record +0x46) and in the actor's stances (+0x244) narrowed by the path's last node or the
// claimed node (+4 record +0x48) when within 2 units horizontally and 80 vertically.
uint8_t XB_CDECL GScr_ActorIsStanceAllowed(uint32_t handle, uint32_t stance);
// 0x002fd100: unless 0x0094cbb4 > 0: pushes the critical section state (9; message when already in one) and
// names it (+0xc8) after name, or the int hash map text of the actor record's (+0x250) +4 for none.
void XB_CDECL GScr_ActorBeginCriticalSection(uint32_t handle, const ScriptString* name);

struct ScrStringArray;
// 0x002fd1e0: a shake/rumble descriptor (ShakeRumbleDesc_Init): the shake channel (+0) from shake_a and its
// strength when > 0, the rumble channel (+0x20) unless its time is below 0.01 (strength clamped to 0..1);
// started on shakeRumbleMgr, names released.
void XB_CDECL GScr_ShakeRumble(uint32_t rumble_a, float rumble_strength, float rumble_time, uint32_t rumble_b,
                               uint32_t rumble_c, uint32_t shake_a, float shake_strength);
// 0x002fd2d0: both channels by name when strength > 0 (shake: name, time, strength 1; rumble: name, strength).
void XB_CDECL GScr_ShakeRumbleNamed(const ScriptString* rumble, float strength, const ScriptString* shake,
                                    uint32_t shake_time);
// 0x002fd3a0: HudManager_SetComponentColor on the HUD manager's component +0x1c.
void XB_CDECL GScr_HudSetComponentColor(int32_t component, int32_t color);
// 0x002fd420: the sentient's (+0x244) goal node +0x4a; for non-players also Sentient_SetGoalNode.
void XB_CDECL GScr_SetGoalNode(uint32_t handle, const uint16_t* node);
// 0x002fd490, 0x002fd4d0: the render record's (CG_GetEntityRefEntity) byte +0xee = value / +0xef = value != 0.
void XB_CDECL GScr_SetRefEntByte_ee(uint32_t handle, uint32_t value);
void XB_CDECL GScr_SetRefEntFlag_ef(uint32_t handle, uint32_t value);
// 0x002fd520: notifies "death" (Scr_Notify), then G_FreeEntity.
void XB_CDECL GScr_Delete(uint32_t handle);
// 0x002fd570: the callback at +0x86c of the object at 0x0095f49c on the script_animtree and script_name cvars
// (copied to script strings), 0 when either is empty.
uint32_t XB_CDECL GScr_CheckScriptAnimTreeCvars();
// 0x002fd630: FX_IsEffectActive on the list at 0x0095f5b4.
uint8_t XB_CDECL GScr_IsSoundPlaying(uint32_t id);
// 0x002fd740: the localized string (rm_locStrings entry +8, else +0xc) for the hash into out. Returns out.
ScriptString* XB_CDECL GScr_GetLocalizedString(ScriptString* out, uint32_t hash);
// 0x002fd770, 0x002fd790, 0x002fd7b0, 0x002fd820: HUD element *index becomes a timer of kind 7 / 6 / 5 / 4
// (HudElem_SetTimerKind; inlined for 5).
void XB_CDECL GScr_HudSetTimer7(const int32_t* index, float seconds);
void XB_CDECL GScr_HudSetTimer6(const int32_t* index, float seconds);
void XB_CDECL GScr_HudSetTimer(const int32_t* index, float seconds);
void XB_CDECL GScr_HudSetTimer4(const int32_t* index, float seconds);
// 0x002fd840: the zone of that name in the zone set (+0x18 of 0x0095f638: StringMap_Find over +8, index
// checked against +0x10, values +0x14), 0 if none.
uint32_t XB_CDECL GScr_FindZone(const ScriptString* name);
// 0x002fd8a0: config string 9 = "%g" of value.
void XB_CDECL GScr_SetConfigstring9Float(float value);
// 0x002fd8c0: the two keys bound to the command (VM_Call(cgvm, 0xc); "KEY_UNBOUND" / "" without cgame)
// appended to out; returns the VM_Call result (0 without cgame).
int32_t XB_CDECL GScr_GetKeyBinding(ScrStringArray* out, const ScriptString* command);
// 0x002fd9b0: out (DynArray) = the entity numbers of the actors of the team (all teams 0xe for an empty name).
void XB_CDECL GScr_GetAI(uint8_t* out, const ScriptString* team);
// 0x002fda60, 0x002fda90: the cvar's float (+0x1c, ST0) / int (+0x20), 0 for unknown cvars.
float XB_CDECL GScr_GetCvarFloat(const char* name);
int32_t XB_CDECL GScr_GetCvarInt(const char* name);

// 0x002fdab0: out = the cvar's string (first 0x7f characters), "" for unknown cvars. Returns
// ScriptString_AssignCString's result.
uint32_t XB_CDECL GScr_GetCvar(ScriptString* out, const char* name);
// 0x002fdb20, 0x002fdb70: removes / adds the (key, callback) pair of the entity's notify callback list (+0x234;
// SetEntVar creates it from the small heap 0x0095f568 first). AL = done.
uint32_t XB_CDECL GScr_GetEntVar(uint32_t handle, uint32_t key, uint32_t callback);
uint32_t XB_CDECL GScr_SetEntVar(uint32_t handle, uint32_t key, uint32_t callback);
// 0x002fe250: objective index becomes the current one ("state" 4; a previous current one gets 1); the "active"
// update (G_SendObjectiveUpdate with 0x00984ae0).
void XB_CDECL GScr_ObjectiveCurrent(int32_t index);
// 0x002fe5e0: the objective's "state" key from the state name; G_SendObjectiveUpdate.
void XB_CDECL GScr_ObjectiveState(int32_t index, const ScriptString* name);
// 0x002fe6f0: objective configstring (index + 0x10): "state" from the name, "str" = text (checked; disallowed
// control/high characters become spaces in the string itself), "org" = "%i %i %i" of origin; G_SendObjectiveUpdate.
void XB_CDECL GScr_ObjectiveAddWithPosition(int32_t index, const ScriptString* state_name, const ScriptString* text,
                                            const float* origin);
// 0x002feaf0, 0x002feb50: the same with the localized string of a hash / the "%d" of a number as the text.
void XB_CDECL GScr_ObjectiveAddLocalizedWithPosition(int32_t index, const ScriptString* state, uint32_t hash,
                                                     const float* origin);
void XB_CDECL GScr_ObjectiveAddIntTextWithPosition(int32_t index, const ScriptString* state, int32_t number,
                                                   const float* origin);
// 0x002fec10: objective configstring: "state" from the name, "str" = text (checked; none for the empty string);
// G_SendObjectiveUpdate.
void XB_CDECL GScr_ObjectiveAdd(int32_t index, const ScriptString* state_name, const ScriptString* text);
// 0x002fee50, 0x002feea0, 0x002fef50: GScr_ObjectiveAdd with a localized string / "%d" / no text.
void XB_CDECL GScr_ObjectiveAddLocalized(int32_t index, const ScriptString* state, uint32_t hash);
void XB_CDECL GScr_ObjectiveAddIntText(int32_t index, const ScriptString* state, int32_t number);
void XB_CDECL GScr_ObjectiveAddNoText(int32_t index, const ScriptString* state);
// 0x002fef80 (hash on the stack; EBX/EDI handed on): HudText_SetStringByHash on the HUD manager (ESI) with the hash.
// The C version gets the entry ESP.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_HudSetTextByHash(uint32_t entry_esp, uint32_t ebx, uint32_t edi);
extern "C" void GScr_HudSetTextByHash_entry(void);
// 0x002fefa0: HUD manager mode +0x5c; a change runs layout default / hide all / 2 / 3 for modes 0-3.
void XB_CDECL GScr_SetHudMode(uint32_t mode);
// 0x002feff0: the anim's clip length (+0x38; AnimData_EnsureLoaded first), 1 without a clip (ST0).
float XB_CDECL GScr_GetAnimLength(uint32_t unused, uint32_t anim);
// 0x002ff060: a hint-string index (G_FindHintStringIndex of a never-filled buffer) into a trigger_use's +5 or an
// actor's +0x1380.
void XB_CDECL GScr_SetHintString(uint32_t handle);
// 0x002ff120: Path_ConnectPathsForEntity for entities with flag 0x1000 (+0x2a4), else a message.
void XB_CDECL GScr_ConnectPaths(uint32_t handle);
// 0x002ff1a0: G_Damage(entity, damage, mod 0x1b, hit location 2) from the point, direction towards the client /
// entity origin.
void XB_CDECL GScr_DoDamage(uint32_t handle, float damage, const float* point);
// 0x002ff320: notifies "trigger" with the other entity, runs the use callback (entityUseFuncs[+0x33c]); returns
// the other's entity number (0 for bad handles).
uint32_t XB_CDECL GScr_UseBy(uint32_t handle, uint32_t other_handle);
// 0x002ff3c0: FX_FreeEffectsById on the list at 0x0095f5b4.
void XB_CDECL GScr_FreeSound(uint32_t id);

// 0x002ff500: the zone of the world cell holding the point (CM_CellForPoint), else MemContext_FindById of the
// zone set's +0x34.
uint8_t* XB_CDECL GScr_GetZoneAtPoint(float x, float y, float z);
// 0x002ff5a0: CG_SpawnTracer from `from` to `to` when rand & 31 < scale * 32 (0x0094a8e0; 0.7 for weapon 0
// classes 3 and 4).
void XB_CDECL GScr_RandomTracer(const float* to, const float* from);
// 0x002ff680, 0x002ff760: out (DynArray) = the entity numbers of the spawners (of the team mask / all), at most 0x200.
void XB_CDECL GScr_GetSpawnerTeamArray(uint8_t* out, const ScriptString* team);
void XB_CDECL GScr_GetSpawnerArray(uint8_t* out);
// 0x002ff810: GScr_GetAI with the empty team (all).
void XB_CDECL GScr_GetAIAll(uint8_t* out);
// 0x002ff840: S_StopSoundHandle on the music handle (*0x0095f4cc).
void XB_CDECL GScr_MusicStop();
// 0x002ff850, 0x002ff8d0, 0x002ff950: Cvar_Get(name, value, 0x1000) then Cvar_Set2(name, value, forced) with
// "%f" / "%d" of the value / the text.
void XB_CDECL GScr_SetCvarFloat(const char* name, float value);
void XB_CDECL GScr_SetCvarInt(const char* name, int32_t value);
void XB_CDECL GScr_SetCvar(const char* name, char* value);
// 0x002ff9c0, 0x002ffa10: server command "gmb" / "gm" "<text>\n" to everyone.
void XB_CDECL GScr_SendGameMessageBold(const char* text);
void XB_CDECL GScr_SendGameMessage(const char* text);
// 0x002ffb50, 0x002ffb70: G_SetObjectiveString without / with the update message.
void XB_CDECL GScr_ObjectiveString(int32_t index, const ScriptString* text);
void XB_CDECL GScr_ObjectiveStringLocalized(int32_t index, const ScriptString* text);
// 0x002ffb90: "ls %i" with the sound alias's configstring index (0xa0 + up to 0x40, created) to the entity.
void XB_CDECL GScr_PlayLocalSound(uint32_t handle, const ScriptString* alias);
// 0x002ffc00: CG_SelectWeapon of the named weapon when the client owns it; AL = selected.
uint32_t XB_CDECL GScr_PlayerSwitchToWeapon(uint32_t handle, const ScriptString* weapon_name);
// 0x002ffcb0: vectoyaw of the negated actor obstacle direction (+0x3a8, +0x3ac); 0 without an actor (ST0).
float XB_CDECL GScr_ActorGetObstacleYaw_3a8(uint32_t handle);
// 0x002ffd40: the actor's motion yaw (path direction +0x75c, else velocity +0x2a0 when |v|^2 > 1, else 0), Q_fmod
// by the entity's +0x154 (ST0).
float XB_CDECL GScr_ActorGetMotionYaw(uint32_t handle);
// 0x002ffe40, 0x002fff10, 0x002fffa0: the sentient's goal position from a path node (and its yaw when node flag
// 0x8000 of +0x2c is set) / a point / a point and a yaw wrapped into [0, 360) (+0x20); 0x0095f704 = 0.
void XB_CDECL GScr_SetGoalPosFromNode(uint32_t handle, const int32_t* node_handle);
void XB_CDECL GScr_SetGoalPos(uint32_t handle, const float* point);
void XB_CDECL GScr_SetGoalPosAndYaw(uint32_t handle, const float* point, float yaw);

// 0x00300090, 0x003000e0: the value (+0xc) of the key's node in the int map of the owner at 0x0095f4e0 (+0x350)
// / set it; a missing key is inserted with 0 first.
uint32_t XB_CDECL GScr_GetIntMapValue(int32_t key);
void XB_CDECL GScr_SetIntMapValue(int32_t key, uint32_t value);
// 0x00300140: G_SaveCheckpoint(name, the owner at 0x0095f4e0, at the player).
void XB_CDECL GScr_SaveCheckpoint(const char* name);
// 0x00300160: the player's shell shock (client +0x4f4 kind 1-3 for "default"/"pain"/"death", +0x4f8 start,
// +0x4fc duration in ms) with that kind's parameters read from the cvars; registers the three names first while
// 0x009655a4 is empty.
void XB_CDECL GScr_ShellShock(uint32_t handle, const ScriptString* name, float duration);
// 0x00300320: DObj_DebugDrawAnimTrees on the entity's object (+0x22c).
void XB_CDECL GScr_DebugDrawAnimTrees(uint32_t handle);
// 0x00300360: Subtitle_Start(name, no category); AL as its.
uint8_t XB_CDECL GScr_StartSubtitle(const ScriptString* name);
// 0x00300390: the subtitle, then the alias at the entity (S_PlaySoundAlias; the channel's +0x2c = notify) and 1;
// -1 and the notify at once when it cannot play.
int32_t XB_CDECL GScr_PlaySoundWithNotify(uint32_t handle, const ScriptString* alias, uint32_t notify, uint32_t force);
// 0x003005a0: HUD element *hud cleared to a text element with the string's configstring index (0x293 + up to 0x40)
// at +0x68.
void XB_CDECL GScr_HudSetText(const int32_t* hud, const ScriptString* text);
// 0x00300630: with a live player: ClientDeathReload and the failure message (0x00985ec0) = the localized reason
// ("INGAME_PLAYER_DIED" for none or "NEED_LOVE").
void XB_CDECL GScr_MissionFailed(const ScriptString* reason);
// 0x003006d0, 0x00300710: S_UnloadSoundBank / S_LoadSoundBank of the named bank (no wait).
void XB_CDECL GScr_SoundBankUnload(const ScriptString* name);
void XB_CDECL GScr_SoundBankLoad(const ScriptString* name);
// 0x00300750, 0x00300780: S_AmbientPlay / Music_Play on the music state at 0x0095f4cc.
void XB_CDECL GScr_AmbientPlay(const ScriptString* name, uint32_t time_bits);
void XB_CDECL GScr_MusicPlay(const ScriptString* name);
// 0x00300ba0: G_SetObjectiveString with the localized string of the hash (with the update message).
void XB_CDECL GScr_ObjectiveStringByHash(int32_t index, uint32_t hash);
// 0x00300bf0: the actor's orient mode (+0x140) from the mode name (face angle/current/direction/enemy/...).
void XB_CDECL GScr_ActorSetOrientMode(uint32_t handle, const ScriptString* mode, float yaw, const float* vec);
// 0x00300e20: the actor's look anims (+0x210..+0x218), straight at goal 1 and the others cleared; look state reset.
void XB_CDECL GScr_ActorSetLookAnims(uint32_t handle, uint32_t straight, uint32_t left, uint32_t right);
// 0x00301640: G_RestorePlayerLoadout of the owner at 0x0095f4e0.
void XB_CDECL GScr_RestorePlayerLoadout();
// 0x00301650, 0x003016f0, 0x00301790: AnimTree_SetGoalWeight / SetKnobAll / siblings cleared + SetGoalWeight (goal
// below 0.001 = 0) on the entity's anim tree (an actor's created when missing), with a notify.
void XB_CDECL GScr_SetFlaggedAnim(uint32_t handle, uint32_t notify, uint32_t anim, uint32_t goal, uint32_t blend,
                                  uint32_t rate, uint32_t restart);
void XB_CDECL GScr_SetFlaggedAnimKnobAll(uint32_t handle, uint32_t notify, uint32_t anim, uint32_t stop, uint32_t goal,
                                         uint32_t blend, uint32_t rate, uint32_t restart);
void XB_CDECL GScr_SetFlaggedAnimKnob(uint32_t handle, uint32_t notify, uint32_t anim, float goal, uint32_t blend,
                                      uint32_t rate, uint32_t restart);

}  // namespace game
