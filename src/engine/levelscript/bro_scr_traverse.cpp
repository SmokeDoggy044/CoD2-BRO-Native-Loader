#include "bro_scr_traverse.h"

#include <cmath>

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_note_handlers.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kKillAnimScript = 0x07b4e7f7;
constexpr uint32_t kPose = 0x8cb5047a;     // the desired pose
constexpr uint32_t kMove = 0x0ccee5c1;     // movement keys and where traversals save them
constexpr uint32_t kMoveSaved = 0x0ca29c7f;
constexpr uint32_t kMove2 = 0x764918ae;
constexpr uint32_t kMove2Saved = 0x7bc62f6e;
constexpr uint32_t kBlendBack = 0x682ea6e9;  // the anim a traversal blends back to

constexpr uint32_t kNoGravity = 0x0083ac50;  // "nogravity"
constexpr uint32_t kNoClip = 0x0083bb08;     // "noclip"
constexpr uint32_t kGravity = 0x0083ad10;    // "gravity"

using KnobAll = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
using FlaggedKnobAll = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
using HandleNoteTracks = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, ScriptString);
using HandleNoteTracksForTime = void XB_CDECL(uint32_t, float, uint32_t, uint32_t, ScriptString);
using CallEntHash = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
using CallEnt6Anims = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                        const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*,
                                        const uint32_t*);
using BeginThreadFunctor1 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using Length = long double XB_CDECL(const uint32_t*);

uint32_t body() { return title_global<uint32_t>(0x008cd7dc); }  // g_anim_Body
uint32_t root() { return title_global<uint32_t>(0x008cf300); }  // g_anim_root

// Desired pose `pose`, applied.
void set_pose(uint32_t ent, uint32_t pose) {
    *handle_slot(ent, kPose) = pose;
    BroScr_ApplyDesiredAnimPose(ent);
}
// Key `to` = key `from` (both looked up first, then the value copied).
void copy_key(uint32_t ent, uint32_t from, uint32_t to) {
    uint32_t* const source = handle_slot(ent, from);
    uint32_t* const target = handle_slot(ent, to);
    *target = *source;
}
void save_move_keys(uint32_t ent) {
    copy_key(ent, kMove, kMoveSaved);
    copy_key(ent, kMove2, kMove2Saved);
}
void restore_move_keys(uint32_t ent) {
    copy_key(ent, kMoveSaved, kMove);
    copy_key(ent, kMove2Saved, kMove2);
}
void set_move_keys(uint32_t ent, uint32_t move, uint32_t move2) {
    *handle_slot(ent, kMove) = move;
    *handle_slot(ent, kMove2) = move2;
}
void end_on_kill(uint32_t ent) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, kKillAnimScript); }  // Scr_EndOn

// GScr_ActorSetTraverseMode(ent, text) with a temporary string.
void traverse_mode(uint32_t ent, uint32_t text) {
    ScriptString mode;
    scr_string_from(&mode, text);
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922738)(ent, &mode);
    scr_release(&mode);
}

// GScr_SetFlaggedAnimKnobAll(ent, flag, anim, body, 1, blend, 1, restart).
void flagged_knob(uint32_t ent, uint32_t flag, uint32_t anim, float blend = 0.1f, uint32_t restart = 1) {
    slot<FlaggedKnobAll>(0x009225e4)(ent, flag, anim, body(), 1.0f, blend, 1.0f, restart);
}
// GScr_SetAnimKnobAll(ent, anim, parent, 1, blend, 1, 1).
void knob(uint32_t ent, uint32_t anim, uint32_t parent, float blend = 0.1f) {
    slot<KnobAll>(0x009225c8)(ent, anim, parent, 1.0f, blend, 1.0f, 1);
}
// Blends back to the anim under kBlendBack (added as 0).
void blend_back(uint32_t ent, uint32_t parent, float blend = 0.1f) {
    knob(ent, *key_slot(ent, kBlendBack, 0), parent, blend);
}

