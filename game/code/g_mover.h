#pragma once

// Game module: movers (script movers, doors) and falling entities.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0024c3b0 (entity in ESI): sets a script mover up: the "light"/"color" spawn variables
// packed into an RGBA (+0xa8) when either is set, entity type 7, the trajectory and angles from
// the current origin/angles, flag 0x8000.
extern "C" PLATFORM_HIDDEN void XB_CDECL InitScriptMover(uint8_t* ent);
extern "C" void InitScriptMover_entry(void);
// 0x0024c6f0: a door's first think: thinks again in 100 ms; a team master picks its think
// (0x19, 0x1b rotating, 0x1a with spawn flag 8) and hands its +0x3b4 to the team.
void XB_CDECL Think_InitDoor(uint8_t* ent);
// 0x0024c800: think: drops the entity from 1000 units above (gravity trajectory from
// level.time - 50) and frees it after 30 s (think 8).
void XB_CDECL Artillery_Drop(uint8_t* ent);

// 0x0024fb10 (aiming in EAX, turret in ECX): the turret info's (+0x24c) aim state (+1: 0 idle,
// 2 turning, 1 on target; "turretstatechange" when leaving or entering 0) from the angle
// deltas to `angles` (pitch, yaw); then +2 bit 0x40 and the aim point into +0x10.
void XB_REGS3 XB_CDECL Turret_UpdateAimState(uint32_t aiming, uint32_t unused_edx, uint8_t* ent,
                                             const float* aim_point, const float* angles);
// 0x0024fcc0 (turret in EDI): a new random aim offset (+0x68) every 4.5..7.5 s, then turns
// toward it (Turret_UpdateTargetAngles with AI rates).
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_UpdateAimWithJitter(uint8_t* ent);
extern "C" void Turret_UpdateAimWithJitter_entry(void);
// 0x0024fd90 (turret in EAX): clears info +2 bits 0x48 and +0xc, state idle.
void XB_REGS1 XB_CDECL Turret_SetIdle(uint8_t* ent);

// 0x00254800 (door in EBX; cdecl activator): 1 when a func_door_rotating should open away from the activator (it stands in front).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Door_ChooseRotateDirection(uint8_t* door, uint8_t* activator);
extern "C" void Door_ChooseRotateDirection_entry(void);
// 0x00255050 (entity in EDI): ends a scripted anim (+0x3d4) once it has played.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_UpdateScriptedAnimEnd(uint8_t* ent);
extern "C" void G_UpdateScriptedAnimEnd_entry(void);
// 0x00255580: spawn for script_origin.
void XB_CDECL SP_script_origin(uint8_t* ent);
// 0x002555d0: spawn for script_brushmodel.
void XB_CDECL SP_script_brushmodel(uint8_t* ent);

// 0x00254bd0 (turret in ESI, player in EDI): 1 when the player is inside the turret's yaw arc.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_IsPlayerWithinArc(const uint8_t* turret, const uint8_t* player);
extern "C" void Turret_IsPlayerWithinArc_entry(void);
// 0x00254a10 (trajectory in EAX, trace in ESI; cdecl ent, time): the grenade path preview's bounce.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SimulateGrenadeBounce(uint8_t* trajectory, const uint8_t* trace, const uint8_t* ent,
                                                          int32_t time);
