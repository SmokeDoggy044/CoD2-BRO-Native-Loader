#pragma once

// Game module: vehicles, turrets and the weapon-slot helpers they use (gentity_t and the
// vehicle record (+0x248) are reached by byte offsets).

#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x002fd650: how many of the g_vehicleSlotCount (0x00986000) vehicle slots (0x0095f694,
// 0x340 bytes each) are in use (+0x170).
int32_t XB_CDECL Vehicle_CountActive();
// 0x002487d0 (name in EDI): the vehicle def named so (_stricmp) in AX, -1 when none; the
// rest of EAX as the last _stricmp (or the caller) left it.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_FindDefIndex(uint32_t eax, const char* name);
extern "C" void Vehicle_FindDefIndex_entry(void);
// 0x00249e70: frees the vehicle defs (Z_Free) and clears their count. EAX is left as
// Z_Free left it; the caller (G_ShutdownGame) overwrites it.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_FreeDefs();
// 0x002b5df0 (index in EBX; cdecl fields, count): a zeroed 0x870-byte weapon def in
// bg_weaponDefs[index], name "" hashed, string fields (type 0) set to "".
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL BG_AllocWeaponDef(uint32_t index, const uint32_t* fields, int32_t count);
extern "C" void BG_AllocWeaponDef_entry(void);
// 0x00226720 (stdcall vehicle physics record): the eight wheel bones (names 0x008db414) in
// +0..+0x1c, each found one's base pose as a 4x4 at +0x220 + 0x40 * i. Returns 8.
int32_t XB_STDCALL Vehicle_InitWheelTags(int32_t* vehicle);
// 0x002b5690 (weapon in EAX, playerState in ESI): gives the weapon (and its alternates,
// +0x718) unless owned or of kind 7/9, puts it in a free slot by its slot kind (+0x9c).
// Returns 1 when given.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_GivePlayerWeapon(uint32_t weapon, uint8_t* ps);
extern "C" void BG_GivePlayerWeapon_entry(void);
// 0x002b5530 (weapon in EAX, playerState in ECX): takes the weapon and its alternates; its
// slot gets the first other owned weapon that has none (when +0xa0), else 0. Returns 1
// when the weapon was owned.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_RemoveWeaponFromSlot(uint32_t weapon, uint8_t* ps);
extern "C" void BG_RemoveWeaponFromSlot_entry(void);
// 0x0025a400: a push on the vehicle: the direction against its forward/up axes (+0x1e8,
// +0x1ec, normalized, times the def's +0x54/+0x58 and scale clamped to 0..1), spin
// (+0x1f8 = ent +0x2fc * spin) and +0x1fc. EAX = the def (the entity when no vehicle).
uint32_t XB_CDECL Vehicle_ApplyPush(uint8_t* ent, const float* direction, float scale, float spin, uint32_t extra);
// 0x0024e300 (AI flag in EDX; cdecl ent, target angles): turns the turret's pitch and yaw
// (+0x90) toward the target at the weapon's turn speeds (+0x828/+0x82c; 200 without AI)
// this frame; Scr_Notify "turret_on_target" and 1 when both arrive. The entry hands the C
// version the argument block (the original keeps a float in the first slot).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_UpdateTargetAngles(uint32_t* args, uint32_t ai, uint8_t* ent,
                                                                      const float* target);
extern "C" void Turret_UpdateTargetAngles_entry(void);
// 0x0024dc70: the vehicle's tag indices (tag_player .. tag_coax, +0x2b0..+0x2c8) and the
// two bone tables (0x009886e8 -> +0x2cc, 0x009886f8 -> +0x2dc; "tag_origin" = bone 0),
// the name hashes computed once. EAX = the last value stored.
uint32_t XB_CDECL Vehicle_CacheTags(uint8_t* ent);

