#pragma once

// The client game's entities: local entities (tracers), entity and player-state events.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "title_data.h"



// Calls `function` on the current stack with all six registers from `regs` and `count`
// stack words (args[0] pushed last, popped again); returns its EAX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL cg_call_regs(uint32_t function, const game::TitleRegs* regs,
                                                          const uint32_t* args, uint32_t count);

namespace game {

struct DObj;

// 0x00282040: Q3's CG_AddLocalEntities: every active local entity is freed once its end time
// (+0xc) has come, else added by type (+8: 0 its refEntity +0x3c, 1 near-culled, 2 tracer);
// 0x00972f70 cleared first.
void XB_CDECL CG_AddLocalEntities();
// 0x0027ef60 (entity in EAX): a tracer (CG_Tracer) from the trajectory's position at cg.time
// (+0x10) along its direction (+0x28) - length/width 300/1.2 for flag 0x20 at +0xa, else
// the cvars 0x0094ca78/0x0094bb78 (nothing below 0x0083f02c).
void XB_REGS1 XB_CDECL CG_AddTracerLocalEntity(uint8_t* le);
// 0x0027a000 (start EAX, end ECX, cdecl weapon): a tracer local entity flying from the
// start (less its length 0x0094ca78) to the end at speed 0x0094a330, starting 0-32 ms late.
void XB_REGS3 XB_CDECL CG_SpawnTracer(const float* start, uint32_t unused_edx, const float* end, int32_t weapon);
// 0x0027ab40 (weapon in ESI, cdecl (end, start)): CG_SpawnTracer with chance 0x0094a8e0
// (0.7 for weapon kinds 3/4) in 32.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_MaybeSpawnTracer(int32_t weapon, const float* end, const float* start);
extern "C" void CG_MaybeSpawnTracer_entry(void);
// 0x0028a440 (flags EAX, old flags EDX, old events ECX, new events EDI): Q3's
// CG_CheckPlayerstateEvents onto the player's cent (CG_EntityEvent with EBX 1).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_CheckPlayerstateEvents(uint32_t flags, uint32_t old_flags,
                                                                  const int32_t* ops, const int32_t* ps);
extern "C" void CG_CheckPlayerstateEvents_entry(void);
// 0x0028a510 (cent in ESI): Q3's CG_CheckEvents: the entity's new events (+0xb0 sequence,
// seen +0x420) through CG_EntityEvent.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_CheckEvents(uint8_t* cent);
extern "C" void CG_CheckEvents_entry(void);
// 0x0028a9e0 (cent in ESI): the same through CG_EntityPreEvent (seen +0x424).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_CheckEntityPreEvents(uint8_t* cent, uint32_t ebx);
extern "C" void CG_CheckEntityPreEvents_entry(void);

// 0x00284bf0 / 0x00284ec0 / 0x00284fa0 / 0x002850a0: Q3-style per-type adds of a model entity
// (cdecl cent): the refEntity at the render record's +0x80 (origin +0x60, axis from angles
// +0x70, DObj +0x22c) added to the scene, nothing while flag byte +0x10 has its top bit.
// Vehicle/MG42 light from 32 units above the origin (flag 0x80; a vehicle with flag 0x100000
// and not 0x400 uses flag 0x10), a mover (0x40) also without a DObj when its model is
// 0xffffff, a missile (weapon +3, reset above bg_lastWeaponIndex) the weapon's flags | 0x40.
void XB_CDECL CG_Vehicle(uint8_t* cent);
void XB_CDECL CG_Mover(uint8_t* cent);
void XB_CDECL CG_Missile(uint8_t* cent);
void XB_CDECL CG_MG42(uint8_t* cent);
// 0x0027bc90 (cent EAX, refEntity EDI): with flag 0x8000 the lighting origin kept in the
// render record (+0x88, set from the origin the first time) into ref +8 (flag 0x80); else
// the kept one cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_SetLightingOrigin(uint8_t* cent, uint8_t* ref);
extern "C" void CG_SetLightingOrigin_entry(void);
// 0x0027fac0 (out in ESI, cdecl (in, mover, from, to, delta_angles)): Q3's
// CG_AdjustPositionForMover.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_AdjustPositionForMover(float* out, const float* in, uint32_t mover,
                                                                  int32_t from_time, int32_t to_time,
                                                                  float* delta_angles);
extern "C" void CG_AdjustPositionForMover_entry(void);

// 0x00281fa0: Q3's CG_PointContents(point, passEntityNum, mask) over the world and the
// solid brush entities.
int32_t XB_CDECL CG_PointContents(const float* point, int32_t pass_entity, uint32_t mask);
// 0x002887f0 (cent in EAX, cdecl event): the weapon's empty-click sound.
void XB_REGS1 XB_CDECL CG_FireWeaponEmpty(const uint8_t* cent, int32_t event);

// 0x0001c210 (record in EDX): a render record (0x100 bytes) reset; returns it.
uint8_t* XB_REGS2 XB_CDECL RefEntity_Clear(uint32_t unused_eax, uint8_t* record);
// 0x002be200 (cent in ESI): the cent's render record (+0x250), allocated on first use from
// its client's memory context (+0x220) or the heap; null when that fails.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CG_GetEntityRefEntity(uint8_t* cent);
extern "C" void CG_GetEntityRefEntity_entry(void);
// 0x0027f920 (tag hash EAX, DObj EDI, cdecl out): the tag's world matrix (4x4) through the
// view placement 0x00972fd8; 0 without that tag.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CG_DObjGetTagMatrix(uint32_t tag, DObj* obj, float* out);
extern "C" void CG_DObjGetTagMatrix_entry(void);

