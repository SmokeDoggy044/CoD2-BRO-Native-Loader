#include "bro_scr_anim_threads.h"

#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "title_data.h"
#include "bro_scr_anim_scenes.h"
#include "script_string.h"

namespace game {

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

ScriptString copy_string(const ScriptString* source) {
    ScriptString copy;
    ScriptString_Copy(&copy, source);
    return copy;
}
// ScriptString_FromRep(0) (0x0011d5d0): the empty string.
ScriptString empty_string() {
    ScriptString empty;
    own_function<void XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_FromRep)(&empty, 0);
    return empty;
}
ScriptString level_empty_string() { return copy_string(&title_global<ScriptString>(0x00922e90)); }
uint32_t word_e84() { return title_global<uint32_t>(0x00922e84); }  // the undefined entity
uint32_t word_e88() { return title_global<uint32_t>(0x00922e88); }

using Scene7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, uint32_t);
using Scene8 = void XB_CDECL(uint32_t, uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, uint32_t, uint32_t);

void anim_loop_at_ent(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5, uint32_t p6, uint32_t p7,
                      uint32_t p8) {
    own_function<Scene8>(&BroScr_AnimLoopAtEnt)(ent, guys, a3, anime, p5, p6, p7, p8);  // BroScr_AnimLoopAtEnt
}
void anim_loop_at_node(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5, uint32_t p6, uint32_t p7) {
    own_function<Scene8>(&BroScr_AnimLoopAtNode)(node, guys, a3, anime, p5, p6, p7, 0);  // BroScr_AnimLoopAtNode(.., false)
}
void anim_single_at_node(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5, uint32_t p6,
                         uint32_t p7) {
    own_function<Scene7>(&BroScr_AnimSingleAtNode)(node, guys, a3, anime, p5, p6, p7);  // BroScr_AnimSingleAtNode
}
void anim_single_at_ent(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5, uint32_t p6) {
    own_function<Scene7>(&BroScr_AnimSingleAtEnt)(ent, guys, a3, anime, p5, p6, 0);  // BroScr_AnimSingleAtEnt(.., false)
}

// A one-element array {guy} (DynArray_Resize(1, 4)).
void one_guy(DynArray* guys, uint32_t guy) {
    guys->data = 0;
    guys->capacity = 0;
    guys->size = 0;
    DynArray_Resize(reinterpret_cast<uint8_t*>(guys), 1, 4);
    guys->data[0] = guy;
}
void free_guys(DynArray* guys) {
    if (guys->data) Scr_Free(guys->data);
}

}  // namespace