// 0x002314c0: for each physics vehicle (list 0x008feb70, count 0x008feb9c): its pose
// (+0x170, 64 bytes) into its physics body (+0x154) +0x160, then G_SetPoseFromPhysics.
void XB_CDECL Vehicle_PhysicsPreFrame();
// 0x00258a10 (player in EAX, turret in ECX): the turret is usable (+0x24c flag 0x10, not
// in use, a weapon, the player in its arc (0x00254bd0)) by a client that is not busy.
uint32_t XB_REGS3 XB_CDECL Turret_IsUsableBy(uint8_t* player, uint32_t, uint8_t* turret);
// 0x0025b810 (pain callback): bullet and grenade-class weapons (+0x94 1/2) push the
// vehicle along the hit direction (Vehicle_ApplyPush, scale 1).
void XB_CDECL Vehicle_Pain(uint8_t* self, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, const float* direction);
// 0x0024bb70: runs the think of every vehicle slot's entity with +0x297 == 2 (G_RunThink,
// entity in ESI; the original pushes its caller's EBX as the argument); 0x0095f684 set meanwhile.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_RunPlayerVehicleThinks(uint32_t ebx);
extern "C" void G_RunPlayerVehicleThinks_entry(void);
// 0x00259d60: each vehicle's collision brush model (Vehicle_FindCollmap of its def name)
// set (SV_SetBrushModel), contents 0x800000 (0xa00000 with spawnflag 1). EAX = the slot
// count, else EAX as it came.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_InitBrushModels(uint32_t eax);
extern "C" void Vehicle_InitBrushModels_entry(void);
// 0x00266470 (use callback): a client in a vehicle (+0xc0 flag 0x100000) gets out, else in.
uint32_t XB_CDECL Vehicle_Use(uint8_t* self, uint8_t* other);

// 0x0025b950 (controller callback): the body (+0x280/+0x288), turret yaw (+0x294) and
// barrel pitch (+0x290) bone angles through 0x002de420 for the tags the vehicle has.
void XB_CDECL Vehicle_Controller(uint8_t* ent);
// 0x00266390 (die callback): stops the effects, puts the driver out, and pushes the wreck
// along the hit direction for bullet and grenade-class weapons.
void XB_CDECL Vehicle_Die(uint8_t* self, uint32_t, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, uint32_t,
                          const float* direction);
// 0x002c2aa0: weapon 0 ("none"), bg_lastWeaponIndex = 0, then the weapon files (0x00024820,
// "WEAPONFILE"). Registered Result::None: EAX is what 0x00024820 leaves (a stack address
// that differs with the frame depth); the caller does not read it.
uint32_t XB_CDECL BG_InitDefaultWeapon();
// 0x0025cc60 (turret in EDI): its user stops using it (client: G_ClientStopUsingTurret;
// actor: Actor_LeaveTurret); turret info byte 0, +0x297 and +0x24c cleared. EAX = the info.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL G_ReleaseTurretUser(uint8_t* turret);
extern "C" void G_ReleaseTurretUser_entry(void);

// 0x00267020 (move in EAX; cdecl ent, pusher, amove): moves an entity with a pusher: by
// the move plus the pusher's rotation (amove) about it; when blocked, tries offsets in
// shells of 4 units up to half its size (+0xf0). Clients/actors turn with the yaw;
// pushed_p (0x0095bef8) advances. Returns 1 when moved (or still free where it was).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_TryPushingEntity(const float* move, uint8_t* ent, uint8_t* pusher,
                                                               const float* amove);
extern "C" void G_TryPushingEntity_entry(void);
// 0x00270f90: the vehicle turret's trigger (+0x2fc: the player driver's attack button) and
// refire delay (+0x184 -= elapsed); fires (Vehicle_FireTurretWeapon) when due.
uint32_t XB_CDECL Vehicle_UpdateTurret(uint8_t* ent, int32_t elapsed);
// 0x00272360 (player in EAX): keeps using the turret (+0x1a0) while it is in use with
// +0x350 > 0 (0x00271b60, player in EBX), else G_ClientStopUsingTurret.
extern "C" PLATFORM_HIDDEN void XB_CDECL Player_UpdateTurretUse(uint8_t* player);
extern "C" void Player_UpdateTurretUse_entry(void);

// 0x00027dc0: copies a vehicle path state (EAX = source, EBX = destination; returns the destination).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Vehicle_CopyPathState(const uint8_t* src, uint8_t* dst);
extern "C" void Vehicle_CopyPathState_entry(void);
// 0x00027e60: copies a vehicle path node (ESI = destination, EDI = source; returns the destination).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL VehiclePathNode_Copy(uint8_t* dst, const uint8_t* src);
extern "C" void VehiclePathNode_Copy_entry(void);

// 0x00027d30: releases the script strings of the path state's two 0x40 entries (EAX = state).
extern "C" PLATFORM_HIDDEN void XB_CDECL VehiclePathState_Dtor(uint8_t* state);
extern "C" void VehiclePathState_Dtor_entry(void);

// 0x00231710: rebuilds the vehicle's physics body when flag 2 of +0x450 is set (flags 1 and 2
// cleared, 0x0022d650); with flag 4 the matrix at +0x460 is then copied to (+0x444)->+8.
void XB_THISCALL VehiclePhys_RebuildPendingBody(uint8_t* vehicle);
// 0x0022d650: the vehicle's physics body record (pool 0x008fccf0) and world body; stdcall.
void XB_STDCALL VehiclePhys_CreateBody(uint8_t* vehicle);

