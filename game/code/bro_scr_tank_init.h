#pragma once

// Compiled level-script tank and vehicle init functions: team, anim tree, health, the script
// threads (init, turret, stun, dead stop, hull gunner, wheel origins) and the default wheel smoke.
// Not registered (thread_switch): the started threads run at once; compare showed mismatches only in
// the host block holding a saved stack pointer (docs/levelscript_classes.csv).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// Shared tail of every init below: field 0x8473dd9d = 0x10; when the level's wheel-smoke map
// (0x00922ee0 + 0x57c) has no "grass" entry (key 0x438c482f, written over the vehicle's argument
// slot), BroScr_SetWheelSmokeFx(vehicle, "day" or the empty string); then the
// BroScr_SpawnWheelOrigins thread.

// 0x00166ff0: team "allies" for the model xmodel/v_us_lnd_sherman, else "axis"; "panzerIV" anim
// tree; handle field 0x6e688ecd = the level's handle at +0x6ec when valid; health 500 +
// RandomInt(1000); threads VehicleHealthThink, VehicleInit_PanzerIV, VehicleTurretFireLoop_4,
// TankStunOnDamage(1); TankHullGunnerAtGunhull(weapon, "mg34_german_tank" when null). Smoke "day".
void XB_CDECL BroScr_TankInit(uint32_t tank, ScriptString weapon);
// 0x001673c0: "allies", "panzerIV", TankDefaultHealthThink_2; threads VehicleInit_Sherman,
// VehicleTurretFireLoop_5, TankStunOnDamage(1), VehicleDeadStop; TankHullGunnerAtGunhull(weapon,
// "mg30cal_sherman_tank" when null). Smoke: the empty string.
void XB_CDECL BroScr_TankInit_2(uint32_t tank, ScriptString weapon);
// 0x001676f0 / 0x00167dc0 (no dead stop): "allies", "panzerIV", TankInitHealth; threads
// VehicleExplodeSound, TankTurretFireLoopRecoil_3, TankStunOnDamage(1), [VehicleDeadStop];
// TankHullGunnerThink(weapon or "mg30cal_sherman_tank", tag). Smoke: the empty string.
void XB_CDECL BroScr_TankInit_3(uint32_t tank, ScriptString weapon, ScriptString tag);
void XB_CDECL BroScr_TankInit_4(uint32_t tank, ScriptString weapon, ScriptString tag);
// 0x00166620 / 0x001668e0 (no dead stop): as _3 with "axis", TankDefaultHealthThink,
// VehicleInit_M13 and TankTurretFireLoopRecoil.
void XB_CDECL BroScr_TankInit_5(uint32_t tank, ScriptString weapon, ScriptString tag);
void XB_CDECL BroScr_TankInit_6(uint32_t tank, ScriptString weapon, ScriptString tag);
// 0x001679b0 / 0x00167bd0 (no dead stop): "allies", "panzerIV", TankInitHealth; threads
// VehicleExplodeSound, TankTurretFireLoopRecoil_3, TankStunOnDamage(1), [VehicleDeadStop]; no
// gunner. Smoke: the empty string.
void XB_CDECL BroScr_TankInit_7(uint32_t tank);
void XB_CDECL BroScr_TankInit_8(uint32_t tank);
// 0x00165d30 / 0x00166d90: "allies", "willyjeep" / "halftrack", health 500 + RandomInt(1000);
// threads VehicleHealthThink, VehicleInit_Jeep / _Halftrack, VehicleTurretFireLoop_7 / _2,
// TankStunOnDamage(1), VehicleDeadStop. Smoke "day".
void XB_CDECL BroScr_VehicleInit_JeepAllies(uint32_t vehicle);
void XB_CDECL BroScr_VehicleInit_HalftrackAllies(uint32_t vehicle);
// 0x00166b70: as BroScr_TankInit_7 with "axis", TankDefaultHealthThink, VehicleInit_M13 and
// TankTurretFireLoopRecoil.
void XB_CDECL BroScr_VehicleInit_PanzerIV_2(uint32_t tank);
// 0x00168050: as BroScr_TankInit_8 without the team.
void XB_CDECL BroScr_PanzerIVSetup(uint32_t tank);

}  // namespace game
