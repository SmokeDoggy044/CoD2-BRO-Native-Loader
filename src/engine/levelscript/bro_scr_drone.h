#pragma once

// Compiled level-script helpers of the drone paths (levelscript module, .text): spline files per
// drone entity. Names are ours (from the bodies).

#include <cstdint>

#include "bro_scr_util.h"
#include "compiler.h"

namespace game {

// 0x001478f0: "drones_spline_name" set to "all" when empty. Unless create_spline_files is set, the
// level flag at +0x76c is set or the drone already has points (array field 0xb89aec99) or a path
// (field 0x6bd99abb): the spline of its targetname is loaded (interface slot 0x009223a8 into the
// fields 0x6bd99abb, 0x384ad510, 0x095a5675); with a path, field 0x19738af8 = 1, else a "Spline
// File Not Found" message is built and dropped. *out = an empty 12-byte array. The second
// argument is unused. Returns out.
uint32_t* XB_CDECL BroScr_DroneSplineMath(uint32_t* out, uint32_t unused, uint32_t ent);
// 0x0014bfd0 (args: unused, a by-value entity array, a string; all released): only while the level
// counter +0x2e4 is 0 (undefined = 0; counted up either way, reset to 0 at the end):
// BroScr_DroneSplineMath for each drone (the "<targetname>_<i>.spl" / "<first>.spl" names are built
// and dropped).
BRO_SCR_ARGS_DECL(BroScr_BuildDroneSplines)
// 0x001525a0 (args: unused, a by-value array of group names, a string; all released): for each
// name with entities (by targetname), BroScr_BuildDroneSplines(level entity, those entities, a copy
// of the string).
BRO_SCR_ARGS_DECL(BroScr_BuildDroneSplinesForEntArrays)

}  // namespace game