// 0x0024b1a0: resets both path nodes of a vehicle path state (+0x38, +0x78) and, when both node
// indices are valid, fills them from g_vehNodes[EAX] and points the state at node_b. Writes its
// own `state` argument slot (the node index).
void XB_REGS1 XB_CDECL Vehicle_SetSwitchNodes(uint32_t node_a, uint8_t* state, uint32_t node_b);
// 0x002257f0: EAX vehicle: its tag matrices to the model's bone matrices.
void XB_REGS1 XB_CDECL Vehicle_RestoreTagMatrices(uint8_t* vehicle);
// 0x00225520: ESI vehicle physics record destroyed and freed.
extern "C" PLATFORM_HIDDEN void XB_CDECL VehiclePhys_Destroy(uint8_t* phys);
extern "C" void VehiclePhys_Destroy_entry(void);

// 0x0022d230: a physics vehicle record: its entity's (+0x154) matrix saved (+0x170), the body's matrix
// (+0x34) with the centre of mass (+0x160) taken off the origin into the entity, the pose set and the
// four wheel bones (0x0098a490) raised to +0x134 + 15.
void XB_STDCALL Vehicle_UpdateFromPhysics(uint8_t* record);
// 0x0022d410: the entity has all four wheel bones and |+0x160| >= 0.5. No callers or stored pointers (dead).
uint8_t XB_CDECL VehiclePhys_HasWheelBones(uint8_t* ent);

// 0x00231550: the four wheel tags (local origins, z = 15), their extent (z 35, x widened by
// 3.5 * the def height +0x1c0) and centre (z raised by the height), the height into *height.
float* XB_CDECL VehiclePhys_GetWheelBounds(uint8_t* ent, float* wheels, float* size, float* center, uint32_t* height);
// 0x00234560: ESI entity: its vehicle's physics record (+0x248 -> +0x300), made and set up when missing.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL VehiclePhys_GetOrCreate(uint8_t* ent);
extern "C" void VehiclePhys_GetOrCreate_entry(void);
// 0x00247ad0: EAX entity, ECX other: both clients (+0x23c), different, the first with +0x29c set:
// its client's +0x100 > 0.
uint32_t XB_REGS3 XB_CDECL G_CheckOtherClientField100(const uint8_t* ent, uint32_t unused_edx, const uint8_t* other);

// 0x00247c80: EDX path state (node +0 int16, t +4, scale +0xc), EBX out: the point a distance
// ahead along the vehicle nodes (next node in the low 14 bits of +0x3c, length +0x38; 10 past the end).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL VehPath_GetLookaheadPos(const uint8_t* state, float* out);
extern "C" void VehPath_GetLookaheadPos_entry(void);
// 0x00247d50: EDI path state: the blend (XMM0: 0, 1, t or 1 - t) between the flags 0x30000000 of
// the node and the next one.
extern "C" PLATFORM_HIDDEN float XB_CDECL VehPath_GetFlagBlend(const uint8_t* state);
extern "C" void VehPath_GetFlagBlend_entry(void);
// 0x00248620: EAX entity, stack means of death: whether the vehicle def takes that damage.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_TakesDamageFromMod(const uint8_t* ent, uint32_t mod);
extern "C" void Vehicle_TakesDamageFromMod_entry(void);
// 0x00248850: EAX entity: a vehicle whose def type (+0x20) is 1.
uint32_t XB_REGS1 XB_CDECL Vehicle_HasDefType1(const uint8_t* ent);
// 0x00248080: ESI path node, EDI vehicle node: names, positions and the low 30 bits of +0x3c copied.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL VehNode_CopyToPathNode(uint8_t* dst, const uint8_t* src);
extern "C" void VehNode_CopyToPathNode_entry(void);
// 0x00249eb0: EAX entity, ECX vector: the body rock (sin(+0x1f4 degrees) * +0x1f0^2) added to x/z
// with the weights +0x1e8 / +0x1ec, all on the x87.
void XB_REGS3 XB_CDECL Vehicle_GetBodyRock(const uint8_t* ent, uint32_t unused_edx, float* v);
// 0x00249f00: XMM0 target, XMM3 current, stack (scale, count): current moved by
// count * 0x0083eb4c * scale of the gap unless that overshoots or the gap is tiny (XMM0 result).
extern "C" PLATFORM_HIDDEN float XB_CDECL Vehicle_ApproachProportional(float target, float current, float scale, int32_t count);
extern "C" void Vehicle_ApproachProportional_entry(void);
// 0x00249f60: XMM0 target, XMM3 current, stack rate: current moved by rate * 0x008ddaa0 toward the
// target unless that overshoots. No callers or stored pointers (dead).
extern "C" PLATFORM_HIDDEN float XB_CDECL Vehicle_ApproachAtRate(float target, float current, float rate);
extern "C" void Vehicle_ApproachAtRate_entry(void);

