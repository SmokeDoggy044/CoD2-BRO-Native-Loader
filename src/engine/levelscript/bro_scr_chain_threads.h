#pragma once

// Compiled level-script chain threads (levelscript module, .text, thread-switch push c): the
// hangar chains that send AI along a chain of nodes one after the other. Every one reaches a
// script wait: class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x001b4130 (functor target): ends on the level's 0x011066c4. A chain mover `ent` (nodes 0x0605894e,
// AI 0x8969d6d6), while it has two nodes or more: every 1-2 s (while it has AI), when no living axis AI
// is within the node-0 radius (its 0x2266f963 squared when above 64, else 0x008cd770 squared) of node 0
// nor of node 1: after 0xce19d6b0 (busy flag 0x14b95ada set) each living AI notified 0xb633d42e and
// sent to node 1 (BroScr_ActorRunToEntIgnoringAll thread), awaiting node 1's 0x455955a2 and 0.5 s
// each; then node 0 is dropped. With one node left the mover is deleted.
void XB_CDECL BroScr_HangarChainMoverThink(uint32_t ent);
// 0x001bc7c0 (functor target): ends on the level's 0x011066c4. A chain owner `ent` holds the chain
// nodes (array 0x0605894e) and its AI (array 0x8969d6d6; each AI's chain_pos 0x94ce2dcb = its node).
// With one node or none: forever every 3-5 s, each living AI notified 0xb633d42e, chain_pos 0 and a
// BroScr_ActorRunToEntIgnoringAll_2 thread to node 0, 1 s apart. Else every 3-5 s (while it has AI):
// the AI at node 0 (chain_pos 0) collected; more than the level count (0x008cd778): after 0xce19d6b0
// (busy flag 0x14b95ada set) each sent to a random later node. Then the allies and the player within
// the node-0 radius (its 0x2266f963 squared when above 64, else 0x008cd774 squared): two or more:
// the collected AI move up to node 1, node 0 is dropped and every AI's chain_pos counts down; fewer:
// the missing count of AI further up the chain are called back to node 0.
void XB_CDECL BroScr_HangarChainAIThink(uint32_t ent);
// 0x001c4a80: notifies the level 0x011066c4, a frame, then deletes the previous movers (level +0x8c)
// and chain owners (+0x98). For each entity named movers + "_" + n (n = 0, 1, ...): a script_origin
// mover with its targetname (0x99ca82d7), its target chain (BroScr_BuildTargetChain) and a
// BroScr_HangarChainMoverThink thread; likewise chains + "_" + n with a BroScr_HangarChainAIThink
// thread. Then level +0x15c = 1, a BroScr_HangarChainUpdateLoop thread on the level and notify
// 0x492f14bc. Releases both strings.
void XB_CDECL BroLvl_oran_SetupEvent4Chains(ScriptString movers, ScriptString chains);

// 0x001bc150 (Scr_BeginThreadFunctor1 target, for AI with script_battle): picks a hangar chain for AI
// `ent` - with a target, the level list entry whose targetname (0x99ca82d7) matches it (allies: the
// movers at level +0x8c, others: the chain owners at +0x98); without one, allies start a
// BroScr_SetGoalRadius64UntilNotify thread, wait for the level's 0x1b179a21 and take the mover with
// the fewest AI (array 0x8969d6d6), axis get goal radius 64 and take the owner with the fewest AI. A
// valid chain gets `ent` in its AI array and a BroScr_GoToChainNode (allies) / BroScr_FriendlyChainThink
// (axis) thread. Name ours.
void XB_CDECL BroScr_JoinHangarChain(uint32_t ent);

}  // namespace game
