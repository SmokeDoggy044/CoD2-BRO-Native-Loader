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

// 0x000c40c0: calls callback(handle) for every voice of the pool 0x0090a8b4 (0xf8-byte records at 0x0090a8e0,
// read after each resolve); returns the smallest byte +0xd4 below 0 - always 0.
uint32_t XB_CDECL SndVoice_ForEach(void(XB_CDECL* callback)(int32_t));
// 0x000c52b0: SndVoice_ForEach callback: the voice's attenuation flag +0xf4 = 0.
void XB_CDECL SndVoice_ClearAttenuation(int32_t voice);
// 0x000c53b0 (not hookable: PUSH + CALL in its first 5 bytes): SndVoice_ForEach(SndVoice_ClearAttenuation).
uint32_t S_ClearVoiceAttenuation();

// 0x00296a80 (effect list in ESI, id in EDI): the running effect whose id (+0x4c) is `id`, or null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL FX_FindEffectById(const uint32_t* list, uint32_t id);
extern "C" void FX_FindEffectById_entry(void);
// 0x002ea4f0 (stdcall (level, frame time); nothing with time 0): the level's ambient sound emitters (99 groups at
// level +4, 0x60-byte emitters at +0x34, count +0x30): state 0 counts +0x58 down by 1 and starts the alias (+0x40)
// unless its linked trigger entry (+0x44, resolved through level +0x190) is off; 1 waits for the effect; 2 frees it
// when the trigger goes off, or repeats after a random wait (+0x4c..+0x4e hundredths, flag +0x48 bit 0); 3 counts
// the wait down by the frame time; 4 waits for the trigger; 5 = done. A running effect's components get the
// emitter (vtable +0x24) every frame.
void XB_STDCALL S_UpdateAmbientEmitters(uint8_t* level, float frame_time);

// 0x00013890 (handle pointer in EAX): the sound channel of a handle (index = low 12 bits below 0x200, generation =
// the rest, table 0x00962860: {channel, generation}), or null.
uint8_t* XB_REGS1 XB_CDECL SoundHandle_Resolve(const uint32_t* handle);
// 0x002c8830 (crossfade in EAX, frame time on the stack, ret 4): state 1 fades handle +0 out and +4 in (volumes
// +0xc / +0x10 = 1 - t/len and t/len, time +0x18, length +0x14), state 2 the other way and stops channel +4 at the
// end; both channels' voices get param 0 = their base (+0x1c) * GetParam(0) * master (+8) * volume, the channel
// +0x10 the unscaled product (kept in the time argument's slot, as in the original).
void XB_REGS1 XB_STDCALL S_UpdateCrossfade(uint8_t* fade, float time);

// 0x000c3500: the master attenuation 0x008bd650 = -|value| (returned).
int32_t XB_CDECL S_SetMasterAttenuation(int32_t value);
// 0x000c4400: a voice's state: 0 without a record, 5 when stopping (+0xd4 = 5 or +0xd7 bit 1), 4 with +0xd6 bit 1,
// 2 or 3 (+0xd4 = 3) with +0xd6 bit 0, else +0xd4.
uint32_t XB_CDECL SndVoice_GetState(int32_t voice);
// 0x000c44f0: param < 0x2e of a voice = value (bits; +0x10 + 4 * param) and its bit in the dirty mask +8 (64-bit).
void XB_CDECL SndVoice_SetParam(int32_t voice, uint32_t param, uint32_t value_bits);
// 0x000c4690: a voice's position (+0x5c) = origin, dirty bits 0x380000.
void XB_CDECL SndVoice_SetOrigin(int32_t voice, const uint32_t* origin);
// 0x000c58b0: the 40-byte sound entry of a handle: bank slot (handle | 0xffff) >> 16 % 12 in 0x008bd770 (32 bytes:
// key, state 3 = loaded, +0x18 bank: count +0x40, entries +0x44), index handle & 0xffff (at most the count); null
// otherwise.
uint8_t* XB_CDECL SndVoice_Resolve(uint32_t handle);

// 0x000c5810: the handle of a sound by name (the argument is a name pointer; bsearch with stricmp over each loaded
// bank's sorted 40-byte entries, banks = the 12 slots of 0x008bd770): (slot key & 0xffff0000) | index; -1 if none.
int32_t XB_CDECL S_FindSoundByHash(const char* name);
// 0x000c62e0: the sound group (0x128 bytes at 0x008bd8f0, name +0x108, count 0x008beb70) with that name (strnicmp
// 32), else a new one (at most 15: then null) with ids -1 and both volumes 1.0. Null for a null name.
uint8_t* XB_CDECL SndGroup_FindByName(const char* name);

// 0x0001eee0 (table in ESI; handle on the stack, ret 4): frees a live handle of a 0x200-entry handle table (free
// bits at +0, {pointer, generation} pairs at +0x40): free bit set, generation + 1, pointer 0.
extern "C" PLATFORM_HIDDEN void XB_CDECL SoundHandle_Free(uint8_t* table, uint32_t handle);
extern "C" void SoundHandle_Free_entry(void);
// 0x000c5970: the alias name of a sound handle (its entry's first word), or null.
const char* XB_CDECL Snd_GetAliasName(uint32_t handle);
// 0x002c3400 (channel in EDI): frees the channel's voice (twice, through the thunk 0x000c4140 and directly) and its
// sound handle (+0x24, table 0x00962820); with a live entity (+0x20 handle, table 0x0097a968) sends that entity's
// pending sound notify (+0x3c4 when +0x3c8 = the alias name's hash) and the channel's own (+0x2c), then resets the
// channel (voice/alias -1, volumes 1.0, near 50, far 1500, free flag +9).
extern "C" PLATFORM_HIDDEN void XB_CDECL S_StopChannel(uint8_t* channel);
extern "C" void S_StopChannel_entry(void);

// 0x002c8bc0 (handle pointer in ESI): stops a live handle's channel (S_StopChannel) and clears the handle.
extern "C" PLATFORM_HIDDEN void XB_CDECL S_StopSoundHandle(uint32_t* handle);
extern "C" void S_StopSoundHandle_entry(void);
// 0x002c8d60 (sound system in EBX): S_StopChannel on all 0x200 channels (0x30 bytes each), stops the crossfade's
// first channel (0x0095f4cc) and resets the crossfade (state 2, length and time 0), then S_StopAllChannels.
extern "C" PLATFORM_HIDDEN void XB_CDECL S_StopAllSounds(uint8_t* system);
extern "C" void S_StopAllSounds_entry(void);

// 0x000c5100: every source of the pool 0x0090a8e4 (0xc8-byte records at 0x0090a910) merges its pending dirty mask
// (+8, 64-bit) into +0 and clears it. Per-frame sound update (0x000c3700).
void XB_CDECL SndSource_CommitDirty();
// 0x000c8580: releases hardware voice slot `slot` (< 0x008bed00; 0xe0-byte slots at 0x00911548): +0xc8 = -1, state
// +0xc0 1 -> 0, any other nonzero state gets the release bit 8 in +0xc1.
void XB_CDECL SndHw_ReleaseSlot(uint32_t slot);
// 0x000cb200 (not hookable: CALL in its first 5 bytes): the sound clock 0x0092082c = CRT clock() * 1000 / 1000 (64-bit; milliseconds), returned.
uint32_t XB_CDECL Snd_UpdateTime();

}  // namespace game
