#include "bro_scr_anim.h"

#include "bro_scr_common.h"
#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kOne = 0x3f800000;  // float words pushed as immediates
using SetAnim6 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SetAnim7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using SetAnim8 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void SetAnimKnob(uint32_t entity, uint32_t anim, uint32_t weight, uint32_t time, uint32_t rate, uint32_t restart) {
    scr_slot<SetAnim6>(0x009225d8)(entity, anim, weight, time, rate, restart);  // GScr_SetAnimKnob
}

uint32_t QueueNotifyExitCover(uint32_t entity) {
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(entity, 0x281d9462);  // Scr_QueueNotify
    return 0;
}

}  // namespace

uint32_t XB_CDECL Bro_anim_exit_cover_left(uint32_t entity) {
    return QueueNotifyExitCover(entity);
}

uint32_t XB_CDECL Bro_anim_exit_cover_right(uint32_t entity) {
    return QueueNotifyExitCover(entity);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf684(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf684_2(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf684_3(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf684), kOne, 0x3e19999a, 0, 1);
}

void XB_CDECL BroScr_TurretSetAnimKnob_8cf7a4(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf7a4), kOne, 0x3e4ccccd, kOne, 1);
}

void XB_CDECL BroScr_TurretSetProneRecoverAnim(uint32_t, uint32_t entity) {
    SetAnimKnob(entity, title_global<uint32_t>(0x008cf7bc), kOne, 0x3e4ccccd, kOne, 1);
}

void XB_CDECL BroScr_SetProneLegsStraightKnob(uint32_t entity, uint32_t time) {
    scr_slot<SetAnim7>(0x009225c8)(entity, title_global<uint32_t>(0x008cf138), title_global<uint32_t>(0x008cd7dc), kOne,
                                   time, kOne, 0);  // GScr_SetAnimKnobAll(prone_legsstraight, Body)
}

void XB_CDECL BroScr_SetAimAnimKnobs(uint32_t entity, uint32_t flag, uint32_t anim1, uint32_t anim2, uint32_t anim3,
                                     uint32_t weight, uint32_t time, uint32_t rate) {
    SetAnimKnob(entity, anim1, weight, time, rate, 0);
    scr_slot<SetAnim7>(0x009225e0)(entity, flag, anim2, weight, time, rate, 0);  // GScr_SetFlaggedAnimKnob
    SetAnimKnob(entity, anim3, weight, time, rate, 0);
}

void XB_CDECL BroScr_AnimCmd_SetAnim(uint32_t a1, uint32_t, uint32_t a3, uint32_t, uint32_t, uint32_t a6, uint32_t a7,
                                     uint32_t a8) {
    scr_slot<SetAnim6>(0x009225dc)(a1, a3, a6, a7, a8, 0);  // GScr_SetAnim
}

void XB_CDECL BroScr_AnimCmd_SetAnimKnobAll(uint32_t a1, uint32_t, uint32_t a3, uint32_t a4, uint32_t, uint32_t a6,
                                            uint32_t a7, uint32_t a8) {
    scr_slot<SetAnim7>(0x009225c8)(a1, a3, a4, a6, a7, a8, 0);  // GScr_SetAnimKnobAll
}

void XB_CDECL BroScr_AnimCmd_SetFlaggedAnimKnobAll(uint32_t a1, uint32_t, uint32_t a3, uint32_t a4, uint32_t a5,
                                                   uint32_t a6, uint32_t a7, uint32_t a8) {
    scr_slot<SetAnim8>(0x009225e4)(a1, a5, a3, a4, a6, a7, a8, 0);  // GScr_SetFlaggedAnimKnobAll
}

}  // namespace game
