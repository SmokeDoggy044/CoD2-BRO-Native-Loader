#pragma once

// Compiled level-script helpers of the automated flak guns (levelscript module, .text): target
// search, lead position, low-angle check, and the random pitch step of rotating props. Names are
// ours (from the bodies).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0012fa80: the lead for `gun` on `target`: velocity = forward(target angles) * speed (slot
// 0x00922b38; all zero when undefined), time = distance(gun, target) / gun's int field 0x93c3cf6c,
// a random 0..64 - 64 spread per lane times the time. The gun's lead_pos field = target origin +
// velocity + spread; *out = velocity + spread (z through the x87). Returns out.
float* XB_CDECL BroScr_FlakPickLeadPos(float* out, uint32_t gun, uint32_t target);
// 0x0012ff60: *out = whether `gun` can shoot at its lead_pos: the lead direction within 0.94 (dot)
// of the "tag_flash" forward, and either the can_shoot_low field set or the lead higher than the
// gun by more than the float at 0x008cd170 (x87 difference, truncated). Returns out.
uint8_t* XB_CDECL BroScr_FlakCanShootLow(uint8_t* out, uint32_t gun);
// 0x00136c80: *out = the living member of `gun`'s target group (string field 0x46861906) within its
// range (int field 0x93c3cf6c, squared) that is nearest the "tag_flash" forward (largest dot, above
// -90), low targets only with can_shoot_low; 0 when none. Returns out.
uint32_t* XB_CDECL BroScr_FlakFindTargets(uint32_t* out, uint32_t gun);
// 0x00132da0: ends on "death"; the pitch field starts at the entity's pitch (then a discarded
// Scr_RandomFloat(5)); below 80 it grows by step * 40, capped at 80.
void XB_CDECL BroScr_RandomRotateStep(uint32_t ent, float step);

}  // namespace game
