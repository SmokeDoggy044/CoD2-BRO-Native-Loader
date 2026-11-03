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

// 0x00262130: a rotating door's blocked callback (blocker freed or damaged, the team reverses).
void XB_CDECL Blocked_DoorRotate(uint8_t* ent, uint8_t* other);
// 0x00262430: Q3's Blocked_Door.
void XB_CDECL Blocked_Door(uint8_t* ent, uint8_t* other);
// 0x00262740: Q3's Reached_BinaryMover (with the rotation states 9/10).
void XB_CDECL Reached_BinaryMover(uint8_t* ent);
// 0x00262b40 (leader in EDI, time in EBX, cdecl state): the team's rotations reversed, then MatchTeam.
extern "C" PLATFORM_HIDDEN void XB_CDECL Mover_ReverseTeam(uint8_t* leader, int32_t time, uint32_t state);
extern "C" void Mover_ReverseTeam_entry(void);
// 0x00262ba0 (leader in EDI, cdecl state, time): Q3's MatchTeam.
extern "C" PLATFORM_HIDDEN void XB_CDECL MatchTeam(uint8_t* leader, uint32_t state, int32_t time);
extern "C" void MatchTeam_entry(void);
// 0x00262d70: a turret spawn from its "weaponinfo" key.
void XB_CDECL SP_turret(uint8_t* ent);
// 0x00266d50: MatchTeam with the entity's own state.
void XB_CDECL Think_MatchTeam(uint8_t* ent);
// 0x00266fa0: MatchTeam state 10.
void XB_CDECL ReturnToPos1Rotate(uint8_t* ent);
// 0x00266fc0: MatchTeam state 5.
void XB_CDECL Think_MatchTeam2To3(uint8_t* ent);
// 0x00266fe0: MatchTeam state 6.
void XB_CDECL Think_MatchTeam3To2(uint8_t* ent);
// 0x00267000: Q3's ReturnToPos1 (MatchTeam state 4).
void XB_CDECL ReturnToPos1(uint8_t* ent);
// 0x00267980: the incoming-shell sound, think 9 in 1.1 s; returns the sound call's EAX.
uint32_t XB_CDECL Artillery_Incoming(uint8_t* ent);

// 0x00262e00 (turret, actor): 1 when the actor's enemy is no reason to leave the turret, else
// whether the sight trace from beside the weapon tag to its eye is blocked.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_TurretCanTargetEnemy(uint32_t entry, uint8_t* turret, uint8_t* actor,
                                                                    uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void Actor_TurretCanTargetEnemy_entry(void);
// 0x002635c0 (turret, entity, squared distance out or null): 1 when the turret can shoot it.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_CanTargetSentient(uint32_t entry, uint8_t* turret, uint8_t* target,
                                                                  float* out_dist2, uint32_t ebx, uint32_t esi,
                                                                  uint32_t edi, uint32_t edx, uint32_t ebp);
extern "C" void Turret_CanTargetSentient_entry(void);
// 0x002638b0 (turret, sentient, aiming, convergence ms, angles out): the aim point, converging
// from beside the target at first; 0 when it cannot aim.
uint32_t XB_CDECL Turret_ComputeAimPoint(uint8_t* ent, uint8_t* sentient, uint32_t aiming, int32_t converge,
                                         float* angles);

// 0x00265900 (actor in EDI, duration in EBX): starts entering prone (+0x198 {state, start, blend}):
// an expired blend cleared; restarts now with BG_CheckProne into +0x194 unless a blend with another
// duration runs (then started earlier by its fraction).
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_EnterProne(uint8_t* actor, int32_t duration);
extern "C" void Actor_EnterProne_entry(void);
// 0x00266d70 (door in EBX): Q3's Think_SpawnNewDoorTrigger; returns the trigger entity.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Think_SpawnNewDoorTrigger(uint8_t* ent);
extern "C" void Think_SpawnNewDoorTrigger_entry(void);
// 0x0026b920: the trigger with contents 0x1c0000; doors spawned open (+0x2a0 bit 1) turn.
void XB_CDECL Think_SpawnNewDoorTriggerRotate(uint8_t* ent);
// 0x0026b950: think wrapper (EBX = the entity) of Think_SpawnNewDoorTrigger.
void XB_CDECL Think_SpawnNewDoorTrigger_Entry(uint8_t* ent);
// 0x00265630 (cdecl entity): the cell, a reset when not in water, traces up/down to the floor.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Think_DropToFloor(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                               uint32_t ebp);
extern "C" void Think_DropToFloor_entry(void);
// 0x002650b0 (origin in EDI, cdecl target): Q3's CanDamage over five points.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CanDamage(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                       uint32_t ebp);
extern "C" void CanDamage_entry(void);
// 0x00267660 (direction in EAX, start in ECX; cdecl owner, fuse seconds): the missile entity.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL fire_projectile(uint32_t entry, float* dir, const float* start,
                                                             uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void fire_projectile_entry(void);
