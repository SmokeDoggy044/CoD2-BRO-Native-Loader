#pragma once

// Compiled level-script anim reach threads (levelscript module, .text 0x00168910-0x0016d0c0): guys
// sent to the start of a scripted anim (goal = the anim's start offset from a tag, node or entity),
// and the restore after an AnimScripted. Every one waits (Scr_WaitTill*): class thread_switch, kept
// out of kFunctions until script threads run as coroutines. Names are ours (from the bodies). The
// anim tables are level + 0x19c (per animname hash) -> anime key -> anim (pair word +0xc).

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00168910: with `tag` set, origin / angles = the tag's on `tag_ent` (on `ent` when invalid).
// Spawns a script_origin (at the player's origin); per guy: goal pos = GScr_TransformLocalOffset of
// origin / angles by the guy's anim `anime`, a BroScr_AnimReachGuyThread(guy, origin ent) thread;
// waits for 0x2253e8ee on the origin once per guy, goal radius of each = its float key 0x25bf26c6;
// deletes the origin. Frees `guys`, releases `tag`.
void XB_CDECL BroScr_AnimReachCore(uint32_t ent, DynArray guys, uint32_t anime, ScriptString tag, ScrVec3 origin,
                                   uint32_t tag_ent, ScrVec3 angles);
// 0x0016ce40: BroScr_AnimReachCore with the origin / angles of path node `node` (when not 0 /
// 0xffffffff) or of `ent`. 0x0016cf80: the same with entity `node` (when valid).
void XB_CDECL BroScr_AnimReach(uint32_t ent, DynArray guys, uint32_t anime, ScriptString tag, uint32_t node,
                               uint32_t tag_ent);
void XB_CDECL BroScr_AnimReachEnt(uint32_t ent, DynArray guys, uint32_t anime, ScriptString tag, uint32_t node,
                                  uint32_t tag_ent);
// 0x0016aa90: ends on 0x78dbe561 / death of both; waits for 0x0d6e32cd on `guy`, stops its
// AnimScripted and unlinks it, then AnimScriptedNoNotify(guy, 0x54e3b2b6) of its anim 0x140aaa8f at
// `tag` on `ent` and waits for it; the animname back from oldanimname (when set); notifies
// 0x996a3647. Releases `tag`.
void XB_CDECL BroScr_AnimScriptedRestoreAnimname(uint32_t ent, uint32_t guy, ScriptString tag);

}  // namespace game
