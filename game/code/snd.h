#pragma once

// Sound helpers: the voice records (0x00911548) and the sound references FX sound components
// and script sounds keep ({voice handle, alias, flag +8, byte +9, volume +0x10, base volume
// +0x1c, handle +0x24, source +0x28}; 0x30 bytes, tables of 0x200).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x000c8450: the number of voice records (0x008bed00).
uint32_t XB_CDECL SndVoice_GetCount();
// 0x000c8470: voice record `index` (0x00911548, 0xe0 bytes; handle at +0xcc).
uint8_t* XB_CDECL SndVoice_Get(uint32_t index);
// 0x00293310 (not hookable: CALL in its first 5 bytes): the voices whose state is nonzero.
int32_t Snd_CountBusyVoices();

// 0x0001c340 (ref in EAX): SndVoice_GetParam(voice +4, 0, 1.0).
float XB_REGS1 XB_CDECL SoundRef_GetParam0(const uint8_t* ref);
// 0x00023e50 (ref in EAX): SndVoice_GetParam(voice +4, 0x1a, 1.0).
float XB_REGS1 XB_CDECL SoundRef_GetParam1a(const uint8_t* ref);
// 0x002adea0 (ref in ESI; ret 4): with a voice, parameter 0 = base volume (+0x1c) * scale;
// +0x10 = scale.
extern "C" PLATFORM_HIDDEN void XB_CDECL SoundRef_SetVolumeScale(uint8_t* ref, float scale);
extern "C" void SoundRef_SetVolumeScale_entry(void);
// 0x002add40 (ref in EAX): the voice is in state 3 (AL; 0 without a voice).
uint8_t XB_REGS1 XB_CDECL SoundRef_IsState3(const uint8_t* ref);
// 0x002b6c80 (ref in ESI): (voice state 2-5 or flag +8) and 0x000c5a80(alias) -> 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SoundRef_IsBusy(const uint8_t* ref);
extern "C" void SoundRef_IsBusy_entry(void);
// 0x002b6cd0 (ref in EAX): no flag +8 and no voice or a state other than 2-5 (AL).
uint8_t XB_REGS1 XB_CDECL SoundRef_IsDone(const uint8_t* ref);
// 0x002bd4e0 (source in EAX, ref in ECX): +0x28 = source; with a voice, its position =
// source +0x30 (0x000c4690).
void XB_REGS3 XB_CDECL SoundRef_SetSource(const uint8_t* source, uint32_t unused_edx, uint8_t* ref);
// 0x002c3120 (alias in EAX, ref table in ECX; out + 9 words, ret 0x28): a free reference
// (0x002bd3b0), started by S_StartAliasVoice2 (S_PlaySoundAlias' pattern); out = its handle
// (+0x24) or 0 (also with alias -1). Returns out.
uint32_t* XB_REGS3 XB_STDCALL S_PlaySoundAlias2(uint32_t alias, uint32_t unused_edx, uint8_t* table, uint32_t* out,
                                                uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                                uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9);

}  // namespace game
