#include "bro_scr_pose_threads.h"

#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_ai_common.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kStand = 0x070b89a4;
constexpr uint32_t kCrouch = 0xc1165808;
constexpr uint32_t kProne = 0x0738b1a5;
constexpr uint32_t kStop = 0x00385d90;
constexpr uint32_t kWalk = 0x003a09eb;
constexpr uint32_t kRun = 0x0001c6ce;

inline uint32_t pose_of(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent); }  // ScrActor_GetInt_184
// anim_movement read inline (the first test of each script) / through the field accessor.
inline uint32_t movement_inline(uint32_t ent) { return *handle_slot(ent, 0x0ccee5c1); }
inline uint32_t movement(uint32_t ent) { return *ScrField_anim_movement(ent); }
// A prone actor's movement is one of stop / walk / run.
inline bool moving_any(uint32_t ent) {
    return movement(ent) == kStop || movement(ent) == kWalk || movement(ent) == kRun;
}

using EntFn = void XB_CDECL(uint32_t);
inline void stand_combat_walk(uint32_t ent) { title_function<EntFn>(0x001acf80)(ent); }  // BroScr_StandCombatWalk (agent-b list)
inline void stand_combat_run(uint32_t ent) { title_function<EntFn>(0x001acd90)(ent); }   // BroScr_StandCombatRun (agent-b list)
inline void crouch_run(uint32_t ent) { title_function<EntFn>(0x001ad1f0)(ent); }         // BroScr_CrouchRun (agent-b list)
inline void crouch_walk_loop(uint32_t ent) { title_function<EntFn>(0x001ad330)(ent); }   // BroScr_CrouchWalkLoop (agent-b list)

}  // namespace

void XB_CDECL BroScr_PoseMove_StandStop(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop) return;
        if (movement(ent) == kWalk) {
            BroScr_StandWalkToStop(ent);
            return;
        }
        if (movement(ent) == kRun) BroScr_StandWalkToStop_2(ent);
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop) {
            BroScr_StandMoveToStop(ent);
        } else if (movement(ent) == kWalk) {
            BroScr_CrouchWalkToCrouchStop(ent);
            BroScr_StandMoveToStop(ent);
        } else if (movement(ent) == kRun) {
            BroScr_CrouchWalkToCrouchStop_2(ent);
            BroScr_StandMoveToStop(ent);
        }
    } else if (pose_of(ent) == kProne) {
        if (moving_any(ent)) {
            BroScr_IdleFaceCurrent(ent);
            BroScr_StandMoveToStop(ent);
        }
    }
}

void XB_CDECL BroScr_PoseMove_StandWalk(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop) {
            BroScr_StandStartWalk(ent);
            return;
        }
        if (movement(ent) == kWalk) return;
        if (movement(ent) == kRun) stand_combat_walk(ent);
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop) {
            BroScr_CrouchToCrouchWalk(ent);
            stand_combat_walk(ent);
        } else if (movement(ent) == kWalk) {
            stand_combat_walk(ent);
        } else if (movement(ent) == kRun) {
            stand_combat_walk(ent);
        }
    } else if (pose_of(ent) == kProne) {
        if (moving_any(ent)) {
            BroScr_IdleFaceCurrent(ent);
            BroScr_CrouchToCrouchWalk(ent);
            stand_combat_walk(ent);
        }
    }
}

void XB_CDECL BroScr_PoseMove_StandRun(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop || movement(ent) == kWalk)
            stand_combat_run(ent);
        else
            ScrField_anim_movement(ent);  // run: nothing (the read stays)
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop) {
            crouch_run(ent);
            stand_combat_run(ent);
        } else if (movement(ent) == kWalk) {
            stand_combat_run(ent);
        } else if (movement(ent) == kRun) {
            stand_combat_run(ent);
        }
    } else if (pose_of(ent) == kProne) {
        if (moving_any(ent)) {
            BroScr_ProneFaceCurrent(ent);
            stand_combat_run(ent);
        }
    }
}