// 0x00247e60 / 0x00247f70: DI node index: its +0xc / +8 value, or when unset (< 0) one interpolated
// by distance between the nearest set values back (+0x3c bits 14-27) and forward (bits 0-13); XMM0.
// The +8 one counts 0 as set. The entries keep XMM6/7 (VehNode_LinkAll reads XMM6 after the call).
extern "C" PLATFORM_HIDDEN float XB_CDECL VehNode_FillUnsetValue0c(int32_t index);
extern "C" void VehNode_FillUnsetValue0c_entry(void);
extern "C" PLATFORM_HIDDEN float XB_CDECL VehNode_FillUnsetValue08(int32_t index);
extern "C" void VehNode_FillUnsetValue08_entry(void);
// 0x002492f0: EDI vehicle path state: cleared (node -1), its two path nodes (+0x38, +0x78) reset.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysBody_Clear(uint8_t* state);
extern "C" void PhysBody_Clear_entry(void);

// 0x00248f70: ESI vehicle node pool (count +0, blocks +2, used in the last block +4, blocks +8): the
// next node slot (a new 0x80-node block when full), entered in g_vehNodes.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL VehNode_AllocSlot(uint8_t* pool);
extern "C" void VehNode_AllocSlot_entry(void);
// 0x00249010 (SV_MapRestart): the pool's blocks freed (each node's three names released), pool cleared.
uint32_t XB_STDCALL VehNodePool_Free(uint8_t* pool);
// 0x002490c0: a new reset vehicle node from the pool at 0x00988520; g_numVehNodes counts it.
uint8_t* XB_CDECL VehNode_Alloc();

// 0x00249160: CX node index, EDI vehicle path state: the state set at that node (its values, flag
// blend, positions) and its two path nodes reset.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL VehPath_InitAtNode(uint32_t index, uint8_t* state);
extern "C" void VehPath_InitAtNode_entry(void);
// 0x0024bec0: ECX entity: its origin/angles into the vehicle (+0xd0, +0xf0), then the vehicle's first
// 0xb8 bytes to 0x009883b0 and +0xc0.. (0xb0) to 0x00988470.
uint8_t* XB_THISCALL Vehicle_LoadPhysicsState(uint8_t* ent);

// 0x0024b350: ECX = out angles, state on the stack: blends the path node's angles (+0x2c) toward the next
// node's (or from/to the current out angles when only one of them has angles, flag bits 0x30000000) by the
// state's fraction (+4), each wrapped to the short angle and back.
void XB_REGS3 XB_CDECL VehPath_GetNodeAngles(uint32_t unused_eax, uint32_t unused_edx, float* out, const uint8_t* state);
// 0x0024b530: EAX = script string of a targetname, optional origin on the stack: AX = the first vehicle node with
// that name (and that origin, +0x14), else -1.
uint32_t XB_REGS1 XB_CDECL VehNode_FindByTargetname(const uint32_t* name, const float* origin);
// 0x0024bd40: EAX = vehicle entity, elapsed ms: turn rate (+0x2a0, previous in +0x270) from the yaw change,
// clamped to +-60, and the wheel roll angle (+0x1c4) from the ground speed.
void XB_REGS1 XB_CDECL Vehicle_UpdateTurnRateAndWheelAngle(uint8_t* ent, int32_t elapsed);
// 0x0024bfd0: ECX = entity: the vehicle's motion record (+0xc0) from the entity origin/angles, rates zeroed.
uint8_t* XB_THISCALL Vehicle_ResetMotionState(uint8_t* ent);
// 0x0024f5f0: every live vehicle entity's cached bounds (+0x430/+0x440) back to +-FLT_MAX.
uint32_t XB_CDECL Vehicle_ResetCachedBounds();
// 0x0024fef0: EAX = player: configstrings 13-15 = the looked-at actor's name, health fraction and weapon name.
void XB_REGS1 XB_CDECL Player_UpdateLookAtActorInfo(uint8_t* ent);
// 0x00250960: runs the client's pending touch (+0x628 handle) once its delay has passed; 1 when a touch ran.
uint32_t XB_CDECL ClientThink_RunPendingTouch(uint8_t* ent);

