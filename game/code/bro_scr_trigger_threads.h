#pragma once

// Compiled level-script trigger and turret threads (levelscript module, .text, thread-switch push
// c): trigger thinks that wait for touches, mg42 users, grenade throws, fallbackers. Every one
// reaches a script wait: class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x001bf200 (functor target): the trigger's target node ("targetname"; -1 as 0) and that node's
// target turret (mode 0xad76f051, no target). On each trigger 0xe2a210cf by an "allies" sentient
// that is not the player (0x009236dc) and whose 0xa8cd6cb7 is undefined or set: when
// BroScr_TurretUseTimeCheck passes, a BroScr_FriendlyUseMg42 thread (ent, turret, node), wait for
// the turret's 0x7b91738c and, while alive, its turret_use_time = now + 10000; then 1 s.
void XB_CDECL BroScr_FriendlyMg42TriggerThink(uint32_t unused, uint32_t trigger);
// 0x001bf490: spawns a "script_origin" at 0 and, every second, rebuilds the team's AI list until
// none touches the trigger; then deletes the origin. `team` is released.
void XB_CDECL BroScr_WaitUntilTeamNotTouching(uint32_t trigger, ScriptString team);
// 0x001c5bf0: BroScr_WaitUntilTeamNotTouching(trigger, a copy of `team`); `team` is released.
void XB_CDECL BroScr_WaitUntilTeamNotTouchingCall(uint32_t trigger, ScriptString team);
// 0x001be660 (functor target): waits for a thrower (trigger 0x262a34d8; end on its death), starts its
// BroScr_GoToBalconyNode thread (0x00922ea0), sets its ref +0x12a0 to the player; then forever: a
// random start among the trigger target's targets, a random end among the start's targets, a manual
// grenade from the start towards the end at twice their distance (`timer`), 1 s, then "throw".
void XB_CDECL BroScr_GrenadeThrowTrigger(uint32_t trigger, uint32_t timer);
// 0x001bec70 (functor target): fallback `n` (level int arrays +0x0c first spawns, +0x6c alive):
// ends on "fallbacker_trigger"+n, waits "fallback_firstspawn"+n once per first spawn, starts
// BroScr_FallbackerJoinFallback for each AI whose 0x8bbe4822 is n or whose 0x1aa720d8 is `group`,
// waits "fallbacker_died"+n while more than half the starting count lives, then notifies
// "fallbacker_trigger"+n on the level. The "fallback" cvar checks before each step are unused.
void XB_CDECL BroScr_FallbackerSetup(uint32_t unused, uint32_t n, uint32_t group);
// 0x001be170 (functor target): for a named turret whose "target" node has script_mg42: copies the
// node's script_mg42 (0xec070be6) to the turret, waits "get new user" once (node named or turret
// 0x7b1e85ba set), then forever: the closest AI to the node among all AI collected so far (except
// unused ones on the same mg42) is told "stop_going_to_node", sent there (BroScr_GoToBalconyNode)
// and awaited till death; with none, waits "get new user". 0x7b1e85ba defaults to 0.
void XB_CDECL BroScr_MgUserGoToNode(uint32_t turret);

}  // namespace game