extern "C" void G_SimulateGrenadeBounce_entry(void);
// 0x00255610: script mover reached callback (position and rotation phases, movedone / rotatedone).
void XB_CDECL Reached_ScriptMover(uint8_t* ent);
// 0x002581a0: func_rotate use callback: starts the rotation, linked.
void XB_CDECL Use_Func_Rotate(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x002581f0: use callback: toggles the link.
void XB_CDECL Use_Static(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x00258220 (entity in ESI): func_door_rotating's mover setup.
extern "C" PLATFORM_HIDDEN void XB_CDECL InitMoverRotate(uint8_t* ent);
extern "C" void InitMoverRotate_entry(void);
// 0x002583f0 (entity in ESI): Q3's InitMover (noise, light, move state, duration).
extern "C" PLATFORM_HIDDEN void XB_CDECL InitMover(uint8_t* ent);
extern "C" void InitMover_entry(void);
// 0x002589e0: use callback: think 0x11 next frame.
void XB_CDECL Use_DropNamedItem(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x00258a70 (player in EAX, turret in ESI): moves the player 34 units behind the turret.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_PlacePlayerBehind(uint8_t* player, uint8_t* turret);
extern "C" void Turret_PlacePlayerBehind_entry(void);
// 0x00258ba0: use callback: toggles the link (linking clears byte +0x297).
void XB_CDECL Use_ToggleLink(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x00258ce0: Q3's RespawnItem (random team member, solid again).
void XB_CDECL RespawnItem(uint8_t* ent);

// 0x0025abe0: spawn for func_pendulum.
void XB_CDECL SP_func_pendulum(uint8_t* ent);
// 0x0025ad50: spawn for func_bobbing.
void XB_CDECL SP_func_bobbing(uint8_t* ent);
// 0x0025aec0: spawn for func_rotating.
void XB_CDECL SP_func_rotating(uint8_t* ent);
// 0x0025af90: spawn for func_static.
void XB_CDECL SP_func_static(uint8_t* ent);
// 0x0025b3f0 (turret in EAX): the turret's user gets off it.
void XB_REGS1 XB_CDECL G_ClientStopUsingTurret(uint8_t* turret);
// 0x0025b4d0 (entity in EDI): advances the model's anims through every notetrack of this frame.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_DObjAdvanceAnimsUntilDone(uint8_t* ent);
extern "C" void G_DObjAdvanceAnimsUntilDone_entry(void);

// 0x002586f0 (angles in ESI; cdecl ent, trace, keep_spin): G_BounceMissile's angles at the
// hit time; on a floor the pitch is laid along the plane and the spin damped. Proposed name
// (was FUN_002586f0).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_BounceMissileAngles(float* angles, uint8_t* ent, const uint8_t* trace,
                                                              int32_t keep_spin);
extern "C" void G_BounceMissileAngles_entry(void);
// 0x0025a960: spawn for func_door_rotating.
void XB_CDECL SP_func_door_rotating(uint8_t* ent);
// 0x0025b0a0: spawn for func_door.
void XB_CDECL SP_func_door(uint8_t* ent);

// 0x0025b520: use callback: RespawnItem.
void XB_CDECL Use_Item(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x0025ba30: a player leaves its vehicle by the popout/player tag (dead code: no callers).
void XB_CDECL G_VehiclePopOut(uint8_t* player);
// 0x0025c3d0: use callback: the activator mounts the turret (or leaves it when on one).
void XB_CDECL turret_use(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x0025c6a0: controller callback: the aim angles onto the turret's aim bones.
void XB_CDECL turret_controller(uint8_t* ent);

// 0x0025ccd0: 1 when the point is inside the turret's arcs as seen from its flash tag (or
// its origin without a model / with DObj flag 1); the start and the angle offsets out.
uint32_t XB_CDECL Turret_CanTargetPoint(uint8_t* ent, const float* point, float* start, float* angles);
// 0x0025ced0 (index in EAX, entity in ECX): the flash tag (0 "tag_flash", 1 "tag_flash_2")
// as an axis from the entity's aim angles, the tag origin and the tag's first row; 0 without.
uint32_t XB_REGS3 XB_CDECL G_GetFlashTagMatrix(int32_t index, uint32_t unused_edx, uint8_t* ent, float* out);
// 0x0025d1e0 (turret in EAX, angles out in EDI; point, aiming on the stack): aims at the
// point when Turret_CanTargetPoint allows it (Turret_UpdateAimState), else clears info +2
// bit 0x40.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_TryAimAtTarget(uint8_t* ent, const float* point, uint32_t aiming,
                                                                  float* angles);
extern "C" void Turret_TryAimAtTarget_entry(void);
// 0x0025deb0: spawn for script_model. The entry runs Path_DisconnectPathsForEntity at the
// original's ESP (its node query stores a stack address in 0x00929b40).
extern "C" PLATFORM_HIDDEN void XB_CDECL SP_script_model(uint32_t entry, uint8_t* ent, uint32_t edi);
extern "C" void SP_script_model_entry(void);
// 0x0025dfc0 (door in EAX): a door's path links by its state (+0x298: 7 locked-aware, 8 off).
void XB_REGS1 XB_CDECL Door_UpdatePaths(uint8_t* door);

// 0x0025e000 (time in EAX, state in EDX, mover in ECX): Q3's SetMoverState (positions,
// linear moves, rotations), then the current origin, link and door path links.
void XB_REGS3 XB_CDECL SetMoverState(int32_t time, uint32_t state, uint8_t* ent);
// 0x0025e450 (origin in EAX, owner in ECX; fuse on the stack): a cooked grenade dropped
// straight down (cluster grenades); returns it.
uint8_t* XB_REGS3 XB_CDECL fire_grenadeCooked(const float* origin, uint32_t unused_edx, uint8_t* owner, int32_t fuse);
// 0x0025e6e0 (weapon in EAX, owner in EDX, velocity in ECX; origin, fuse on the stack): Q3's
// fire_grenade; returns the grenade.
uint8_t* XB_REGS3 XB_CDECL fire_grenade(uint32_t weapon, uint8_t* owner, const float* velocity, const float* origin,
                                        int32_t fuse);
// 0x0025f290 (turret in EAX, target in ECX; aiming on the stack): tracks a target entity's
// origin when the arcs allow it; 1 when aiming at it.
uint32_t XB_REGS3 XB_CDECL Turret_TrackTargetEnt(uint8_t* ent, uint32_t unused_edx, uint8_t* target, uint32_t aiming);
// 0x00262040 (door in EBX): a rotating door turns to its open angles at once (think 0x15).
extern "C" PLATFORM_HIDDEN void XB_CDECL Door_StartRotate(uint8_t* ent);
extern "C" void Door_StartRotate_entry(void);

// 0x0025e9b0 (entity in ESI, cdecl weapon name): the turret info from the weapon def and spawn keys.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_ParseInfo(uint8_t* ent, const char* weapon);
extern "C" void Turret_ParseInfo_entry(void);
// 0x0025f310: the aim at a sentient from the turret's tag_flash within its arcs; 1 when it can aim.
uint32_t XB_CDECL Turret_GetTargetAimPoint(uint8_t* ent, uint8_t* sentient, float* eye, float* flash, float* angles);
// 0x0025ff10 (fire parameters in EAX, owner in EDI): a launched grenade; returns it.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL weapon_grenadelauncher_fire(const float* params, uint8_t* owner,
                                                                       uint32_t weapon);
extern "C" void weapon_grenadelauncher_fire_entry(void);
// 0x00261df0 (entity in ESI): a clear spot near a stuck entity, moved there; 1 when found.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_FindUnstuckPosition(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t edi,
                                                                uint32_t ebp);
extern "C" void G_FindUnstuckPosition_entry(void);

}  // namespace game