// 0x00249450 (state in EAX, direction in EBX; entity, keep-values byte, stop node on the stack): moves the path
// state's node on while the position (+0x14) is behind it or past the next node along the direction (far jumps
// only for def type 3), sets the segment fraction (+4), end flag (+2), interpolated values (+8 unless kept, +0xc)
// and flag blend (+0x10). EAX = 1 when the stop node was reached.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL VehPath_UpdateSegment(uint8_t* state, const float* dir, const uint8_t* ent,
                                                                   uint8_t keep_values, int32_t stop);
extern "C" void VehPath_UpdateSegment_entry(void);
// 0x002496c0 (node index in BX, out on the stack): the node's angles (+0x2c), or for unset ones (sentinel
// 0x008db094) the nearest set angles before and after it on the path, blended by distance (0 when neither).
extern "C" PLATFORM_HIDDEN void XB_CDECL VehNode_FillUnsetAngles(uint32_t ebx, float* out);
extern "C" void VehNode_FillUnsetAngles_entry(void);
// 0x00251940: one path step of `elapsed` ms: pending node copies (+0x38/+0x78), lookahead (+0x2c), heading
// (+0x20, short-rounded), position advanced by speed (+8), then VehPath_UpdateSegment and the node angles.
uint32_t XB_CDECL Vehicle_PathStep(const uint8_t* ent, uint8_t* state, uint8_t keep_values, int32_t elapsed, int32_t stop);
// 0x00251c10 (G_InitGame): links the vehicle nodes (next = target, prev = whoever targets us), segment directions
// and lengths, filled-in speeds/values/angles; nodes without speed end the path.
uint32_t XB_CDECL VehNode_LinkAll();
// 0x002520f0 (point in ECX, optional out in EBX): 1 when the point is in mount contents (0x400000, out = 1 /
// 0x1000000) or in a trigger_mount (out = its +0x2a0 bit 0) or a non-actor entity with those contents.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Player_FindMountTrigger(const float* point, int32_t* out);
extern "C" void Player_FindMountTrigger_entry(void);
// 0x00252530 (elapsed in EAX, entity on the stack): path-driven vehicle motion: speed toward the target, path steps
// of 50 ms, smoothed angles, velocities (+0x100/+0x110), reached_wait_node / reached_end_node /
// reached_wait_speed notifies, throttle (+0x240/+0x23c).
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_MoveAlongPath(int32_t elapsed, uint8_t* ent);
extern "C" void Vehicle_MoveAlongPath_entry(void);
// 0x002529c0 (box corners in ECX / EAX; entity, pad, contents, out triangle and brush lists, out record on the
// stack): world candidates from the entity's cached lists (regathered 50 wider when the padded box leaves the
// cache), and entities in the padded box into the record (count +0, mins +0x10, maxs +0x20, list +0x30, max 0x80).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Vehicle_GatherCollisionCandidates(const float* a, const float* b, uint8_t* ent,
                                                                             float pad, uint32_t contents,
                                                                             uint8_t* out_triangles, uint8_t* out_brushes,
                                                                             uint8_t* out);
extern "C" void Vehicle_GatherCollisionCandidates_entry(void);

