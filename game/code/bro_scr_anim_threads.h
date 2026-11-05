#pragma once

// Compiled level-script anim-scene wrappers (levelscript module, .text): default-argument and
// one-guy forms of BroScr_AnimLoopAtEnt / AnimLoopAtNode(_2) / AnimSingleAtNode(_2) /
// AnimSingleAtEnt. Every one reaches a script wait through the wrapped scene: class
// thread_switch, kept out of kFunctions until script threads run as coroutines. The last
// argument of most scenes is a bool the wrappers pass as 0. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x00178960 / 0x00178a30 / 0x00179710: a one-element array {guy} (freed after) handed by address:
// BroScr_AnimSingleAtNode(node, &guys, a3, a copy of `anime`, a5, a6, false) / (node, &guys, a3,
// "", 0, 0, false) / BroScr_AnimSingleAtNode_2(node, &guys, a3, a copy of `anime`, a5, a6).
// `anime` is released.
void XB_CDECL BroScr_AnimSingleSoloAtNode(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                          uint32_t a6);
void XB_CDECL BroScr_AnimSingleSoloAtNode_2(uint32_t node, uint32_t guy, uint32_t a3);
void XB_CDECL BroScr_AnimSingleSoloAtNode_3(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                            uint32_t a6);
// 0x0017a2a0: BroScr_AnimLoopAtNode_2(node, &{guy}, a3, a copy of `anime`, a5, a6, a7).
void XB_CDECL BroScr_AnimLoopSoloAtNode(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                        uint32_t a6, uint32_t a7);
// 0x00179650: BroScr_AnimLoopAtNodeCall_2(&node, &{guy}, a3, a copy of `anime`, a5, a6, a7).
void XB_CDECL BroScr_AnimLoopAtNodeArrayCall(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                             uint32_t a6, uint32_t a7);
// 0x00178e70..0x00179110: BroScr_AnimLoopAtEnt(ent, guys, a3, anime, p5, p6, p7, false); the
// missing arguments: anime the empty string (0x00922e90), p5 the word at 0x00922e88, p6 and p7
// the undefined entity (0x00922e84). _1: (none), _2: p8 (as given), _3: anime, _4: anime p5,
// _5: anime p5 p6, _6: anime p5..p7. A given `anime` is copied and released.
void XB_CDECL BroScr_AnimLoopAtEntCall_1(uint32_t ent, uint32_t guys, uint32_t a3);
void XB_CDECL BroScr_AnimLoopAtEntCall_2(uint32_t ent, uint32_t guys, uint32_t a3, uint32_t p8);
void XB_CDECL BroScr_AnimLoopAtEntCall_3(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime);
void XB_CDECL BroScr_AnimLoopAtEntCall_4(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5);
void XB_CDECL BroScr_AnimLoopAtEntCall_5(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                         uint32_t p6);
void XB_CDECL BroScr_AnimLoopAtEntCall_6(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                         uint32_t p6, uint32_t p7);
// 0x001791a0 / 0x00179230: BroScr_AnimLoopAtNode(node, guys, a3, a copy of `anime`, p5, p6, 0 / p7,
// false); `anime` released.
void XB_CDECL BroScr_AnimLoopAtNodeCall_1(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                          uint32_t p6);
void XB_CDECL BroScr_AnimLoopAtNodeCall_2(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                          uint32_t p6, uint32_t p7);
// 0x001792c0..0x00179480: BroScr_AnimSingleAtNode(node, guys, a3, anime, p5, p6, p7) with anime ""
// (ScriptString_FromRep(0)) or a copy (released), the rest 0: _1 (node, guys, a3), _2 (*node_ref,
// guys, a3; p7 given), _3 anime, _4 anime p5, _5 anime p5 p6.
void XB_CDECL BroScr_AnimSingleAtNodeCall_1(uint32_t node, uint32_t guys, uint32_t a3);
void XB_CDECL BroScr_AnimSingleAtNodeCall_2(const uint32_t* node_ref, uint32_t guys, uint32_t a3, uint32_t p7);
void XB_CDECL BroScr_AnimSingleAtNodeCall_3(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime);
void XB_CDECL BroScr_AnimSingleAtNodeCall_4(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5);
void XB_CDECL BroScr_AnimSingleAtNodeCall_5(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                            uint32_t p6);
// 0x00179510 / 0x001795b0: BroScr_AnimSingleAtEnt(ent, guys, a3, a copy of `anime`, p5, 0 / p6,
// false); `anime` released.
void XB_CDECL BroScr_AnimSingleAtEntCall_1(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5);
void XB_CDECL BroScr_AnimSingleAtEntCall_2(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                           uint32_t p6);

}  // namespace game
