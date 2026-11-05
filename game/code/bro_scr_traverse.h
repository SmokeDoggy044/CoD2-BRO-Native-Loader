#pragma once

// Compiled AI traversal anim scripts (levelscript module, .text 0x001aee80-0x001b2ec0 and their
// Bro_* entries at 0x001b8c60-0x001b8ea0): a pose, traverse modes ("nogravity", "noclip",
// "gravity"), a flagged traversal anim with note tracks, then the movement keys back. Every one
// waits (note tracks, Scr_WaitTill*): class thread_switch, kept out of kFunctions until script
// threads run as coroutines. Names are the title's script names (Bro_*) and ours (from the bodies).
//
// Keys (handles in the actor's key table): 0x8cb5047a the desired pose, 0x0ccee5c1 / 0x764918ae
// two movement keys (saved in 0x0ca29c7f / 0x7bc62f6e around some traversals), 0x682ea6e9 the
// anim the traversal blends back to (added as 0 when missing).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001aee80: pose 0x070b89a4; "nogravity", "noclip"; g_anim_c_climb_60units_dock with note
// tracks (0xb8092bb9); keys 0x1c6ce / 0xc028584c; blends back (body, 0.1 s).
void XB_CDECL BroScr_Traverse(uint32_t ent);
// 0x001af0a0 / 0x001af480 / 0x001af860 / 0x001b12c0: pose 0xc1165808, movement keys saved;
// "nogravity", "noclip"; the anim with note tracks (0xaa613677); "gravity"; keys restored;
// blends back; BroScr_MiscStepSounds thread (end 0x19791439). Anims: fenceclimb (twice),
// climb_wall_48, climbstation_platform_P.
void XB_CDECL BroScr_Traverse_2(uint32_t ent);
void XB_CDECL BroScr_Traverse_3(uint32_t ent);
void XB_CDECL BroScr_Traverse_4(uint32_t ent);
void XB_CDECL BroScr_Traverse_7(uint32_t ent);
// 0x001afc40: pose 0x070b89a4, keys saved; "noclip"; g_anim_c_slidingDown_runA played for the
// time the anim needs to cover the distance to the path node; "gravity"; keys restored.
void XB_CDECL BroScr_Traverse_11(uint32_t ent);
// 0x001b0070: pose 0x070b89a4; "nogravity", "noclip"; jump_across_72 (0x9d313600) until its
// "gravity on" note; "gravity"; the rest of its note tracks; keys; blends back (root, 0.2 s).
void XB_CDECL BroScr_Traverse_5(uint32_t ent);
// 0x001b0310 / 0x001b0550 / 0x001b1ca0: pose 0xc1165808, keys 0x3a09eb / 0x6279a34; "nogravity";
// the anim (0xcd3136c8) until "gravity on"; "gravity"; note tracks; crouchwalk_loop (root).
// Anims: jump_down_56, jump_down_96, 0x008cf654.
void XB_CDECL BroScr_Traverse_12(uint32_t ent);
void XB_CDECL BroScr_Traverse_13(uint32_t ent);
void XB_CDECL BroScr_Traverse_18(uint32_t ent);
// 0x001b0790: "nogravity", "noclip"; jump_over_high_wallA or B (even odds) until "gravity on",
// "noclip", until 0xdac03d50, "noclip", until "gravity on"; "gravity"; keys; blends back.
void XB_CDECL BroScr_Traverse_6(uint32_t ent);
// 0x001b0ab0: forever waits for `notify` notes: stops on 0x199a4 / 0x9aa82fee / 0xcc5542ad
// (hashes of the note), plays "step_run_dirt" on 0x19785ffb / 0x00385cd0.
void XB_CDECL BroScr_PlayStepRunSound(uint32_t ent, uint32_t notify);
// 0x001b0bb0: ladder down: ladder_climbon with note tracks, then ladder_climbdown for the time
// its move rate needs to reach the path node's height; "gravity"; keys 0x385d90 / 0x6279a34,
// stance (int +0x184) 0x070b89a4.
void XB_CDECL BroScr_Traverse_14(uint32_t ent);
// 0x001b0ed0: ladder up: ladder_climbup until the node height minus ladder_climboff's move
// (+ 1), then ladder_climboff with note tracks; "gravity"; keys, stance 0xc1165808; blends back.
void XB_CDECL BroScr_Traverse_15(uint32_t ent);
// 0x001b16a0 / 0x001b1990: stairs down / up: the (armed) stairs anim for the time its flat move
// needs to cover the flat distance to the path node; keys 0x3a09eb / 0x6279a34, stance 0x070b89a4.
void XB_CDECL BroScr_Traverse_16(uint32_t ent);
void XB_CDECL BroScr_Traverse_17(uint32_t ent);
// 0x001b1ee0: pose 0xc1165808, keys 0x3a09eb / 0x6279a34; "nogravity"; step_up_low_wall
// (0xcd3136c8) until "gravity on"; "gravity"; note tracks; blends back (root).
void XB_CDECL BroScr_Traverse_8(uint32_t ent);
// 0x001b2150: pose 0x070b89a4; "nogravity", "noclip"; waits 1 s; "gravity"; note tracks
// (0x99ca28c2); keys; combatrun (body); BroScr_MiscStepSounds thread.
void XB_CDECL BroScr_Traverse_9(uint32_t ent);
// 0x001b2380: pose 0xc1165808; "nogravity", "noclip"; jump_over_low_wall with "dive_wall",
// until "gravity on"; "gravity"; note tracks; keys; blends back; the crouch-run directional
// move thread (BroScr_DirectionalMoveAnimsLoop) and BroScr_MiscStepSounds.
void XB_CDECL BroScr_TraverseDiveWall(uint32_t ent);
// 0x001b26c0: windowclimb, as BroScr_TraverseDiveWall without the sound and the move thread.
void XB_CDECL BroScr_Traverse_10(uint32_t ent);
// 0x001b2e20: ends on `end`; waits for three "end" notes of `notify`, then notifies `done`.
void XB_CDECL BroScr_WaitThreeEndNotes(uint32_t ent, uint32_t notify, uint32_t done, uint32_t end);