// 0x00284db0 / 0x00285190 (cdecl cent): a script mover (flag 0x40; also without a DObj
// when its model is 0xffffff, lit from CG_SetLightingOrigin otherwise) and a general model
// entity (lit from CG_SetLightingOrigin), as CG_Vehicle.
void XB_CDECL CG_ScriptMover(uint8_t* cent);
void XB_CDECL CG_General(uint8_t* cent);
// 0x002821e0: Q3's CG_CalcEntityLerpPositions(cent).
void XB_CDECL CG_CalcEntityLerpPositions(uint8_t* cent);
// 0x00288dc0 (weapon EAX, alt ECX, source ESI): the weapon's fire sound (def +8, kind 1 or
// 2) and FXCmd_Type9(2), each with EAX = source.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_PlayWeaponFireSound(int32_t weapon, uint32_t alt, uint32_t source);
extern "C" void CG_PlayWeaponFireSound_entry(void);

// 0x0027bb70 (cent in EAX): a portal surface entity.
void XB_REGS1 XB_CDECL CG_Portal(uint8_t* cent);
// 0x00288510 (cent in ESI): the weapon flash timer (+0x41c) counted down with the weapon's
// sound kind 0xb while it runs and 0xc when it ends.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_UpdateFlashSound(uint8_t* cent);
extern "C" void CG_UpdateFlashSound_entry(void);
// 0x00289170 (cdecl cent): an actor model entity plus CG_UpdateFlashSound.
void XB_CDECL CG_Actor(uint8_t* cent);

// 0x00286950: Q3's CG_ClipMoveToEntities(start, mins, maxs, end, skip, mask, capsule, tr); the
// C version also gets the entry ESP (CG_ClipMoveToEntities_entry).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_ClipMoveToEntities(const float* start, const float* mins, const float* maxs,
                                                              const float* end, int32_t skip, uint32_t mask,
                                                              uint32_t capsule, uint8_t* tr, uint32_t entry_esp);
extern "C" void CG_ClipMoveToEntities_entry(void);
// 0x00286c60: Q3's CG_Trace(result, start, mins, maxs, end, skip, mask), capsule entity boxes.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_Trace(uint8_t* result, const float* start, const float* mins,
                                                 const float* maxs, const float* end, int32_t skip, uint32_t mask,
                                                 uint32_t entry_esp);
extern "C" void CG_Trace_entry(void);
// 0x00286d00 (mask EAX, end ECX, cdecl (result, start, mins, maxs, skip)): as CG_Trace
// without capsules.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_TracePoint(uint32_t mask, const float* end, uint8_t* result,
                                                      const float* start, const float* mins, const float* maxs,
                                                      int32_t skip, uint32_t entry_esp);
extern "C" void CG_TracePoint_entry(void);

// 0x0027eb80 (yaw byte EAX, pitch byte ECX, cdecl damage): Q3's CG_DamageFeedback plus the
// damage indicators and a controller rumble.
void XB_REGS3 XB_CDECL CG_DamageFeedback(int32_t yaw_byte, uint32_t unused_edx, int32_t pitch_byte, int32_t damage);
// 0x0028abd0: Q3's CG_TransitionSnapshot.
void XB_CDECL CG_TransitionSnapshot();

// 0x00285e70 (cent in EDI): an item entity (registered on first sight).
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_Item(uint8_t* cent);
extern "C" void CG_Item_entry(void);
// 0x0028a250 (cent in EAX): Q3's CG_AddCEntity.
void XB_REGS1 XB_CDECL CG_AddCEntity(uint8_t* cent);
// 0x0028a760: Q3's CG_AddPacketEntities.
void XB_CDECL CG_AddPacketEntities();

// 0x002860f0 (out in EDI, cdecl (handle, tag)): the eye or tag position of an entity.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CG_GetEntityEyePosition(float* out, uint32_t handle, const char* tag);
extern "C" void CG_GetEntityEyePosition_entry(void);
// 0x00288620 (end EAX, start ESI, cdecl shooter handle): the bullet whiz-by sound.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_BulletWhizBy(const float* end, const float* start, uint32_t handle);
extern "C" void CG_BulletWhizBy_entry(void);
// 0x00289270 (tag in EAX, cdecl (handle, end, weapon)): tracer and whiz-by of a bullet.
void XB_REGS1 XB_CDECL CG_BulletTracerAndWhizBy(const char* tag, uint32_t handle, const float* end, int32_t weapon);
// 0x0028a370 (start EAX, cent ECX, weapon EDI, cdecl (end, extra)): a bullet's impact
// effect, tracer and whiz-by.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_Bullet(const uint32_t* start, uint8_t* cent, int32_t weapon,
                                                  const uint32_t* end, uint32_t extra, uint32_t entry_esp);
extern "C" void CG_Bullet_entry(void);

// 0x00285e00 (name in EBX): every weapon's view hands set to `name` where they differ.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_SetViewHandsModel(const char* name);
extern "C" void CG_SetViewHandsModel_entry(void);
// 0x0028aaa0 (snapshot in EAX): Q3's CG_SetNextSnap.
extern "C" PLATFORM_HIDDEN void XB_CDECL CG_SetNextSnap(uint8_t* snap);
extern "C" void CG_SetNextSnap_entry(void);

}  // namespace game