void XB_CDECL BroScr_AnimSingleSoloAtNode(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                          uint32_t a6) {
    DynArray guys;
    one_guy(&guys, guy);
    // The bool is a byte written over the slot that held `guy`: its upper bytes stay.
    const uint32_t flag = guy & 0xffffff00u;
    anim_single_at_node(node, address_of(&guys), a3, copy_string(&anime), a5, a6, flag);
    free_guys(&guys);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleSoloAtNode_2(uint32_t node, uint32_t guy, uint32_t a3) {
    DynArray guys;
    one_guy(&guys, guy);
    ScriptString anime = empty_string();
    anim_single_at_node(node, address_of(&guys), a3, copy_string(&anime), 0, 0, 0);
    scr_release(&anime);
    free_guys(&guys);
}

void XB_CDECL BroScr_AnimSingleSoloAtNode_3(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                            uint32_t a6) {
    DynArray guys;
    one_guy(&guys, guy);
    using Scene6 = void XB_CDECL(uint32_t, uint32_t, uint32_t, ScriptString, uint32_t, uint32_t);
    own_function<Scene6>(&BroScr_AnimSingleAtNode_2)(node, address_of(&guys), a3, copy_string(&anime), a5, a6);  // BroScr_AnimSingleAtNode_2
    free_guys(&guys);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopSoloAtNode(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                        uint32_t a6, uint32_t a7) {
    DynArray guys;
    one_guy(&guys, guy);
    own_function<Scene7>(&BroScr_AnimLoopAtNode_2)(node, address_of(&guys), a3, copy_string(&anime), a5, a6, a7);  // BroScr_AnimLoopAtNode_2
    free_guys(&guys);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtNodeArrayCall(uint32_t node, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5,
                                             uint32_t a6, uint32_t a7) {
    DynArray guys;
    one_guy(&guys, guy);
    BroScr_AnimLoopAtNodeCall_2(address_of(&node), address_of(&guys), a3, copy_string(&anime), a5, a6, a7);
    free_guys(&guys);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtEntCall_1(uint32_t ent, uint32_t guys, uint32_t a3) {
    const uint32_t p7 = word_e84();
    const uint32_t p6 = word_e84();
    anim_loop_at_ent(ent, guys, a3, level_empty_string(), word_e88(), p6, p7, 0);
}

void XB_CDECL BroScr_AnimLoopAtEntCall_2(uint32_t ent, uint32_t guys, uint32_t a3, uint32_t p8) {
    const uint32_t p7 = word_e84();
    const uint32_t p6 = word_e84();
    anim_loop_at_ent(ent, guys, a3, level_empty_string(), word_e88(), p6, p7, p8);
}

void XB_CDECL BroScr_AnimLoopAtEntCall_3(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime) {
    const uint32_t p7 = word_e84();
    const uint32_t p6 = word_e84();
    const uint32_t p5 = word_e88();
    anim_loop_at_ent(ent, guys, a3, copy_string(&anime), p5, p6, p7, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtEntCall_4(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5) {
    const uint32_t p7 = word_e84();
    const uint32_t p6 = word_e84();
    anim_loop_at_ent(ent, guys, a3, copy_string(&anime), p5, p6, p7, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtEntCall_5(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                         uint32_t p6) {
    anim_loop_at_ent(ent, guys, a3, copy_string(&anime), p5, p6, word_e84(), 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtEntCall_6(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                         uint32_t p6, uint32_t p7) {
    anim_loop_at_ent(ent, guys, a3, copy_string(&anime), p5, p6, p7, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtNodeCall_1(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                          uint32_t p6) {
    anim_loop_at_node(node, guys, a3, copy_string(&anime), p5, p6, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimLoopAtNodeCall_2(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                          uint32_t p6, uint32_t p7) {
    anim_loop_at_node(node, guys, a3, copy_string(&anime), p5, p6, p7);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtNodeCall_1(uint32_t node, uint32_t guys, uint32_t a3) {
    anim_single_at_node(node, guys, a3, empty_string(), 0, 0, 0);
}

void XB_CDECL BroScr_AnimSingleAtNodeCall_2(const uint32_t* node_ref, uint32_t guys, uint32_t a3, uint32_t p7) {
    const ScriptString anime = empty_string();
    anim_single_at_node(*node_ref, guys, a3, anime, 0, 0, p7);
}

void XB_CDECL BroScr_AnimSingleAtNodeCall_3(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime) {
    anim_single_at_node(node, guys, a3, copy_string(&anime), 0, 0, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtNodeCall_4(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5) {
    anim_single_at_node(node, guys, a3, copy_string(&anime), p5, 0, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtNodeCall_5(uint32_t node, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                            uint32_t p6) {
    anim_single_at_node(node, guys, a3, copy_string(&anime), p5, p6, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtEntCall_1(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5) {
    anim_single_at_ent(ent, guys, a3, copy_string(&anime), p5, 0);
    scr_release(&anime);
}

void XB_CDECL BroScr_AnimSingleAtEntCall_2(uint32_t ent, uint32_t guys, uint32_t a3, ScriptString anime, uint32_t p5,
                                           uint32_t p6) {
    anim_single_at_ent(ent, guys, a3, copy_string(&anime), p5, p6);
    scr_release(&anime);
}

}  // namespace game