// 0x002538f0 (player touch callback; self, other): pushes the player off (touch times +0x620/+0x624); an actor
// standing on its claimed node near the player hands it over, and a non-enemy actor remembers the player (+0x1390).
void XB_CDECL Player_Touch(uint8_t* self, uint8_t* other);
// 0x00253ed0: tank tread texture scroll (+0xa0, ms) from the speed along the heading; plus the angular rate +0x114.
void XB_CDECL Vehicle_UpdateTreadScroll(uint8_t* ent, int32_t elapsed);
// 0x00259090 (out in EDI, player on the stack): origin + view height, the lean offset, then the view offset
// (client +0x5c8).
extern "C" PLATFORM_HIDDEN void XB_CDECL Player_GetViewOrigin(float* out, const uint8_t* ent);
extern "C" void Player_GetViewOrigin_entry(void);
// 0x00255220 (entity in EAX; origin, angles): unless already there (and +0xd0 set), trajectories from the old
// origin/angles to the new, relinked, and the two attached entities (vehicle +0x204/+0x208) placed there too;
// floats on water for +0x2a4 bit 0x20000000. Returns 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_SetOriginAndAngles(uint8_t* ent, const float* origin, const float* angles);
extern "C" void Vehicle_SetOriginAndAngles_entry(void);
// 0x0025a600 (vehicle record in EAX, entity in ECX, def index on the stack): clears the record, placement, and the
// wheel radius (+0x1c0, 15 or the model's tag_wheel_front_left +0x68).
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_InitRecord(uint8_t* vehicle, uint8_t* ent, uint32_t def_index);
extern "C" void Vehicle_InitRecord_entry(void);
// 0x0025a810 (entity in EBX; tag slot, out on the stack): the tag's skeleton origin (tags 0x009886f8, names
// 0x008daea0; __break 1 when missing); with +0x297 == 2 clamped to the def's radius (+0x1b0) less 2. EAX = bone.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Vehicle_GetTagOrigin(uint8_t* ent, int32_t slot, float* out);
extern "C" void Vehicle_GetTagOrigin_entry(void);
// 0x0025b780: the player's view axis (client +0x5c0/+0x5c4, roll +0xa4) at out, view origin at out + 0x24.
void XB_CDECL Player_GetViewOriginAndAxis(const uint8_t* ent, float* out);
// 0x0025b8a0 (def in ECX, entity on the stack): wheel bone angles (roll +0x1c4; steering +0x270 on the two
// front wheels 0x0095f658 / 0x008d9d58), 4 or 6 wheels.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_UpdateWheelBones(const uint8_t* def, uint8_t* ent, uint32_t entry_esp);
extern "C" void Vehicle_UpdateWheelBones_entry(void);
// 0x00254060 (entity in EAX; usercmd, elapsed ms): stick to steering (0x0095f680, yaw +0xe4) and speed (velocity
// +0x100) for a driven vehicle; 0x0095bba4 set = direct steering/throttle.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_PlayerInputToMove(uint8_t* ent, const int8_t* cmd, int32_t elapsed);
extern "C" void Vehicle_PlayerInputToMove_entry(void);
// 0x0025bb60 (vehicle in EAX, player in EDI): seats the player at tag_player facing the gun tag, links them, sets
// the drive mode (client +0x4e4/+0x4e8/+0xc0), notifies player_on_vehicle.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_EnterPlayer(uint8_t* ent, uint8_t* player);
extern "C" void Vehicle_EnterPlayer_entry(void);
extern "C" void Vehicle_EnterPlayer_entry(void);

// 0x0025c8d0 (entity in EDI): tag_body (pitch/roll +0x250/+0x258 plus the rock), tag_turret (yaw +0x264) and
// tag_barrel (pitch +0x260) bone angles.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_UpdateTurretBones(uint8_t* ent);
extern "C" void Vehicle_UpdateTurretBones_entry(void);
// 0x0025daf0 (def index in AX, entity in ECX, vehicle record in EBX): entity fields of a vehicle (think and
// callbacks, contents, weapons from the def's +0x78/+0x98 names), cached-candidate box and lists (+0x430..+0x454),
// model bounds, link, weapon registration.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_SetupEntity(uint32_t def_index, uint8_t* ent, uint8_t* vehicle);
extern "C" void Vehicle_SetupEntity_entry(void);
// 0x00260330 (entity in EAX; def name, keep-model flag): a free vehicle record (g_vehicles), cleared, set up and
// initialized; the collmap brush model unless kept; def type 3 entities listed in 0x00960074 (count 0x00960070).
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_Init(uint8_t* ent, const char* name, uint32_t keep_model);
extern "C" void Vehicle_Init_entry(void);
// 0x0025ca60 (entity in EAX, EDI handed to Vehicle_SlideMove): velocity clipped against the ground normal
// (0x00988580) keeping its speed, then two or three Vehicle_SlideMove passes; good moves saved (+0x308..+0x32c),
// failed ones timed (+0x304).
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_GroundMove(uint8_t* ent, uint32_t edi);
extern "C" void Vehicle_GroundMove_entry(void);
// 0x0025fd40: 15 cooked grenades scattered around origin (random yaw and distance, staggered fuses).
uint8_t* XB_CDECL Weapon_SpawnClusterGrenades(uint8_t* owner, const uint8_t* source, const float* origin);
// 0x00260480 (player in EDI): unlinks the player from their vehicle, clears the drive state, and teleports them to
// tag_detach (or above the origin), stepping outward up to 15 times until a downward trace starts clear.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_ExitPlayer(uint8_t* player);
extern "C" void Vehicle_ExitPlayer_entry(void);
// 0x00266cd0 (entity in EAX; origin, scale, tag on the stack): FXCmd_Type8AtTag with the def's name, value 0x21
// and the transform {origin, identity quaternion, scale}; the effect id or -1.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Vehicle_PlayFxAtTag(uint8_t* ent, const float* origin, uint32_t scale, uint32_t tag,
                                                                uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi);