void XB_CDECL BroScr_PoseMove_CrouchStop(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop) {
            BroScr_StandToCrouchStop(ent);
        } else if (movement(ent) == kWalk) {
            BroScr_StandWalkToStop(ent);
            BroScr_StandToCrouchStop(ent);
        } else if (movement(ent) == kRun) {
            BroScr_StandWalkToStop_2(ent);
            BroScr_StandToCrouchStop(ent);
        }
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop) return;
        if (movement(ent) == kWalk) {
            BroScr_CrouchWalkToCrouchStop(ent);
            return;
        }
        if (movement(ent) == kRun) BroScr_CrouchWalkToCrouchStop_2(ent);
    } else if (pose_of(ent) == kProne) {
        if (moving_any(ent)) BroScr_IdleFaceCurrent(ent);
    }
}

void XB_CDECL BroScr_PoseMove_CrouchWalk(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop) {
            BroScr_StandStartWalk(ent);
            crouch_walk_loop(ent);
        } else if (movement(ent) == kWalk) {
            crouch_walk_loop(ent);
        } else if (movement(ent) == kRun) {
            crouch_walk_loop(ent);
        }
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop) {
            BroScr_CrouchToCrouchWalk(ent);
            return;
        }
        if (movement(ent) == kWalk) return;
        if (movement(ent) == kRun) crouch_walk_loop(ent);
    } else if (pose_of(ent) == kProne) {
        if (moving_any(ent)) {
            BroScr_ProneFaceCurrent(ent);
            crouch_walk_loop(ent);
        }
    }
}

void XB_CDECL BroScr_PoseMove_CrouchRun(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop || movement(ent) == kWalk || movement(ent) == kRun) crouch_run(ent);
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop || movement(ent) == kWalk)
            crouch_run(ent);
        else
            ScrField_anim_movement(ent);  // run: nothing (the read stays)
    } else if (pose_of(ent) == kProne) {
        if (moving_any(ent)) BroScr_ProneFaceCurrent(ent);
    }
}

void XB_CDECL BroScr_PoseMove_ProneStop(uint32_t ent) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop || movement(ent) == kWalk)
            BroScr_StandToProne(ent);
        else if (movement(ent) == kRun)
            BroScr_CrouchRunToProne(ent);
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop || movement(ent) == kWalk)
            BroScr_CrouchToProne(ent);
        else if (movement(ent) == kRun)
            BroScr_CrouchRunToProne(ent);
    } else if (pose_of(ent) == kProne) {
        if (movement(ent) == kStop) return;
        if (movement(ent) == kWalk || movement(ent) == kRun) BroScr_ProneCrawlToAim(ent);
    }
}

void XB_CDECL BroScr_PoseToProneCrawl(uint32_t ent, uint32_t move) {
    if (pose_of(ent) == kStand) {
        if (movement_inline(ent) == kStop) {
            BroScr_StandToProne(ent);
            BroScr_ProneAimToCrawl(ent, move);
        } else if (movement(ent) == kWalk) {
            BroScr_CrouchRunToProne(ent);
            BroScr_ProneAimToCrawl(ent, move);
        } else if (movement(ent) == kRun) {
            crouch_run(ent);
            BroScr_CrouchRunToProne(ent);
            BroScr_ProneAimToCrawl(ent, move);
        }
    } else if (pose_of(ent) == kCrouch) {
        if (movement(ent) == kStop) {
            BroScr_CrouchToProne(ent);
            BroScr_ProneAimToCrawl(ent, move);
        } else if (movement(ent) == kWalk) {
            BroScr_CrouchRunToProne(ent);
            BroScr_ProneAimToCrawl(ent, move);
        } else if (movement(ent) == kRun) {
            BroScr_CrouchRunToProne(ent);
            BroScr_ProneAimToCrawl(ent, move);
        }
    } else if (pose_of(ent) == kProne) {
        if (movement(ent) == kStop)
            BroScr_ProneAimToCrawl(ent, move);
        else if (movement(ent) == kWalk || movement(ent) == kRun)
            *ScrField_anim_movement(ent) = move;
    }
}

void XB_CDECL BroScr_PoseMove_ProneWalk(uint32_t ent) { BroScr_PoseToProneCrawl(ent, kWalk); }

void XB_CDECL BroScr_PoseMove_ProneRun(uint32_t ent) { BroScr_PoseToProneCrawl(ent, kRun); }

}  // namespace game
