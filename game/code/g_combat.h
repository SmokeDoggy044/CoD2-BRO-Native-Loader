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

}  // namespace game
