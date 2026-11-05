#pragma once

// Compiled level-script halftrack crews (levelscript module, .text 0x0016e270-0x0016fa40): guys
// force-spawned into a halftrack's passenger seats. Both wait (Scr_WaitFrames, Scr_Wait): class
// thread_switch, kept out of kFunctions until script threads run as coroutines. Names are ours.

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0016e270 (was FUN_0016e270): the halftrack's passenger (array field 0xe2a39a33) whose string
// 0xd91e5c5b equals `tag`, and its index in the level's passenger list (+0x4ec); `guy` teleported
// to the tag (origin / angles), BroScr_VehicleGetPassengers(halftrack, guy, tag, seat), entity
// flag 0x400 cleared, one frame; the old passenger deleted and taken off the level list, and off
// the halftrack's array (unless at index 0). Releases `tag`.
void XB_CDECL BroScr_HalftrackSeatGuy(uint32_t halftrack, uint32_t guy, ScriptString tag, int32_t seat);
// 0x0016f920: for seats 1-5: a guy force-spawned from `spawner`, targetname "halftrackguy_<n>",
// entity flag 0x400, added to the level's list (+0x4f8), BroScr_HalftrackSeatGuy at
// "tag_passenger<n>"; then 0.1 s and BroScr_HalftrackAttachPassengers for seats 5 down to 1.
void XB_CDECL BroScr_HalftrackSpawnGuys(uint32_t halftrack, uint32_t spawner);

}  // namespace game