// BroScr_HandleNoteTracks(out, ent, flag, 0, a copy of the empty string); the result released.
void note_tracks(uint32_t ent, uint32_t flag) {
    ScriptString empty;
    ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
    ScriptString result;
    own_function<HandleNoteTracks>(&BroScr_HandleNoteTracks)(&result, ent, flag, 0, empty);  // BroScr_HandleNoteTracks
    scr_release(&result);
}
// BroScr_HandleNoteTracksForTime(ent, time, flag, 0, a copy of the empty string).
void note_tracks_for_time(uint32_t ent, float time, uint32_t flag) {
    ScriptString empty;
    ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
    own_function<HandleNoteTracksForTime>(&BroScr_HandleNoteTracksForTime)(ent, time, flag, 0, empty);  // BroScr_HandleNoteTracksForTime
}
// Waits for the flag's "gravity on" note (Scr_WaitTillArgs(ent, flag, hash, 0, 0)).
void wait_gravity_on(uint32_t ent, uint32_t flag) {
    uint32_t hash;
    Scr_WaitTillArgs(ent, flag, *Scr_HashName(&hash, pointer<const char>(0x00826280)), 0, 0);  // "gravity on"
}
// Scr_CallThread_ent_hash(BroScr_MiscStepSounds, ent, end 0x19791439).
void step_sounds(uint32_t ent) {
    const uint32_t end = 0x19791439;
    title_function<CallEntHash>(0x004b08e0)(0, 0, 0, 0, 0x00183f50, ent, &end);
}
// The assert text the script builds and drops before a timed play.
void climbing_time_text() {
    ScriptString text;
    scr_string_from(&text, 0x008263b8);  // "Climbing time should be positive!"
    scr_release(&text);
}

uint32_t* origin_of(uint32_t* buffer, uint32_t ent) { return slot<GetVec>(0x00922b20)(buffer, ent); }  // ScrEnt_GetOrigin
void path_node_or_origin(uint32_t ent, uint32_t* out) {
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922788)(ent, out);  // GScr_ActorGetPathNodeOrOrigin
}
void move_delta(uint32_t* out, uint32_t anim) {
    slot<void XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00922408)(out, anim, 0, 0x3f800000);  // GScr_GetMoveDelta
}
long double anim_length(uint32_t ent, uint32_t anim) {
    return slot<long double XB_CDECL(uint32_t, uint32_t)>(0x009225cc)(ent, anim);  // GScr_GetAnimLength
}
void set_stance(uint32_t ent, uint32_t stance) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, stance); }  // ScrActor_SetInt_184

// The fence/wall climbs: pose 0xc1165808, keys saved, the anim with note tracks, keys back.
void climb_with_note_tracks(uint32_t ent, uint32_t anim_global) {
    set_pose(ent, 0xc1165808);
    save_move_keys(ent);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    flagged_knob(ent, 0xaa613677, title_global<uint32_t>(anim_global));
    note_tracks(ent, 0xaa613677);
    traverse_mode(ent, kGravity);
    restore_move_keys(ent);
    blend_back(ent, body());
    step_sounds(ent);
}

// The drops: pose 0xc1165808, keys 0x3a09eb / 0x6279a34; "nogravity"; the anim until
// "gravity on"; "gravity"; the remaining note tracks.
void drop_until_gravity(uint32_t ent, uint32_t anim_global) {
    set_pose(ent, 0xc1165808);
    end_on_kill(ent);
    set_move_keys(ent, 0x003a09eb, 0x06279a34);
    traverse_mode(ent, kNoGravity);
    flagged_knob(ent, 0xcd3136c8, title_global<uint32_t>(anim_global));
    wait_gravity_on(ent, 0xcd3136c8);
    traverse_mode(ent, kGravity);
    note_tracks(ent, 0xcd3136c8);
}
void drop_to_crouch_walk(uint32_t ent, uint32_t anim_global) {
    drop_until_gravity(ent, anim_global);
    knob(ent, title_global<uint32_t>(0x008ce760), root());  // g_anim_crouchwalk_loop
}

// The vaults' start: pose 0xc1165808; "nogravity", "noclip"; the flagged anim.
void vault(uint32_t ent, uint32_t anim_global, uint32_t flag) {
    set_pose(ent, 0xc1165808);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    flagged_knob(ent, flag, title_global<uint32_t>(anim_global));
}

