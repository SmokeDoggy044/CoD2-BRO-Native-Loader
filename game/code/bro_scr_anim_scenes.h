#pragma once

// Compiled level-script anim scenes (levelscript module, .text 0x00178080-0x0017a2a0): the place
// (origin / angles) of BroScr_AnimLoopGuys / BroScr_AnimSingleGuys, the solo waits, the Sherman tank
// crew anims. Every one reaches a script wait through the scene: class thread_switch, kept out of
// kFunctions until script threads run as coroutines. `guys` is a DynArray's address. Names are ours.

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x00178080 / 0x00178280: BroScr_AnimLoopGuys(ref, guys, a3, a copy of `anime`, p5, origin, p7, p8,
// angles) at: the vector 0x00922ec4 (AtEnt) / the undefined lanes (AtNode) when `p8`; else the
// valid entity / path node `place`; else that vector with p8 true when *ref is the level object;
// else the entity *ref. Release `anime`.
void XB_CDECL BroScr_AnimLoopAtEnt(const uint32_t* ref, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                   uint32_t place, uint32_t p7, uint32_t p8);
void XB_CDECL BroScr_AnimLoopAtNode(const uint32_t* ref, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                    uint32_t place, uint32_t p7, uint32_t p8);
// 0x001784c0 / 0x00178710: the same for BroScr_AnimSingleGuys(ent, guys, a3, anime, p5, origin, flag,
// angles), the entity by value, the undefined lanes in both.
void XB_CDECL BroScr_AnimSingleAtNode(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                      uint32_t place, uint32_t flag);
void XB_CDECL BroScr_AnimSingleAtEnt(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                     uint32_t place, uint32_t flag);
// 0x00178b00 / 0x001797d0: validity checks of the guys and the place (tag of `tag` on `tag_ent` or
// `ent`, path node `node`, or `ent`; results unused), then BroScr_AnimSingleAtNode(ent, guys, a3, tag,
// node, tag_ent, false) / BroScr_AnimLoopAtNodeCall_2(&ent, guys, a3, tag, p5, node, tag_ent).
// Release `tag`.
void XB_CDECL BroScr_AnimSingleAtNode_2(uint32_t ent, const void* guys, uint32_t a3, ScriptString tag, uint32_t node,
                                        uint32_t tag_ent);
void XB_CDECL BroScr_AnimLoopAtNode_2(uint32_t ent, const void* guys, uint32_t a3, ScriptString tag, uint32_t p5,
                                      uint32_t node, uint32_t tag_ent);
// 0x00178cd0: waits ("finished anim solo") while `node`'s bool 0xdedbf624 is set (giving up after
// 5 s), sets it, BroScr_AnimSingleAtNode(node, &{guy}, a3, tag, p5, p6, false), notifies 0x2079d925.
// Releases `tag`.
void XB_CDECL BroScr_WaitAnimSoloFinished(uint32_t node, uint32_t guy, uint32_t a3, ScriptString tag, uint32_t p5,
                                          uint32_t p6);
// 0x001799a0 / 0x00179ca0 / 0x00179fa0 (identical): the tank's guy (array "guys" 0x00325b53) whose
// int 0x9c0e3c3d is `id`: AnimScripted stopped, animname = its string 0xf203df5e, notify "stopanim",
// BroScr_AnimSingleSoloAtNode(guy, guy, hash(anime), its anim string 0xb6abdc5e, 0x00922ea0, tank),
// animname "sherman_tank", a BroScr_AnimLoopAtNodeArrayCall thread (guy, guy, "idle<id>", that
// string, "stopanim", 0x00922ea0, tank). Releases `anime`.
void XB_CDECL BroScr_ShermanTankAnim(uint32_t tank, uint32_t id, ScriptString anime);
void XB_CDECL BroScr_ShermanTankAnim_2(uint32_t tank, uint32_t id, ScriptString anime);
void XB_CDECL BroScr_ShermanTankAnim_3(uint32_t tank, uint32_t id, ScriptString anime);

}  // namespace game