// 0x00264b60 (cdecl item, caller's second word): Q3's FinishSpawningItem.
extern "C" PLATFORM_HIDDEN void XB_CDECL FinishSpawningItem(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                            uint32_t ebp);
extern "C" void FinishSpawningItem_entry(void);
// 0x002631a0 (cdecl turret): the pitch from the aim tag at which a trace toward the butt offset hits.
extern "C" PLATFORM_HIDDEN void XB_CDECL turret_think_init(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                           uint32_t ebp);
extern "C" void turret_think_init_entry(void);

// 0x002679c0: think 0xb after the weapon's fire delay (+0x754) and the "MP_Artillery_Fire" sound.
uint32_t XB_CDECL Artillery_Fire(uint8_t* ent);
// 0x00267a10: drops the item named by +0x3b8 in place (vtos of the origin when that fails).
void XB_CDECL Think_DropNamedItem(uint8_t* ent);
// 0x00267a50 (actor in ECX, cdecl turret): 1 without an enemy or one 256+ units away, else
// Actor_TurretCanTargetEnemy. Proposed name (was FUN_00267a50).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_TurretCheckNearEnemy(uint32_t entry, uint8_t* actor, uint32_t ebx,
                                                                        uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void Actor_TurretCheckNearEnemy_entry(void);
// 0x00267ac0 (cdecl turret): the auto-targeting turret's sentient (current one, else the nearest
// targetable of the enemy teams, one candidate per call); 0 for none.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_PickSentientTarget(uint32_t entry, uint32_t ebx, uint32_t esi,
                                                                       uint32_t edi, uint32_t ebp);
extern "C" void Turret_PickSentientTarget_entry(void);
// 0x00267c30 (turret in EDI, aiming in EDX, convergence in ECX; cdecl sentient): aims; 1 when it can.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_AimAtTarget(uint8_t* ent, uint32_t aiming, int32_t converge,
                                                                uint8_t* sentient);
extern "C" void Turret_AimAtTarget_entry(void);
// 0x00268770 (player in EDI, cdecl item): health pickup; returns -1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Pickup_Health(uint8_t* other, uint8_t* ent);
extern "C" void Pickup_Health_entry(void);
// 0x00269450: Q3's player_die (self, inflictor, attacker, damage, means of death, weapon).
void XB_CDECL player_die(uint8_t* self, uint8_t* inflictor, uint8_t* attacker, int32_t damage, uint32_t mod,
                         uint32_t weapon);
// 0x00269570: the player's sentient claims the nearest path node within 32 units.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_UpdatePlayerNodeClaim(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                                 uint32_t ebp);
extern "C" void G_UpdatePlayerNodeClaim_entry(void);
// 0x00269700 (player in EBX): Weapon_MeleeTrace from the view axis and origin.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_PlayerMelee(uint32_t entry, uint8_t* ent, uint32_t esi, uint32_t edi,
                                                       uint32_t ebp);
extern "C" void G_PlayerMelee_entry(void);
// 0x00269cb0 (entity in EDI): Q3's G_CallSpawn (spawn map, actor_*, items).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_CallSpawnByClassname(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                                uint32_t ebp);
extern "C" void G_CallSpawnByClassname_entry(void);
// 0x00269d80: spawn function of script_vehicle ("vehicletype" key).
void XB_CDECL SP_script_vehicle(uint8_t* ent);

// 0x00268e60: Q3's G_RadiusDamage (origin, inflictor, attacker, max, min, radius, ignore, means of
// death); 1 when another client with health was hit.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_RadiusDamage(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                            uint32_t ebp);
extern "C" void G_RadiusDamage_entry(void);
// 0x0026a130 (mover in EAX): the player stuck in the mover is pushed, moved free or killed.
extern "C" PLATFORM_HIDDEN void XB_CDECL Mover_UnstickPlayer(uint32_t entry, uint8_t* mover, uint32_t ebx, uint32_t esi,
                                                             uint32_t edi, uint32_t ebp);
extern "C" void Mover_UnstickPlayer_entry(void);
// 0x0026c4d0 (actor in ECX, turret in EDI): whether the actor can use the turret.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Actor_CanUseTurret(uint32_t entry, uint8_t* actor, uint32_t ebx, uint32_t esi,
                                                                uint32_t edi, uint32_t ebp);
extern "C" void Actor_CanUseTurret_entry(void);
// 0x0026cd70 (turret in EAX): the auto-targeting turret's think step.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_ThinkAutoTarget(uint32_t entry, uint8_t* turret, uint32_t ebx, uint32_t esi,
                                                                uint32_t edi, uint32_t ebp, uint32_t edx);
