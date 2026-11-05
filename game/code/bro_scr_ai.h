#pragma once

// Compiled level-script AI helpers (actor state kept in the anim state object at 0x00922efc).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Per-team values of the anim state object: four arrays of four words, one per team hash of
// the entity's team ("axis" +0x1c, "all" +0xf8, "neutral" +0xd0, "allies" +0x74); the suffix is
// the word's offset in the "neutral" array (d0/d4/d8/dc = index 0-3).
// Get (0x00182b80 d0, 0x00182c90 d4, 0x00182950 d8, 0x00182a60 dc): *out = the value, 0 for
// another team; returns out.
uint32_t* XB_CDECL BroScr_GetPerTeamValue_d0(uint32_t* out, uint32_t entity);
uint32_t* XB_CDECL BroScr_GetPerTeamValue_d4(uint32_t* out, uint32_t entity);
uint32_t* XB_CDECL BroScr_GetPerTeamValue_d8(uint32_t* out, uint32_t entity);
uint32_t* XB_CDECL BroScr_GetPerTeamValue_dc(uint32_t* out, uint32_t entity);
// Set (0x00182f80 d0, 0x00182da0 d4, 0x00182e90 d8, 0x00183070 dc): the value for the entity's
// team; nothing for another team.
void XB_CDECL BroScr_SetPerTeamValue_d0(uint32_t entity, uint32_t value);
void XB_CDECL BroScr_SetPerTeamValue_d4(uint32_t entity, uint32_t value);
void XB_CDECL BroScr_SetPerTeamValue_d8(uint32_t entity, uint32_t value);
void XB_CDECL BroScr_SetPerTeamValue_dc(uint32_t entity, uint32_t value);

}  // namespace game