// 0x001acac0 and 0x001b8c60-0x001b8ea0 (32 bytes each): begin the script thread as a functor
// thread (Scr_BeginThreadFunctor1(1, 0, 0, 0, body, ent)). Bodies: BroScr_AnimScript_GrenadeRecover
// and the traversals above, in the order of the header.
void XB_CDECL Bro_anim_main_grenade_recover(uint32_t ent);
void XB_CDECL Bro_climb_up_ledge(uint32_t ent);
void XB_CDECL Bro_fence_climb(uint32_t ent);
void XB_CDECL Bro_fence_climb_39(uint32_t ent);
void XB_CDECL Bro_fence_climb_48(uint32_t ent);
void XB_CDECL Bro_jump_across_72(uint32_t ent);
void XB_CDECL Bro_jump_down(uint32_t ent);
void XB_CDECL Bro_jump_down_96(uint32_t ent);
void XB_CDECL Bro_jump_over_high_wall(uint32_t ent);
void XB_CDECL Bro_ladder_down(uint32_t ent);
void XB_CDECL Bro_ladder_up(uint32_t ent);
void XB_CDECL Bro_platform_climb(uint32_t ent);
void XB_CDECL Bro_stairs_down(uint32_t ent);
void XB_CDECL Bro_stairs_up(uint32_t ent);
void XB_CDECL Bro_step_down(uint32_t ent);
void XB_CDECL Bro_step_up(uint32_t ent);
void XB_CDECL Bro_train_jump(uint32_t ent);
void XB_CDECL Bro_wall_dive(uint32_t ent);
void XB_CDECL Bro_windowclimb(uint32_t ent);
void XB_CDECL Bro_hill_slide(uint32_t ent);

}  // namespace game
