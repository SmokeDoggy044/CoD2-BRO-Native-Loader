#pragma once

// Compiled level-script anim helpers: one-line wrappers around the engine's anim slots
// (GScr_SetAnim / SetAnimKnob / SetAnimKnobAll / SetFlaggedAnimKnob(All)) and notifies.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001836a0 / 0x001836c0: Scr_QueueNotify(entity, 0x281d9462); returns 0.
uint32_t XB_CDECL Bro_anim_exit_cover_left(uint32_t entity);
uint32_t XB_CDECL Bro_anim_exit_cover_right(uint32_t entity);

// 0x001827d0 / 0x00182800 / 0x001828a0: SetAnimKnob(entity, anim 0x008cf684, 1.0, 0.15, 0, 1).
void XB_CDECL BroScr_TurretSetAnimKnob_8cf684(uint32_t a1, uint32_t entity);
void XB_CDECL BroScr_TurretSetAnimKnob_8cf684_2(uint32_t a1, uint32_t entity);
void XB_CDECL BroScr_TurretSetAnimKnob_8cf684_3(uint32_t a1, uint32_t entity);
// 0x00183aa0 / 0x00183a70: SetAnimKnob(entity, anim 0x008cf7a4 / 0x008cf7bc, 1.0, 0.2, 1.0, 1).
void XB_CDECL BroScr_TurretSetAnimKnob_8cf7a4(uint32_t a1, uint32_t entity);
void XB_CDECL BroScr_TurretSetProneRecoverAnim(uint32_t a1, uint32_t entity);

// 0x00184050: SetAnimKnobAll(entity, prone_legsstraight, Body, 1.0, time, 1.0, 0).
void XB_CDECL BroScr_SetProneLegsStraightKnob(uint32_t entity, uint32_t time);

// 0x00183220 (8 words: entity, flag name, three anims, weight, time, rate): SetAnimKnob(a3),
// SetFlaggedAnimKnob(a2, a4), SetAnimKnob(a5), each with (a6, a7, a8, 0).
void XB_CDECL BroScr_SetAimAnimKnobs(uint32_t entity, uint32_t flag, uint32_t anim1, uint32_t anim2, uint32_t anim3,
                                     uint32_t weight, uint32_t time, uint32_t rate);

// Anim command handlers (8 words; a2 and some others unused): 0x00183ce0 SetAnim(a1, a3, a6, a7, a8, 0),
// 0x00183d10 SetAnimKnobAll(a1, a3, a4, a6, a7, a8, 0), 0x00183d40 SetFlaggedAnimKnobAll(a1, a5, a3, a4,
// a6, a7, a8, 0).
void XB_CDECL BroScr_AnimCmd_SetAnim(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                     uint32_t a7, uint32_t a8);
void XB_CDECL BroScr_AnimCmd_SetAnimKnobAll(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                            uint32_t a6, uint32_t a7, uint32_t a8);
void XB_CDECL BroScr_AnimCmd_SetFlaggedAnimKnobAll(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                                   uint32_t a6, uint32_t a7, uint32_t a8);

}  // namespace game
