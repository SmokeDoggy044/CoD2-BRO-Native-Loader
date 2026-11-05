#pragma once

// Compiled level-script sound/effect helpers.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0015bcf0: GScr_PlaySound(entity, the sound alias the level's sound map (0x00922ee0 +0x458)
// holds for key, 0, 0, 0). The key is passed by value and not released here.
void XB_CDECL BroScr_PlaySoundOn(uint32_t entity, ScriptString key);

// 0x001660b0: a thread (Scr_StartThreadFunctor1) on BroScr_StartLoopingFx with the word at +4 of
// the level object 0x00922ee0 (a null object faults, as in the original).
void XB_CDECL BroScr_StartFxLoopingThread();

// BroScr_RandomSoundBursts threads (Scr_StartThread_ent_hash_i_i_f_f_f_f_hash, 0x0017b0c0, which
// copies each argument through its pointer). Missing arguments are the script's undefined
// values: 0x0015fd60 (entity, alias), 0x0015fde0 (entity, alias, 4 more), 0x0015fe50 (all 8).
void XB_CDECL BroScr_RandomSoundBurstsThread(uint32_t entity, uint32_t alias);
void XB_CDECL BroScr_RandomSoundBurstsThread_2(uint32_t entity, uint32_t alias, uint32_t a3, uint32_t a4, uint32_t a5,
                                               uint32_t a6);
void XB_CDECL BroScr_RandomSoundBurstsThread_3(uint32_t entity, uint32_t alias, uint32_t a3, uint32_t a4, uint32_t a5,
                                               uint32_t a6, uint32_t a7, uint32_t a8);

}  // namespace game
