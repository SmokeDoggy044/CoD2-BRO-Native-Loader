#pragma once

// Compiled level-script thread bodies (levelscript module): the checkpoint / intro-screen fade
// (black screen with up to four text lines) and the letterbox bars' animation. They suspend the
// script thread (Scr_Wait / Scr_WaitFrames), so they are not in kFunctions (class thread_switch in
// docs/research/levelscript_classes.csv).

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x00125d30: a full-screen "black" hud element (level +0x2e0, x/y 0; with cvar "checkpoint" other
// than "0" moved off screen at -777), the player's int 0x58c set. With "checkpoint" "0": after 2 s
// ("tunisia") or 0.1 s, each non-empty line fades in (`line3` first, at y 200 with font scale 2,
// then `line0`..`line2` at y 260 / 290 / 320, scale 1.5; level +0x384 / +0x31c / +0x320 / +0x5d8),
// 2.2 s more (+ level float +0x5ac when defined and positive), notifies 0xcc232f0f, 1 s, waits while
// level byte +0x260 is set, fades the black out over 1.5 s (0x5da71cc2), 0.5 s, player int 0x58c
// cleared (0xd06e3679), the lines faded out, 1.6 s, all destroyed. Otherwise the short form (2 s,
// the black faded out, the same notifies). Releases the four lines.
void XB_CDECL BroScr_CheckpointFade(ScriptString line0, ScriptString line1, ScriptString line2, ScriptString line3);
// 0x001266f0: animates the letterbox bars (level huds +0x79c "black" shader height, +0x270 y at 480 -
// height) over `duration` seconds of frame time: `closing` set grows the height from 0 to
// `height` * elapsed / `total`, else shrinks it from `height` and then destroys both huds. Nothing
// but the destroy (when not closing) for an undefined or non-positive duration.
void XB_CDECL BroScr_FadeHudBlack(float total, float duration, uint32_t closing, float height);
// 0x00130ab0: BroScr_CheckpointFade with three localized lines (keys) and an empty fourth.
void XB_CDECL BroScr_CheckpointFadeLocalized(uint32_t key0, uint32_t key1, uint32_t key2);
// 0x00130a10: BroScr_CheckpointFade with four localized lines (keys), built last first.
void XB_CDECL BroScr_SetIntroScreenLine(uint32_t key0, uint32_t key1, uint32_t key2, uint32_t key3);

}  // namespace game