// The stairs: the anim for the time its flat move covers the flat distance to the path node
// (origin read twice, once per lane); keys 0x3a09eb / 0x6279a34, stance 0x070b89a4.
float flat_distance_to(const uint32_t* end, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, ent);
    uint32_t origin[3] = {o[0], o[1], o[2]};
    const float dx = as_float(end[0]) - as_float(origin[0]);
    o = origin_of(buffer, ent);
    origin[0] = o[0];
    origin[1] = o[1];
    origin[2] = o[2];
    const float dy = as_float(end[1]) - as_float(origin[1]);
    return static_cast<float>(std::sqrt(static_cast<long double>(dy * dy + dx * dx)));
}
void stairs_play(uint32_t ent, uint32_t anim, float distance) {
    const uint32_t u = undefined_lane();
    uint32_t delta[3] = {u, u, u};
    move_delta(delta, anim);
    reinterpret_cast<float*>(delta)[2] = 0.0f;
    const float flat = static_cast<float>(slot<Length>(0x00922270)(delta));  // Scr_Length
    const long double length = anim_length(ent, anim);
    note_tracks_for_time(ent, static_cast<float>(distance / flat * length), 0x9c115537);
    *handle_slot(ent, kMove) = 0x003a09eb;
    set_stance(ent, 0x070b89a4);
    *handle_slot(ent, kMove2) = 0x06279a34;
}
uint32_t stairs_anim(uint32_t ent, uint32_t armed, uint32_t unarmed) {
    uint32_t type;
    if (*BroScr_GetWeaponType(&type, ent) == 0x0036b1a5) return title_global<uint32_t>(unarmed);
    const uint32_t other = *BroScr_GetWeaponType(&type, ent);
    return title_global<uint32_t>(other == 0xe6a85d8c ? unarmed : armed);
}

void begin_functor_thread(uint32_t body_address, uint32_t ent) {
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, body_address, ent);  // Scr_BeginThreadFunctor1
}

}  // namespace

void XB_CDECL BroScr_Traverse(uint32_t ent) {
    set_pose(ent, 0x070b89a4);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    flagged_knob(ent, 0xb8092bb9, title_global<uint32_t>(0x008cd7e8));  // g_anim_c_climb_60units_dock
    note_tracks(ent, 0xb8092bb9);
    set_move_keys(ent, 0x0001c6ce, 0xc028584c);
    blend_back(ent, body());
}

void XB_CDECL BroScr_Traverse_2(uint32_t ent) { climb_with_note_tracks(ent, 0x008ce814); }  // g_anim_fenceclimb
void XB_CDECL BroScr_Traverse_3(uint32_t ent) { climb_with_note_tracks(ent, 0x008ce814); }  // g_anim_fenceclimb
void XB_CDECL BroScr_Traverse_4(uint32_t ent) { climb_with_note_tracks(ent, 0x008cda34); }  // g_anim_climb_wall_48
void XB_CDECL BroScr_Traverse_7(uint32_t ent) { climb_with_note_tracks(ent, 0x008cda70); }  // g_anim_climbstation_platform_P

void XB_CDECL BroScr_Traverse_11(uint32_t ent) {
    set_pose(ent, 0x070b89a4);
    save_move_keys(ent);
    end_on_kill(ent);
    traverse_mode(ent, kNoClip);
    const uint32_t anim = title_global<uint32_t>(0x008cd848);  // g_anim_c_slidingDown_runA
    const uint32_t u = undefined_lane();
    uint32_t end[3] = {u, u, u};
    path_node_or_origin(ent, end);
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, ent);
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    float to_end[3];
    to_end[0] = as_float(end[0]) - as_float(origin[0]);
    to_end[1] = as_float(end[1]) - as_float(origin[1]);
    to_end[2] = as_float(end[2]) - as_float(origin[2]);
    const float distance = static_cast<float>(slot<Length>(0x00922270)(reinterpret_cast<const uint32_t*>(to_end)));
    uint32_t delta[3] = {u, u, u};
    move_delta(delta, anim);
    const float move = static_cast<float>(slot<Length>(0x00922270)(delta));  // Scr_Length
    const long double length = anim_length(ent, anim);
    const float time = static_cast<float>(distance / move * length);
    flagged_knob(ent, 0x9b80df77, anim);
    climbing_time_text();
    note_tracks_for_time(ent, time, 0x9b80df77);
    traverse_mode(ent, kGravity);
    restore_move_keys(ent);
}