extern "C" void Turret_ThinkAutoTarget_entry(void);
// 0x0026d0d0: Q3's Touch_Item (item, player, touched flag).
void XB_CDECL Touch_Item(uint8_t* ent, uint8_t* other, uint32_t touched);

// 0x0026d440: Touch_Item re-armed on every touch (not for clients with +0x630).
void XB_CDECL Touch_Item_Auto(uint8_t* ent, uint8_t* other, uint32_t touched);
// 0x002ae8f0 (stdcall entity): whether the entity's cell is valid (always without cell data).
uint8_t XB_STDCALL G_EntityInValidCell(uint8_t* ent);
// 0x00270990: Q3's Use_BinaryMover (doors: open, close, reverse; team master).
void XB_CDECL Use_BinaryMover(uint8_t* ent, uint8_t* other, uint8_t* activator);
// 0x002710e0 (door in EAX; cdecl activator): the activate key on a door.
void XB_REGS1 XB_CDECL TryDoor(uint8_t* door, uint8_t* activator);
// 0x00271210: a door trigger's touch - uses its door.
void XB_CDECL Touch_DoorTrigger(uint8_t* ent, uint8_t* other, uint32_t touched);
// 0x00270c10 (user in EAX; cdecl turret, target, aim): one shot from each of the turret's flash tags.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_FireFromFlashTags(uint32_t entry, uint8_t* user, uint32_t ebx,
                                                                  uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void Turret_FireFromFlashTags_entry(void);
// 0x002712a0 (turret in EAX, user in ESI): the turret fires for its user.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_FireForUser(uint32_t entry, uint8_t* turret, uint32_t ebx, uint32_t esi,
                                                            uint32_t edi, uint32_t ebp);
extern "C" void Turret_FireForUser_entry(void);
// 0x0026f6d0 (mover in EAX; cdecl think word): Q3's G_RunMover.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_RunMover(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                                    uint32_t edi, uint32_t ebp);
extern "C" void G_RunMover_entry(void);
// 0x0026fb00 (item in EAX; cdecl think word): Q3's G_RunItem.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_RunItem(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                                   uint32_t edi, uint32_t ebp);
extern "C" void G_RunItem_entry(void);
// 0x00270df0 (player in EAX): Q3's FireWeapon.
extern "C" PLATFORM_HIDDEN void XB_CDECL FireWeapon(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                                    uint32_t edi, uint32_t ebp);
extern "C" void FireWeapon_entry(void);

// 0x0026e450 (cdecl mover): Q3's G_MoverTeam.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_MoverTeam(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi,
                                                     uint32_t edi, uint32_t ebp);
extern "C" void G_MoverTeam_entry(void);

// 0x0026eab0 (turret in EAX; cdecl actor user or 0): the turret think for an AI user; 1 or Turret_UpdateTarget's result.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_ThinkActorUser(uint32_t entry, uint8_t* turret, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void Turret_ThinkActorUser_entry(void);
// 0x0026e810 (grenade in EAX; cdecl duration, out position, bounce, out time): the predicted grenade path.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_SimulateGrenadePath(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void G_SimulateGrenadePath_entry(void);
// 0x0026ecf0 (trace in EAX; cdecl item): Q3's G_BounceItem.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_BounceItem(uint32_t entry, uint8_t* tr, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void G_BounceItem_entry(void);
// 0x0026f890 (cdecl turret, msec): the turret's think.
extern "C" PLATFORM_HIDDEN void XB_CDECL turret_think(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void turret_think_entry(void);
// 0x002744e0 (entity in EAX, the runners' word in EDI): Q3's G_RunEntity.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_RunEntity(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void G_RunEntity_entry(void);
// 0x00271b60 (turret in EBX; cdecl player): the player's turret fire.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_UpdatePlayerFire(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void Turret_UpdatePlayerFire_entry(void);
// 0x00272760: Q3's G_SpawnFinished-like end of level spawning.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SpawnFinished(uint32_t entry, uint32_t eax, uint32_t ebx, uint32_t esi,
                                                         uint32_t edi, uint32_t ebp, uint32_t ecx);
extern "C" void G_SpawnFinished_entry(void);
// 0x00272e90 (cdecl handle): Q3's ClientBegin.
extern "C" PLATFORM_HIDDEN void XB_CDECL ClientBegin(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi,
                                              uint32_t edi, uint32_t ebp);
extern "C" void ClientBegin_entry(void);

// 0x00271590 (old event sequence in EAX; cdecl player): Q3's ClientEvents.
extern "C" PLATFORM_HIDDEN void XB_CDECL ClientEvents(uint32_t entry, uint32_t old_sequence, uint32_t ebx, uint32_t esi,
                                                      uint32_t edi, uint32_t ebp);
