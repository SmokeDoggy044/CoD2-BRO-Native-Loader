#pragma once

// Compiled level-script mortar groups and mortar impacts.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "qcommon_fs.h"

namespace game {

// 0x00173040: defaults f1 = 400, f2 = 1200 (undefined float), i = 1 (undefined int);
// BroScr_MortarEntInit on each entity of `ents`, then a BroScr_MortarGroupNearEntLoop thread on the
// level object's word (+4) with pointers to all arguments (the values after the defaults). Frees
// both arrays.
void XB_CDECL BroScr_StartMortarGroupNearEnt(uint32_t ent, DynArray ents, float f1, float f2, uint32_t handle,
                                             uint32_t w, uint32_t i, DynArray extra);
// 0x001728b0: per entity of `ents`: a BroScr_MortarEntInit thread, then a BroScr_MortarLoopNearPlayer
// thread (functor {0x00824488, function, ent, a4..a7}). `self` and the last word are unused. Frees
// `ents`.
void XB_CDECL BroScr_StartMortarLoopsNearPlayer(uint32_t self, DynArray ents, uint32_t a4, uint32_t a5, uint32_t a6,
                                                uint32_t a7, uint32_t unused);
// 0x0016fa40: radius / max / min damage default to the level maps (+0x65c / +0x690 / +0x7a0) value
// for `type`, else 300 / 400 / 25; GScr_RadiusDamage at the entity's origin, then
// BroScr_MortarThink(ent, origin, a4, a5, a6, type).
void XB_CDECL BroScr_MortarThinkWithDefaults(uint32_t ent, uint32_t radius, uint32_t max_damage, uint32_t min_damage,
                                             uint32_t a4, uint32_t a5, uint32_t a6, uint32_t type);

// 0x0016e760: a mortar hit at `origin`: earthquake scale (the level's +0x29c override, else the
// argument, else the level map by type or 0.45), duration (2) and radius (850); the effect =
// field 0xe5d3774f, else the level's per-type effect (+0x488), else its default (+0x6e8). No
// effect: a debug print "No FX!" thread. With a valid target entity (target -> GetEntArray): the
// effect along the direction to it (PlaySoundAtPosition); else PlaySound(0xbd1fe198) and notify
// 0x7652bc7c on the level; "Boom!" debug print with debug_mortars 1. Then: deletes the ents of
// array 0x05c4ceb7 when field 0xb2f8556f is set, clears flag 0x400 of the linked ent 0x28667a5b,
// runs BroScr_Exploder for script_exploder, starts BroScr_StukaStartNearbyDrones and
// GScr_Earthquake. The entity's argument slot ends with a stack address, as the original.
void XB_CDECL BroScr_MortarThink(uint32_t ent, ScrVec3 origin, uint32_t scale, uint32_t duration, uint32_t radius,
                                 uint32_t type);

}  // namespace game
