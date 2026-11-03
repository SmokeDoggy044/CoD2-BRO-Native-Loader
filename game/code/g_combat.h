#pragma once

// Game module: damage rules (triggers that take damage, actors' lethal-damage clamp).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002481d0 (means of death in EAX, trigger in ECX): 0 when the trigger's spawn flags (+0x2a0)
// exclude the means of death, else 1.
uint32_t XB_REGS3 XB_CDECL TriggerDamage_AcceptsMeansOfDeath(int32_t mod, uint32_t unused_edx, const uint8_t* trigger);
// 0x0024add0 (damage in EAX, entity in ECX): an actor's (+0x240) damage through
// Actor_ClampLethalDamage; 0 for entities without one. Unhookable (RET in the first 5 bytes).
uint32_t XB_REGS3 XB_CDECL G_ActorClampLethalDamage(uint32_t damage, uint32_t unused_edx, const uint8_t* ent);

// 0x0024c5e0 (args: entity, ...): pain callback: once level.time passes the last time
// (+0x384) + random 0..999 + +0x38c + 500, the last time becomes level.time. Keeps level.time
// as a float in its first argument slot.
extern "C" PLATFORM_HIDDEN void XB_CDECL Pain_Func4(uint32_t* args);
extern "C" void Pain_Func4_entry(void);

// 0x0024f520: think every frame (nextthink = level.time + 1) until level.time passes the free
// time (+0x384, float), then G_FreeEntity (tail jump: the caller's second word passes on).
extern "C" PLATFORM_HIDDEN void XB_CDECL hurt_think(const uint32_t* args);
extern "C" void hurt_think_entry(void);

// 0x00250170 (dir in EAX, target in ECX, damage in EBX): the end of Q3's G_Damage - damage
// record, health, "damage"/"death" notifies, pain (+0x340) or die (+0x344) callbacks.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_ApplyDamage(const float* dir, uint8_t* targ, int32_t damage, uint8_t* inflictor,
                                                       uint8_t* attacker, const float* point, int32_t mod, int32_t weapon,
                                                       int32_t hitloc);
extern "C" void G_ApplyDamage_entry(void);
// 0x00250340 (point in EAX; cdecl targ, inflictor, attacker, dir, damage, dflags, mod, hit
// location, weapon): Q3's G_Damage. The C version gets the argument block (it writes damage,
// inflictor, attacker and hit location there, as the original).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_Damage(const float* point, uint32_t* args);
extern "C" void G_Damage_entry(void);
// 0x00250b70: pain callback - health back to 99999, the damage to the owner (handle +0x1a0).
void XB_CDECL Pain_ForwardToOwner(uint8_t* self, uint8_t* attacker, int32_t damage, const float* point, int32_t mod,
                                  const float* dir, int32_t hitloc);
// 0x00251700: die callback - Pain_ForwardToOwner at the entity's origin.
void XB_CDECL Die_ForwardToOwner(uint8_t* self, uint8_t* inflictor, uint8_t* attacker, int32_t damage, int32_t mod,
                                 int32_t weapon, const float* point, const float* dir, int32_t hitloc);

// 0x002522a0 (self in ESI, damage in EDI; cdecl attacker, mod): trigger_damage's activation.
extern "C" PLATFORM_HIDDEN void XB_CDECL Activate_trigger_damage(uint8_t* self, int32_t damage, uint8_t* attacker,
                                                                 int32_t mod);
extern "C" void Activate_trigger_damage_entry(void);
// 0x00252290: touch callback - G_Trigger.
void XB_CDECL Touch_TriggerNotify(uint8_t* self, uint8_t* other);
// 0x002523d0: trigger_hurt's touch (G_Damage every 100 ms / 1 s).
void XB_CDECL hurt_touch(uint8_t* self, uint8_t* other);
// 0x00252490: touch callback - friendly chain node for the toucher's sentient, G_Trigger.
void XB_CDECL Touch_FriendlyChain(uint8_t* self, uint8_t* other);
// 0x002524d0: trigger_multiple's touch - G_Trigger, multi_trigger.
void XB_CDECL Touch_Multi(uint8_t* self, uint8_t* other);

// 0x00253e30: trigger_damage's die callback: Activate_trigger_damage, health reset to 32000 without a total.
void XB_CDECL Die_trigger_damage(uint8_t* self, uint8_t* inflictor, uint8_t* attacker, int32_t damage, int32_t mod,
                                 int32_t weapon, const float* point, const float* dir, int32_t hitloc);
// 0x00253e70: trigger_damage's pain callback, as Die_trigger_damage.
void XB_CDECL Pain_trigger_damage(uint8_t* self, uint8_t* attacker, int32_t damage, const float* point, int32_t mod,
                                  const float* dir, int32_t hitloc);
// 0x00253eb0: trigger_damage's use callback: activates with the full damage total.
void XB_CDECL Use_trigger_damage(uint8_t* self, uint8_t* other, uint8_t* activator);

// 0x0025d7d0: trigger_damage entities (with +0x2a4 bit 0x40000) in the box of the missile's
// move that a sight trace from start to end reaches: "damage" notify and the activation.
void XB_CDECL G_MissileActivateDamageTriggers(uint8_t* ent, const float* start, const float* end, int32_t damage,
                                              int32_t mod);
// 0x0025d960: the same for a hit trace (melee, bullets, missile impact), any trigger_damage.
void XB_CDECL G_CheckHitTriggerDamage(uint8_t* ent, const float* start, const float* end, int32_t damage, int32_t mod);

// 0x002691a0 (means of death in EAX, entity in ECX, cdecl weapon): the dead player's state, the local player's
// death shake and rumble, the "player died" interface notice.
void XB_REGS3 XB_CDECL G_PlayerDeathSetup(uint32_t mod, uint32_t unused_edx, uint8_t* self, uint32_t weapon);

}  // namespace game