void XB_CDECL BroScr_Traverse_5(uint32_t ent) {
    set_pose(ent, 0x070b89a4);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    flagged_knob(ent, 0x9d313600, title_global<uint32_t>(0x008ce934));  // g_anim_jump_across_72
    wait_gravity_on(ent, 0x9d313600);
    traverse_mode(ent, kGravity);
    note_tracks(ent, 0x9d313600);
    set_move_keys(ent, 0x0001c6ce, 0xc028584c);
    knob(ent, *key_slot(ent, 0x66a44409, 0), root(), 0.2f);
}

void XB_CDECL BroScr_Traverse_12(uint32_t ent) { drop_to_crouch_walk(ent, 0x008ce940); }  // g_anim_jump_down_56
void XB_CDECL BroScr_Traverse_13(uint32_t ent) { drop_to_crouch_walk(ent, 0x008ce94c); }  // g_anim_jump_down_96
void XB_CDECL BroScr_Traverse_18(uint32_t ent) { drop_to_crouch_walk(ent, 0x008cf654); }

void XB_CDECL BroScr_Traverse_6(uint32_t ent) {
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    const uint32_t anim = random_int(100) < 50 ? title_global<uint32_t>(0x008ce958)   // g_anim_jump_over_high_wallA
                                               : title_global<uint32_t>(0x008ce964);  // g_anim_jump_over_high_wallB
    flagged_knob(ent, 0xaa613677, anim);
    wait_gravity_on(ent, 0xaa613677);
    traverse_mode(ent, kNoClip);
    Scr_WaitTillArgs(ent, 0xaa613677, 0xdac03d50, 0, 0);
    traverse_mode(ent, kNoClip);
    wait_gravity_on(ent, 0xaa613677);
    traverse_mode(ent, kGravity);
    set_move_keys(ent, 0x0001c6ce, 0xc028584c);
    blend_back(ent, body());
}

void XB_CDECL BroScr_PlayStepRunSound(uint32_t ent, uint32_t notify) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, notify,
                                                                                      &note);  // Scr_WaitTillOutputStr_ORAN
        if (!note.rep) ScriptString_AssignCString(&note, pointer<const char>(0x007f9e88));  // "undefined"
        uint32_t hash;
        const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&note));
        if (h == 0x000199a4 || h == 0x9aa82fee || h == 0xcc5542ad) break;
        if (h == 0x19785ffb || h == 0x00385cd0) {
            ScriptString sound;
            scr_string_from(&sound, 0x008263dc);  // "step_run_dirt"
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0,
                                                                                                   0);  // GScr_PlaySound
            scr_release(&sound);
        }
    }
    scr_release(&note);
}

void XB_CDECL BroScr_Traverse_14(uint32_t ent) {
    set_pose(ent, 0xc1165808);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    const uint32_t u = undefined_lane();
    uint32_t end[3] = {u, u, u};
    path_node_or_origin(ent, end);
    flagged_knob(ent, 0x9c115537, title_global<uint32_t>(0x008ce994));  // g_anim_ladder_climbon
    note_tracks(ent, 0x9c115537);
    const uint32_t down = title_global<uint32_t>(0x008ce97c);  // g_anim_ladder_climbdown
    flagged_knob(ent, 0x9c115537, down);
    uint32_t delta[3] = {u, u, u};
    move_delta(delta, down);
    const float rate = static_cast<float>(as_float(delta[2]) / anim_length(ent, down));
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, ent);
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const float time =
        static_cast<float>((static_cast<long double>(as_float(end[2])) - as_float(origin[2])) / rate);
    climbing_time_text();
    note_tracks_for_time(ent, time, 0x9c115537);
    traverse_mode(ent, kGravity);
    *handle_slot(ent, kMove) = 0x00385d90;
    set_stance(ent, 0x070b89a4);
    *handle_slot(ent, kMove2) = 0x06279a34;
}