extern "C" void ClientEvents_entry(void);
// 0x00273d70 (cdecl command, arguments): Q3's vmMain for the game.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_vmMain(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi,
                                                      uint32_t edi, uint32_t ebp);
extern "C" void G_vmMain_entry(void);

// 0x00302740 (entity in EAX, cdecl velocity, time, accel, decel): ScriptMover_SetupRotateVelocity for the angles
// trajectory (+0x38, current angles +0x150), then SV_LinkEntity.
void XB_REGS1 XB_CDECL ScriptMover_RotateVelocity(uint8_t* ent, uint32_t velocity, uint32_t time, uint32_t accel,
                                                  uint32_t decel);
// 0x003027a0 / 0x00302870 (entity in EAX, cdecl target, time, accel, decel): ScriptMover_SetupMove for the
// angles trajectory (+0x38 from +0x150) / the origin trajectory (+0x14 from +0x140), then SV_LinkEntity.
void XB_REGS1 XB_CDECL ScriptMover_RotateTo(uint8_t* ent, uint32_t target, uint32_t time, uint32_t accel,
                                            uint32_t decel);
void XB_REGS1 XB_CDECL ScriptMover_MoveTo(uint8_t* ent, uint32_t target, uint32_t time, uint32_t accel,
                                          uint32_t decel);
// 0x00302800 (velocity in EAX, entity in ESI, time in XMM0): a gravity trajectory (type 5) for the origin from the
// current origin at level_time for time * 1000 ms, evaluated at once, then SV_LinkEntity.
extern "C" PLATFORM_HIDDEN void XB_CDECL ScriptMover_MoveGravity(const uint32_t* velocity, uint8_t* ent, float time);
extern "C" void ScriptMover_MoveGravity_entry(void);

// 0x002545e0: TODO.
// 0x002545e0 (goal EAX, start ECX, end EDX, time XMM1; cdecl trajectory, speed, time2): Reached_ScriptMover's next
// phase - type 9 with time > 0: linear (3) from start to goal; type 3 (or 9 with time <= 0) and time2 > 0: toward
// end at speed (10) from the goal; else stopped (0) where a gravity trajectory (5) is now or at end: EAX 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ScriptMover_NextTrajectoryPhase(const float* goal, const float* start,
                                                                         const float* end, float time, uint8_t* tr,
                                                                         float speed, float time2);
extern "C" void ScriptMover_NextTrajectoryPhase_entry(void);

// 0x00300f20 (velocity in EBX, trajectory in ESI; cdecl time, accel, decel, current, then the speed, linear
// time, decel time, accel end, linear end and end outputs): a script mover's rotate-at-velocity phases.
extern "C" PLATFORM_HIDDEN void XB_CDECL ScriptMover_SetupRotateVelocity(const float* velocity, uint8_t* tr, float time,
                                                                         float accel, float decel, float* current,
                                                                         float* speed, float* mid_time,
                                                                         float* decel_time, float* accel_end,
                                                                         float* mid_end, float* end);
extern "C" void ScriptMover_SetupRotateVelocity_entry(void);
// 0x00301240 (trajectory in EAX, target in EBX, current in ESI; cdecl time, accel, decel, then the speed, linear
// time, decel time, accel end, linear end and end outputs): a script mover's move/rotate-to phases.
extern "C" PLATFORM_HIDDEN void XB_CDECL ScriptMover_SetupMove(uint8_t* tr, const float* target, float* current,
                                                               float time, float accel, float decel, float* speed,
                                                               float* mid_time, float* decel_time, float* accel_end,
                                                               float* mid_end, float* end);
extern "C" void ScriptMover_SetupMove_entry(void);

// 0x0026ce50 (turret in EAX, cdecl player): the player's turret view clamped to the arc and the turn rate.
void XB_REGS1 XB_CDECL Turret_ClampPlayerViewAngles(uint8_t* turret, uint8_t* player);

// 0x00272f20 (trace in EAX, entity in EDI): Q3's G_BounceMissile; 1 when the impact should make a bounce sound
// (out of water, velocity changed by more than 100).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_BounceMissile(const uint8_t* trace, uint8_t* ent);
extern "C" void G_BounceMissile_entry(void);

// 0x002723c0 (entity in EAX, cdecl word handed to the vehicle thinks as EBX): Q3's ClientEndFrame.
void XB_REGS1 XB_CDECL ClientEndFrame(uint8_t* ent, uint32_t arg);

// 0x00288df0 (player turn flag in DL, turret in EDI): the player's turret turn effects (loop/stop) and the aim
// bones' rotations.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_UpdateTurnFxAndBones(uint32_t player_turn, uint8_t* turret);
extern "C" void Turret_UpdateTurnFxAndBones_entry(void);

}  // namespace game