extern "C" void Vehicle_PlayFxAtTag_entry(void);
// 0x00265a40 (fire record in EBX; owner, spread): a spread trace; a hit in range (not sky, surface flag 4)
// spawns the cluster around the hit point, else the round goes back (Add_Ammo).
extern "C" PLATFORM_HIDDEN void XB_CDECL Weapon_FireClusterAtTrace(uint8_t* fire, uint8_t* owner, uint32_t spread_bits);
extern "C" void Weapon_FireClusterAtTrace_entry(void);
extern "C" void Weapon_FireClusterAtTrace_entry(void);

extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_UpdateTurretAim(uint32_t entry_esp, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                                         uint32_t edi, uint32_t ebp);
extern "C" void Vehicle_UpdateTurretAim_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_TraceGround(uint32_t entry_esp, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                                     uint32_t edi, uint32_t ebp);
extern "C" void Vehicle_TraceGround_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL Player_UpdateLookAtEntity(uint32_t entry_esp, uint8_t* ent, uint32_t ebx, uint32_t edi,
                                                           uint32_t ebp);
extern "C" void Player_UpdateLookAtEntity_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_PlayerDriveThink(uint32_t entry_esp, uint32_t esi, uint32_t edi);
extern "C" void Vehicle_PlayerDriveThink_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_UpdateEngineSound(int32_t elapsed, uint8_t* ent, uint32_t entry_esp);
extern "C" void Vehicle_UpdateEngineSound_entry(void);

// 0x00271b20 (turret in EAX, user in ECX): Turret_FireForUser for the user, else the turret's driver.
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_FireForUserOrDriver(uint8_t* turret, uint8_t* user, uint32_t entry_esp,
                                                            uint32_t edi);
extern "C" void Turret_FireForUserOrDriver_entry(void);
// 0x0026f670 (fire in EAX, attacker in EBX; cdecl target, spread, damage, flags): dead.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Bullet_Fire(uint8_t* fire, uint8_t* attacker, uint32_t entry_esp);
extern "C" void Bullet_Fire_entry(void);
// 0x002704c0 (fire in EAX; cdecl shooter, spread, damage, attacker, flags): a bullet from the fire record.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Weapon_FireBullet(uint8_t* fire, uint32_t entry_esp, uint32_t esi, uint32_t edi);
extern "C" void Weapon_FireBullet_entry(void);
// 0x002726b0 (cdecl client entity handle): Q3's ClientThink.
extern "C" PLATFORM_HIDDEN void XB_CDECL ClientThink(uint32_t entry_esp);
extern "C" void ClientThink_entry(void);

// 0x0026a230 (entity in ESI): frees the vehicle's physics, sounds and linked entities.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_FreePhysics(uint8_t* ent, uint32_t entry_esp);
extern "C" void Vehicle_FreePhysics_entry(void);

// 0x00271870 (cdecl entity, elapsed ms): the vehicle entity's think.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_Think(uint32_t entry_esp, uint32_t edi);
extern "C" void Vehicle_Think_entry(void);

// 0x002697c0 (owner in EAX, fire record in ESI; cdecl spread degrees, fuse): a projectile in the spread cone.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_FireSpreadProjectile(uint8_t* owner, const float* fire, uint32_t entry_esp);
extern "C" void Vehicle_FireSpreadProjectile_entry(void);
// 0x00269a30 (owner in EAX, fire record in ESI; cdecl spread degrees, fuse, flag): a projectile in the spread cone.
extern "C" PLATFORM_HIDDEN void XB_CDECL Weapon_FireProjectile(uint8_t* owner, const float* fire, uint32_t entry_esp);
extern "C" void Weapon_FireProjectile_entry(void);

// 0x00269e30 (cdecl self, other; touch callback): a moving vehicle pushes, hurts or crushes actors and players.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_Touch(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void Vehicle_Touch_entry(void);

// 0x0026e1b0 (cdecl entity; think callback): the vehicle entity's first think.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_ThinkInit(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                   uint32_t ebp);
extern "C" void Vehicle_ThinkInit_entry(void);

// 0x002c0ec0 (out list in ESI, list in EDI; cdecl mins, maxs, mask): the list's entries whose box overlaps mins..maxs.
extern "C" PLATFORM_HIDDEN void XB_CDECL Phys_FilterEntriesInBox_body(uint8_t* out, const uint8_t* list, const float* mins,
                                                                     const float* maxs, uint32_t mask);
extern "C" void Phys_FilterEntriesInBox_entry(void);

// 0x0025d050 (entity in EBX; was FUN_0025d050): a vehicle overlapping another slot's type-2 vehicle gets its
// state (+0x248) reset from the defaults.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_ResetOnVehicleOverlap(uint8_t* ent);
extern "C" void Vehicle_ResetOnVehicleOverlap_entry(void);