void XB_CDECL BroScr_Traverse_15(uint32_t ent) {
    set_pose(ent, 0xc1165808);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    const uint32_t up = title_global<uint32_t>(0x008ce9a0);   // g_anim_ladder_climbup
    const uint32_t off = title_global<uint32_t>(0x008ce988);  // g_anim_ladder_climboff
    flagged_knob(ent, 0x9c115537, up);
    const uint32_t u = undefined_lane();
    uint32_t off_delta[3] = {u, u, u};
    move_delta(off_delta, off);
    uint32_t end[3] = {u, u, u};
    path_node_or_origin(ent, end);
    // The height to climb to: the node minus the climb-off move, + 1.
    const float tx = as_float(end[0]) - as_float(off_delta[0]);
    const float ty = as_float(end[1]) - as_float(off_delta[1]);
    const float tz = as_float(end[2]) - as_float(off_delta[2]) + title_global<float>(0x0083eb20);
    end[0] = bits(tx);
    end[1] = bits(ty);
    end[2] = bits(tz);
    uint32_t up_delta[3] = {u, u, u};
    move_delta(up_delta, up);
    const float rate = static_cast<float>(as_float(up_delta[2]) / anim_length(ent, up));
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, ent);
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const float time = static_cast<float>((static_cast<long double>(as_float(end[2])) - as_float(origin[2])) / rate);
    note_tracks_for_time(ent, time, 0x9c115537);
    flagged_knob(ent, 0x9c115537, off);
    note_tracks(ent, 0x9c115537);
    traverse_mode(ent, kGravity);
    *handle_slot(ent, kMove) = 0x0001c6ce;
    set_stance(ent, 0xc1165808);
    *handle_slot(ent, kMove2) = 0x06279a34;
    blend_back(ent, body());
}

void XB_CDECL BroScr_Traverse_16(uint32_t ent) {
    set_pose(ent, 0x070b89a4);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    const uint32_t u = undefined_lane();
    uint32_t end[3] = {u, u, u};
    path_node_or_origin(ent, end);
    const float distance = flat_distance_to(end, ent);
    const uint32_t anim = stairs_anim(ent, 0x008cda4c, 0x008cda40);  // g_anim_climbstairs_down(_armed)
    flagged_knob(ent, 0x9c115537, anim, 0.3f, 0);
    stairs_play(ent, anim, distance);
}

void XB_CDECL BroScr_Traverse_17(uint32_t ent) {
    set_pose(ent, 0xc1165808);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    const uint32_t anim = stairs_anim(ent, 0x008cda64, 0x008cda58);  // g_anim_climbstairs_up(_armed)
    flagged_knob(ent, 0x9c115537, anim, 0.1f, 0);
    const uint32_t u = undefined_lane();
    uint32_t end[3] = {u, u, u};
    path_node_or_origin(ent, end);
    end[2] = bits(as_float(end[2]) + title_global<float>(0x0083eb20));
    const float distance = flat_distance_to(end, ent);
    stairs_play(ent, anim, distance);
}

void XB_CDECL BroScr_Traverse_8(uint32_t ent) {
    drop_until_gravity(ent, 0x008cf660);  // g_anim_step_up_low_wall
    blend_back(ent, root());
}

void XB_CDECL BroScr_Traverse_9(uint32_t ent) {
    set_pose(ent, 0x070b89a4);
    end_on_kill(ent);
    traverse_mode(ent, kNoGravity);
    traverse_mode(ent, kNoClip);
    Scr_Wait(1.0f);
    traverse_mode(ent, kGravity);
    note_tracks(ent, 0x99ca28c2);
    set_move_keys(ent, 0x0001c6ce, 0xc028584c);
    knob(ent, title_global<uint32_t>(0x008cda7c), body());  // g_anim_combatrun
    step_sounds(ent);
}

void XB_CDECL BroScr_TraverseDiveWall(uint32_t ent) {
    vault(ent, 0x008ce970, 0xaa613677);  // g_anim_jump_over_low_wall
    ScriptString sound;
    scr_string_from(&sound, 0x008263ec);  // "dive_wall"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0,
                                                                                           0);  // GScr_PlaySound
    scr_release(&sound);
    wait_gravity_on(ent, 0xaa613677);
    traverse_mode(ent, kGravity);
    note_tracks(ent, 0xaa613677);
    set_move_keys(ent, 0x0001c6ce, 0xc028584c);
    blend_back(ent, body());
    title_function<CallEnt6Anims>(0x001e53e0)(
        0, 0, 0, 0, 0x0019ff70, ent, pointer<const uint32_t>(0x008ce70c), pointer<const uint32_t>(0x008ce700),
        pointer<const uint32_t>(0x008ce73c), pointer<const uint32_t>(0x008ce748), pointer<const uint32_t>(0x008ce730),
        pointer<const uint32_t>(0x008ce724));  // BroScr_DirectionalMoveAnimsLoop(crouchrun_loop forward, back, left, right, forwardright, forwardleft)
    step_sounds(ent);
}

