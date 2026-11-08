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

// 0x001c51a0 (functor target): the trigger's targets split into trigger_multiples (each a
// BroScr_MagicGrenadeThrowThink thread) and thrower triggers (each a BroScr_GrenadeThrowTrigger
// thread), both with the trigger's script_delay (3.5 when undefined). Then `count` times (the
// trigger's count when defined, else 2): on each trigger by the player while touching, the targets
// re-read into the thrower list, both lists notified "throw", and a random wait (script_delay_min +
// random(max - min), else 2 + random(7)).
void XB_CDECL BroScr_GrenadeThrowTriggers(uint32_t ent);
// 0x001c5790 (functor target): a BroScr_GrenadeThrowTrigger thread on each target of the trigger
// (script_delay, 2 when undefined); after the trigger 0xe2a210cf, BroScr_DeleteFloodSpawner(trigger).
void XB_CDECL BroScr_GrenadeSpawnerOnceTriggerThink(uint32_t trigger);
// 0x001c5910 (functor target): adds the spawner's count to the level's fallbacker count of `group`
// (level +0x6c); for each spawned AI (output of 0x262a34d8): the first notifies "fallback_firstspawn"
// + group; after 0.05 s, a living AI (after "finished spawning" unless 0xc0bb2676 is set) joins the
// fallback (BroScr_FallbackerJoinFallback with group, `node`, "is spawner"); a dead one notifies
// "fallbacker_died" + group and counts down.
void XB_CDECL BroScr_FallbackSpawnerThink(uint32_t spawner, uint32_t group, uint32_t node);

}  // namespace game