// 0x002b9a30 (EAX = all, ECX = key; value, out): the vehicle nodes whose field named by the key
// ("targetname", "target", "origin", "angles", "speed", "lookahead", "script_noteworthy") equals
// the value; with `out` their indices and the count, else the first index (-1: none or unknown key).
int32_t XB_REGS3 XB_STDCALL VehicleNode_FindByKey(uint32_t all, uint32_t unused_edx, const ScriptString* key,
                                                  const ScriptString* value, int32_t* out);
// 0x00266b30 (cdecl ent, elapsed): turret heat (+0x198), overheat sound/notify and smoke effect.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_UpdateOverheat(uint32_t entry_esp, uint32_t ebx, uint32_t esi,
                                                               uint32_t edi, uint32_t ebp);
extern "C" void Vehicle_UpdateOverheat_entry(void);
// 0x00260810 (cdecl ent, elapsed): the driver's fire button, heat, and the aim point traced from the view.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_FireTurret(uint32_t entry_esp, uint32_t ebx, uint32_t esi,
                                                           uint32_t edi, uint32_t ebp);
extern "C" void Vehicle_FireTurret_entry(void);
// 0x00270560 (cdecl ent): one shot (bullet or projectile) from the turret's flash tag toward the aim point.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_FireTurretWeapon(uint32_t entry_esp, uint32_t ebx, uint32_t esi,
                                                                 uint32_t edi, uint32_t ebp);
extern "C" void Vehicle_FireTurretWeapon_entry(void);

// 0x00267e10 (cdecl ent): the use hint, its entity and string for the client (+0x47c/+0x480/+0x484/+0x4c0).
extern "C" PLATFORM_HIDDEN void XB_CDECL Player_UpdateCursorHints(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                                 uint32_t ebp);
extern "C" void Player_UpdateCursorHints_entry(void);
// 0x0026f0a0 (cdecl pass, attacker, start, end, a4, depth, fire, a7, a8): a bullet segment with effects only
// (dead: its one caller Bullet_Fire has none).
extern "C" PLATFORM_HIDDEN void XB_CDECL Bullet_Trace_r(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                       uint32_t ebp);
extern "C" void Bullet_Trace_r_entry(void);
// 0x0026fcb0 (cdecl pass, attacker, start, end, damage, depth, fire, a7, a8): one bullet segment: trace,
// events, effects, damage, penetration and pass-through (recursive).
extern "C" PLATFORM_HIDDEN void XB_CDECL Weapon_BulletTrace_r(uint32_t entry_esp, uint32_t ebx, uint32_t esi,
                                                             uint32_t edi, uint32_t ebp);
extern "C" void Weapon_BulletTrace_r_entry(void);
// 0x00271c30 (cdecl ent): Q3's ClientThink_real (command time, pmove, entity state, triggers, buttons).
extern "C" PLATFORM_HIDDEN void XB_CDECL ClientThink_real(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                         uint32_t ebp);
extern "C" void ClientThink_real_entry(void);
// 0x00264060 (cdecl ent, list of {entity, score}): the usable entities in front of the player, best first.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Player_GetUseList(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi,
                                                              uint32_t ebp);
extern "C" void Player_GetUseList_entry(void);
// 0x00265c10 (EAX fire record; cdecl ent): the melee trace, damage, impact event and rumble.
extern "C" PLATFORM_HIDDEN void XB_CDECL Weapon_MeleeTrace(uint32_t entry_esp, uint8_t* fire, uint32_t ebx, uint32_t esi,
                                                          uint32_t edi, uint32_t ebp);
extern "C" void Weapon_MeleeTrace_entry(void);

// 0x00259df0 (cdecl ent, triangle list, brush list, entity record, point, radius, out; AL): pushes a sphere out of
// the candidates Vehicle_GatherCollisionCandidates found (entity models in their frame, brushes, triangles).
uint8_t XB_CDECL Vehicle_ClipAgainstEntities(uint8_t* ent, uint8_t* triangles, uint8_t* brushes, const uint8_t* record,
                                             const float* point, float radius, float* out);
// 0x0025bd10 (entity in EBX; msec, move, force): three spheres along the vehicle pushed out of the world and
// entities; with `move` the end point becomes the origin, a hard enough push an impulse. Returns the push count.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_SlideMove(uint8_t* ent, int32_t msec, uint32_t move, uint32_t force);
extern "C" void Vehicle_SlideMove_entry(void);
// 0x002611e0 (dead): Q3's PM_SlideMove for a vehicle's motion (ent, gravity flag, msec).
uint32_t XB_CDECL Vehicle_PlaneSlideMove(uint8_t* ent, uint32_t gravity, int32_t msec);

}  // namespace game