void XB_CDECL BroScr_Traverse_10(uint32_t ent) {
    vault(ent, 0x008cf6cc, 0xaa613677);  // g_anim_windowclimb
    wait_gravity_on(ent, 0xaa613677);
    traverse_mode(ent, kGravity);
    note_tracks(ent, 0xaa613677);
    set_move_keys(ent, 0x0001c6ce, 0xc028584c);
    blend_back(ent, body());
    step_sounds(ent);
}

void XB_CDECL BroScr_WaitThreeEndNotes(uint32_t ent, uint32_t notify, uint32_t done, uint32_t end) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, end);  // Scr_EndOn
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (int32_t count = 0; count < 3;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, notify,
                                                                                      &note);  // Scr_WaitTillOutputStr_ORAN
        if (ScriptString_Equals(&note, pointer<const char>(0x00832b28))) ++count;  // "end"
    }
    queue_notify(ent, done);
    scr_release(&note);
}

void XB_CDECL Bro_anim_main_grenade_recover(uint32_t ent) { begin_functor_thread(0x001ac400, ent); }  // BroScr_AnimScript_GrenadeRecover
void XB_CDECL Bro_climb_up_ledge(uint32_t ent) { begin_functor_thread(0x001aee80, ent); }       // BroScr_Traverse
void XB_CDECL Bro_fence_climb(uint32_t ent) { begin_functor_thread(0x001af0a0, ent); }          // BroScr_Traverse_2
void XB_CDECL Bro_fence_climb_39(uint32_t ent) { begin_functor_thread(0x001af480, ent); }       // BroScr_Traverse_3
void XB_CDECL Bro_fence_climb_48(uint32_t ent) { begin_functor_thread(0x001af860, ent); }       // BroScr_Traverse_4
void XB_CDECL Bro_jump_across_72(uint32_t ent) { begin_functor_thread(0x001b0070, ent); }       // BroScr_Traverse_5
void XB_CDECL Bro_jump_down(uint32_t ent) { begin_functor_thread(0x001b0310, ent); }            // BroScr_Traverse_12
void XB_CDECL Bro_jump_down_96(uint32_t ent) { begin_functor_thread(0x001b0550, ent); }         // BroScr_Traverse_13
void XB_CDECL Bro_jump_over_high_wall(uint32_t ent) { begin_functor_thread(0x001b0790, ent); }  // BroScr_Traverse_6
void XB_CDECL Bro_ladder_down(uint32_t ent) { begin_functor_thread(0x001b0bb0, ent); }          // BroScr_Traverse_14
void XB_CDECL Bro_ladder_up(uint32_t ent) { begin_functor_thread(0x001b0ed0, ent); }            // BroScr_Traverse_15
void XB_CDECL Bro_platform_climb(uint32_t ent) { begin_functor_thread(0x001b12c0, ent); }       // BroScr_Traverse_7
void XB_CDECL Bro_stairs_down(uint32_t ent) { begin_functor_thread(0x001b16a0, ent); }          // BroScr_Traverse_16
void XB_CDECL Bro_stairs_up(uint32_t ent) { begin_functor_thread(0x001b1990, ent); }            // BroScr_Traverse_17
void XB_CDECL Bro_step_down(uint32_t ent) { begin_functor_thread(0x001b1ca0, ent); }            // BroScr_Traverse_18
void XB_CDECL Bro_step_up(uint32_t ent) { begin_functor_thread(0x001b1ee0, ent); }              // BroScr_Traverse_8
void XB_CDECL Bro_train_jump(uint32_t ent) { begin_functor_thread(0x001b2150, ent); }           // BroScr_Traverse_9
void XB_CDECL Bro_wall_dive(uint32_t ent) { begin_functor_thread(0x001b2380, ent); }            // BroScr_TraverseDiveWall
void XB_CDECL Bro_windowclimb(uint32_t ent) { begin_functor_thread(0x001b26c0, ent); }          // BroScr_Traverse_10
void XB_CDECL Bro_hill_slide(uint32_t ent) { begin_functor_thread(0x001afc40, ent); }           // BroScr_Traverse_11

}  // namespace game
