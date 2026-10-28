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
// 0x00293310: the voices whose state is nonzero.
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
// 0x000c53b0: SndVoice_ForEach(SndVoice_ClearAttenuation).
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
uint8_t* XB_CDECL SndBank_GetSoundEntry(uint32_t handle);

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
// 0x000cb200: the sound clock 0x0092082c = CRT clock() * 1000 / 1000 (64-bit; milliseconds), returned.
uint32_t XB_CDECL Snd_UpdateTime();

// 0x000c39a0: the number of set bits of a 64-bit mask (low, high words; SWAR adds).
uint32_t XB_CDECL Snd_PopCount64(uint32_t low, uint32_t high);
// 0x000c3f00: releases the hardware slot (+0xd0, -1 = none) of a voice of the pool 0x0090a8b4 (0xf8-byte records).
void XB_CDECL SndVoice_ReleaseHwSlot(int32_t handle);
// 0x000c63a0 / 0x000c6390: the sound group table 0x008bd8f0 / the mix state 0x008bda18.
uint8_t* XB_CDECL Snd_GetGroups();
uint8_t* XB_CDECL Snd_GetMixState();
// 0x000c8d80: a request record: +4 flags (| 2 when the last word is not -1), +8 / +0xc -1, +0x10 / +0x14, +0x18 the
// 0x1000-byte buffer 0x0091f780 (filled with 0xbf), +0x20, +0 = 0.
uint32_t XB_CDECL Snd_InitRequest(uint8_t* request, uint32_t flags, uint32_t a, uint32_t b, int32_t c);
// 0x000c7ea0 (out in EAX, in in ECX; both kept): out = in / |in| (x87 length), zero when |in| <= 0x007ce5c8.
extern "C" PLATFORM_HIDDEN float* XB_CDECL Snd_NormalizeVector(float* out, const float* in);
extern "C" void Snd_NormalizeVector_entry(void);

// 0x000c84b0 (sound handle, owner, any-slot flag): the first free hardware slot (0x00911548, 0xe0 bytes) of the
// sound's type (0x000c6fb0; slots 0/1 only with the flag, the last slot never) - cleared, +0xc8 sound, +0xcc owner,
// +0xc0 in use, +0xc1 0x40, +0xc2 type, +0xc3/+0xc4 entry bytes 5/6, 0x0091f678[type] = slot + 1; -1 if none.
int32_t XB_CDECL SndHw_AllocSlot(uint32_t sound, uint32_t owner, uint32_t any_slot);
// 0x000c85c0: the listener axes from the mix state (+0xc0..+0xd4): three cross products normalized (0x0091f654,
// 9 floats), then the two vectors 0x008bd668 / 0x008bd688 expressed in them (0x008bd678 / 0x008bd698).
void XB_CDECL Snd_UpdateListenerAxis();

// 0x000c4af0: per voice of the pool 0x0090a8b4 - its
// source's dirty parameters (pool 0x0090a8e4) copied in, distance cull against the range (+0x78; hardware slot
// released past range * 0x0083f278), a hardware slot from SndHw_AllocSlot (freed after 10 failures), the state
// machine bytes +0xd4..+0xd7 into the slot's +0xc0/+0xc1, dirty parameters copied to the slot, volume / pitch /
// +0x7c scaled by the entry's curve (+0x14, Snd_PopCount64 index), group (+0x10), the global groups and 0x008bd768.
void XB_CDECL SndVoice_ApplyParams();
// 0x000c5ca0: frees the sound bank with that key (slot (key >> 16) % 12 of 0x008bd770): a loading one cancels the
// request (0x000c91e0, tail call), a loaded one frees its data (0x000c8380 for +0x88, Mem_AlignedFree); the slot
// key advances by 0xc0000, state 0.
uint32_t XB_CDECL SndBank_Free(uint32_t key);
// 0x000c6080: bank loading per frame - polls the request 0x0090a914 (0x000c8fa0) for the loading slot (1: loaded,
// entries qsorted with 0x000c5790; < 0: freed), then starts the queued slot with the lowest priority (+0x14).
void XB_CDECL SndBank_UpdateLoads();

// 0x000c8fa0 (request 0x0090a914: +0 state, +4 flags, +8 file handle, +0x10..+0x18 file / offset / buffer, +0x1c
// size, +0x20 / +0x24 second part): the bank load state machine - header read (0x1000 bytes, magic at 0x007ce6bc +
// "11", else -5), the data buffer (0x000c37f0) and a page-rounded second part (0x000c8250 unless flag 2; -4 when
// either fails), both reads, then 1 and 0x000c8de0. 0x100: wait for the handle, then -1. File I/O through
// File_QueueRequest / 0x000c9db0. Returns the state.
int32_t XB_CDECL SndBank_PollLoad(uint8_t* request);

// 0x000c34f0: |master attenuation| (0x008bd650; CDQ/XOR/SUB).
uint32_t XB_CDECL Snd_GetSpeakerMode();
// 0x000c3550 (alignment in ECX, size on the stack): bump allocator over the arena {base 0x0090a890, used +4, size +8}:
// returns base + used rounded up to the alignment, used += size + alignment - 1; 0 when that passes the size, 1 (and
// only the count) without a base.
uint32_t XB_REGS3 XB_CDECL SndArena_Alloc(uint32_t unused_eax, uint32_t unused_edx, uint32_t alignment, uint32_t size);
// 0x000c36e0: SndArena_EmptyStub, then the arena's base, used and size = 0.
void XB_CDECL SndArena_Reset();
// 0x000c7010 / 0x000cb0f0 / 0x000c72f0: empty (a bare RET; offline-tested only).
void XB_CDECL SndArena_EmptyStub();
void XB_CDECL Snd_Printf();
void XB_CDECL SndSys_EmptyStub();
// 0x000c37f0: Mem_AlignedAlloc(size, 0x40, 0).
void* XB_CDECL SndBank_AllocAligned64(uint32_t size);
// 0x000c8180: the sound memory's two block stacks (64 sizes at 0x0091f548, bit 31 = freed; the low stack grows up
// to 0x008bed08, the high one down from 0x40 to 0x008bed04): pops freed blocks off both tops (zeroing them), then
// *low_end = the low blocks' total, *high_end = size - the high blocks' total (either may be null); the free space
// 0x0091f650 = high_end - low_end (returned).
uint32_t XB_CDECL SndMem_Compact(uint32_t* low_end, uint32_t* high_end);
// 0x000c8250 (flags, size): a block of size rounded up to 576 bytes (signed), from the top of the high stack
// (flags bit 0) or the low one; null when there is no room or both stacks are full.
uint32_t XB_CDECL SndMem_Alloc(uint32_t flags, int32_t size);
// 0x000c8300 / 0x000c8310: the sound memory's base 0x0091f648 / size 0x0091f64c.
uint32_t XB_CDECL SndMem_GetBase();
uint32_t XB_CDECL SndMem_GetSize();
// 0x000c8320: base = address rounded up to 576, size = (size - 576) rounded down to 576, both stacks empty.
void XB_CDECL SndMem_Init(uint32_t address, uint32_t size);
// 0x000c8380: marks the block starting at `address` freed (low stack first, then the high one) and compacts.
void XB_CDECL SndMem_Free(uint32_t address);
// 0x000c6240 (out in EAX, in in ECX): Snd_NormalizeVector with another store order; ECX kept.
extern "C" PLATFORM_HIDDEN float* XB_CDECL Snd_NormalizeVectorDup(float* out, const float* in);
extern "C" void Snd_NormalizeVectorDup_entry(void);
// 0x000c65c0: the listener position (mix state +0x54) = origin, dirty bits 0x380000.
void XB_CDECL Snd_SetListenerOrigin(const uint32_t* origin);
// 0x000c6640: the listener axes (mix state +0xc0 = a, +0xcc = b; b copied first), normalized in place; dirty bits
// 0xfc000 in the mask's high word.
void XB_CDECL Snd_SetListenerOrientation(const uint32_t* a, const uint32_t* b);
// 0x000c6710 (format in EAX): the number of bits of the channel mask byte +6 (1 when 0); 0 for null.
uint32_t XB_REGS1 XB_CDECL SndFormat_GetChannelCount(const uint8_t* format);
// 0x000c5990: the group name (+0x108 of the entry's group +0x10) of a sound handle, or null.
const char* XB_CDECL SndBank_GetEntryGroupName(uint32_t handle);
// 0x000c5a80: bit 3 of the sound entry's flag byte +5 (0 without an entry).
uint32_t XB_CDECL SndBank_GetEntryFlag3(uint32_t handle);
// 0x000c8c10 / 0x000c5560: copies of Snd_PopCount64.
uint32_t XB_CDECL Snd_PopCount64_2(uint32_t low, uint32_t high);
uint32_t XB_CDECL Snd_PopCount64_3(uint32_t low, uint32_t high);
// 0x000c8d30 (mask pointer in EAX, bit on the stack): the index of a set bit among the mask's set bits, -1 if unset.
int32_t XB_REGS1 XB_CDECL Snd_MaskBitIndex64(const uint32_t* mask, uint32_t bit_low, uint32_t bit_high);
// 0x000c5c20: a bank slot's load state: -1 when the key is not the slot's, else 1 while not loaded (state 3).
int32_t XB_CDECL SndBank_IsLoading(uint32_t key);
// 0x000c5b90: takes the first free bank slot (state 0 at +4; 12 slots of 0x20 at 0x008bd770): key = next
// generation * 12 + slot in the high half | 0xffff, state 1, +8 a, +0xc file, +0x10 b, +0x14 serial 0x0090a93c++,
// +0x18 0, +0x1c c. Returns the key, -1 for file -1 or no free slot.
uint32_t XB_CDECL SndBank_AddLoadRequest(uint32_t a, int32_t file, uint32_t b, uint32_t c);
// 0x000c6180: SndBank_AddLoadRequest(a, file, b, -1).
uint32_t XB_CDECL SndBank_LoadFromFile(uint32_t a, int32_t file, uint32_t b);
// 0x000c57b0 (name in EAX, bank in ESI; no callers): the index of a name in the bank's sorted entries (bsearch,
// "" for null), -1 if none.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL SndBank_FindSound(const char* name, const uint8_t* bank);
extern "C" void SndBank_FindSound_entry(void);
// 0x000c8090 (x in XMM0, y in XMM1, result in XMM0; only caller the uncalled 0x000c88c0): an octant approximation
// of atan2 scaled by 0x0083eea0.
extern "C" PLATFORM_HIDDEN float XB_CDECL Snd_ApproxAngle2D(float x, float y);
extern "C" void Snd_ApproxAngle2D_entry(void);
// 0x000c35a0: the voice and source pools in the sound arena (only while nothing is allocated, else -1): config
// {voices, sources, +8, +0xc} (copied to 0x008bd654; null keeps the last), a 16-byte header (0x0090a89c), per voice
// 12 + 0xf8 bytes (0x0090a8dc / 0x0090a8e0), per source 12 + 0xc8 (0x0090a90c / 0x0090a910), 16 more
// (0x0090a8a0). Returns the arena's used bytes, or -1 when an allocation fails with a base (without a base the
// arena only counts: a sizing pass).
uint32_t XB_CDECL SndVoice_AllocPools(const uint32_t* config);
// 0x000c3d00: a new voice (pool 0x0090a8b4) for a sound: record cleared, sound +0xc8, owner +0xcc -1, state +0xd4
// 1, play length +0xe0 = entry +0xc * 1000 / entry +0x20 (64-bit); streamed sounds (entry +5 bit 0) get defaults
// for params 0x19/0x1a/0x1c (bit 0x40 of +0xd6 with `any_slot`), others a hardware slot (+0xd0; none and not
// `any_slot`: the handle is freed, -1); then the entry's param set (+0x14) unpacked and params 0, 1, 0x1b = 1.0 with
// their dirty bits. Returns the handle, -1 without an entry or record.
int32_t XB_CDECL SndVoice_Create(uint32_t sound, uint32_t any_slot);
// 0x000c3f50: releases a voice's hardware slot (+0xd0) and its handle.
void XB_CDECL SndVoice_Free(int32_t voice);
// 0x000c3fb0 / 0x000c3ff0: a voice's play flags +0xd6 |= 1 (queued) / |= 3 (queued and playing).
void XB_CDECL SndVoice_Queue(int32_t voice);
void XB_CDECL SndVoice_Play(int32_t voice);
// 0x000c41b0 / 0x000c4220: a voice's pause count +0xd2 up / down (not below 0; Resume needs a sound entry); on the
// first pause a stopping voice (state 5) loses bit 2 of +0xd7, others get bit 1, and the last resume undoes it.
void XB_CDECL SndVoice_Pause(int32_t voice);
void XB_CDECL SndVoice_Resume(int32_t voice);
// 0x000c47d0: a voice's velocity (+0x68) = the vector, dirty bits 0x1c00000.
void XB_CDECL SndVoice_SetVelocity(int32_t voice, const uint32_t* velocity);
// 0x000c4910: a voice's source (+0xcc; -1 without a source record): the source's params (+0x10 + 4 * i) that are set
// in either of its masks (+0/+8, 64-bit) are copied and those bits added to the voice's dirty mask.
void XB_CDECL SndVoice_SetSource(int32_t voice, int32_t source);
// 0x000c5390: the voice volume scale 0x008bd768 = scale, then SndVoice_ForEach(0x000c5270) (returned).
uint32_t XB_CDECL Snd_SetVoiceVolumeScale(uint32_t scale_bits);
// 0x000c5400: the sound entry of a voice's sound (+0xc8); -1 (as a pointer) for a dead handle.
uint8_t* XB_CDECL SndVoice_GetSound(int32_t voice);
// 0x000c59b0: param `param` (< 0x2e) of a sound's param set (entry +0x14: 64-bit mask, then the set params' values),
// else `fallback` (ST0).
float XB_CDECL SndVoice_GetParam(uint32_t sound, uint32_t param, float fallback);
// 0x000c6490: the "master" group (0x007ce5cc) gets volume +8 and flag bit 0, then S_SetMasterAttenuation(
// Snd_GetSpeakerMode()) (returned).
int32_t XB_CDECL S_SetMasterVolume(uint32_t volume_bits);
// 0x000c64d0: the "master" group's volume (+8), 0 without the group (ST0).
float XB_CDECL Snd_GetMasterGroupVolume();
// 0x002b6ba0 / 0x002b6be0 (channels in EAX): every one of the 0x200 channels (0x30 bytes) with a voice resumes /
// pauses it and gets paused flag +8 = 0 / 1.
void XB_REGS1 XB_CDECL S_ResumeAllChannels(uint8_t* channels);
void XB_REGS1 XB_CDECL S_PauseAllChannels(uint8_t* channels);
// 0x000c91e0: a bank load request not idle (-1) or cancelled (0x100) aborts its file request (+8) and becomes
// cancelled.
void XB_CDECL SndBank_CancelLoad(int32_t* request);
// 0x002bd520 / 0x002bd770 (byte in AL, channel in ESI, alias in EDI; volume, pitch, near, far (float bits; <= 0 =
// the sound's param 0 / 1 / 0x19 / 0x1a, defaults 1, 1, 50, 1500), origin, velocity, owner, any_slot; ret 0x20):
// channel alias +4, byte +9, voice +0 (SndVoice_Create; nothing more without one); the voice gets params 0 (base
// +0x1c = 1.0 times the volume), 1, 0x19, 0x1a and 0x1c = 4.0, its origin and velocity; the channel keeps volume
// +0x10, pitch +0xc, near +0x14, far +0x18, owner +0x20. The first starts the voice (SndVoice_Play), the second only
// queues it (SndVoice_Queue).
extern "C" PLATFORM_HIDDEN void XB_CDECL S_StartAliasVoice(uint32_t eax, uint8_t* channel, uint32_t alias, uint32_t volume,
                                                           uint32_t pitch, uint32_t near_bits, uint32_t far_bits,
                                                           const uint32_t* origin, const uint32_t* velocity,
                                                           uint32_t owner, uint32_t any_slot);
extern "C" void S_StartAliasVoice_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL S_StartAliasVoice2(uint32_t eax, uint8_t* channel, uint32_t alias,
                                                            uint32_t volume, uint32_t pitch, uint32_t near_bits,
                                                            uint32_t far_bits, const uint32_t* origin,
                                                            const uint32_t* velocity, uint32_t owner, uint32_t any_slot);
extern "C" void S_StartAliasVoice2_entry(void);
// 0x002bd3b0 (channel table in ECX): the first channel without a voice (0x200 of 0x30 bytes), -1 if none; its old
// handle (+0x24) is freed and a new one allocated (HandleTable512_Alloc, table 0x00962820) that points at it.
int32_t XB_REGS3 XB_CDECL S_AllocChannelHandle(uint32_t unused_eax, uint32_t unused_edx, uint8_t* table);
// 0x002c30a0 (alias in EAX, channel table in ECX; out + 9 words, ret 0x28): S_PlaySoundAlias2 with
// S_StartAliasVoice, and nothing while cls_state (0x0092bdf0) is 1.
uint32_t* XB_REGS3 XB_STDCALL S_PlaySoundAlias(uint32_t alias, uint32_t unused_edx, uint8_t* table, uint32_t* out,
                                               uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                               uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9);
// 0x00025f70 (channel table in EAX; out, name, owner, any_slot; ret 0x10): the sound by name on a free channel
// (S_StartAliasVoice2, all four values from the sound, origin 0x0082e290, velocity 0x0082e110); out = its handle or
// 0. Returns out.
uint32_t* XB_REGS1 XB_STDCALL S_PlayLocalSoundByHash(uint8_t* table, uint32_t* out, const char* name, uint32_t owner,
                                                     uint32_t any_slot);
// 0x002c8d30 (channels in EAX): S_StopChannel on every channel whose paused flag +8 is clear.
void XB_REGS1 XB_CDECL S_StopUnpausedChannels(uint8_t* channels);
// 0x002ce6a0 (handle pair in EAX): S_StopChannel on the live channels of both handles (the crossfade's).
void XB_REGS1 XB_CDECL S_StopCrossfadeChannels(const uint32_t* handles);
// 0x002bd140 (crossfade in EAX, volume in XMM0): +8 = volume; both channels' voices get param 0 = base (+0x1c) *
// (GetParam(0) * their fade (+0xc / +0x10) * volume), the channel +0x10 the product.
extern "C" PLATFORM_HIDDEN void XB_CDECL S_SetHandleVolume(uint8_t* fade, uint32_t volume_bits);
extern "C" void S_SetHandleVolume_entry(void);
// 0x002bdbd0 (bank list in EAX; 32-byte name hash on the stack): 0 when the 0x84-byte entry with that hash (+0xc,
// +8 count) has one of its six states (+0x30..+0x44) at 1 or 3 (still loading), else 1 (also when none matches).
uint8_t XB_REGS1 XB_STDCALL SndBankList_IsLoaded(const uint8_t* list, const uint32_t* hash);
// 0x002ce9c0 (bank manager in EDI): S_CheckWaveBankNotifies until no wave bank state (+0x30.., 6 per 0x84-byte
// bank) is 1 or 3.
extern "C" PLATFORM_HIDDEN void XB_CDECL S_WaitForWaveBanks(uint8_t* manager);
extern "C" void S_WaitForWaveBanks_entry(void);
// 0x002ce830 (see the entry): opens wave bank `index` of a bank entry (file +0x54, key +0x6c, state +0x24 = 1;
// SndBank_LoadFromFile with the name +0x3c) and, unless no_wait, waits for it and reports progress; failures set
// manager +5.
extern "C" PLATFORM_HIDDEN void XB_CDECL S_OpenWaveBank(const char* path, uint8_t* bank, uint32_t index,
                                                        uint8_t* manager, uint32_t no_wait);
extern "C" void S_OpenWaveBank_entry(void);
// 0x002ce8f0 (stdcall manager, 32-byte name, no_wait): the manager's bank (0x84 bytes from +0xc, count +8) with that
// name opens its common wave bank (5) and the one of the language 0x0095f5dc from its zone's path (+0x20);
// manager +5 = 1 for an unknown name.
void XB_STDCALL S_LoadSoundBank(uint8_t* manager, const uint32_t* name, uint32_t no_wait);

// --- Sound options menu (vtable 0x00829fa0; UIMenu subclass, ui_menu.h) ---
// Fields after the UIMenu base: +0x34 the panel, +0x38 the help bar text, +0x3c the volume slider (+0x20 -> its
// quads: background +0x00, bar +0x40; +0x34 width), +0x40 the volume 0..50.

// 0x00330f80 (owner in EAX, menu in ECX): UIMenu_Init (3 items at (320, 240), 8, no flags), flags +0x2a |= 0x180,
// the vtable, +0x33 = 5, volume 50. Returns the menu.
uint8_t* XB_REGS3 XB_CDECL SoundMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
// 0x00012950 (menu in EAX): the panel deleted (Panel_Delete's steps), the help bar deleted (vtable +0x00 (1)), then
// the UIMenu part (UIMenu_FreeItems).
void XB_REGS1 XB_CDECL SoundMenu_Dtor(uint8_t* menu);
// 0x00012a60: the destructor, Z_Free with flags bit 0. Returns the menu.
uint8_t* XB_THISCALL SoundMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
// 0x0031e710: the panel (Panel_Draw), the help bar text (FEMENU_HELPBAR_BACK on row 0, else FEMENU_OP_HELPBAR) and
// its draw (+0x3c), UIMenu_Draw; unless 0x0095f916 the background movie (vtable +0x08, the subtitles, its frame
// texture into the quad 0x0095f458 +0x60, nglListAddQuad).
void XB_THISCALL SoundMenu_Draw(uint8_t* menu);
// 0x000129c0 / 0x00012a10 (ret 4, the pad unused): on row 0 the volume -1 / +1, clamped to 0..50 (the move sound
// vtable +0x14 unless clamped), then SoundMenu_UpdateSliders.
void XB_THISCALL SoundMenu_OnLeft(uint8_t* menu, uint32_t pad);
void XB_THISCALL SoundMenu_OnRight(uint8_t* menu, uint32_t pad);
// 0x00338e40: UIMenu_RestoreSelection, the background movie "Propgnda" opened full screen once (0x0095f91a), the
// volume from 0x008dce88, then (tail jump) SoundMenu_UpdateSliders.
uint32_t XB_THISCALL SoundMenu_RestoreSelection(uint8_t* menu);
// 0x0031e3b0 (menu in EAX): v = volume * 0.02; the slider's bar quad (+0x40 of its quads) gets x from its left edge
// to left + (right - left) * width (+0x34) * clamp(v, 0, 1), u to u0 + that fraction (vertex order by whether the
// quad's x edges are 0.001 apart); the crossfade 0x0095f4cc volume v (S_SetHandleVolume), the sound system's
// 0x0095f4c4 +0x6018 = v clamped to 0..1, master volume 1 - (1 - it)^2. Returns S_SetMasterVolume's result.
uint32_t XB_REGS1 XB_CDECL SoundMenu_UpdateSliders(uint8_t* menu);

// 0x000c6fb0: the voice type of a format (AL): 2 for kind (+4) 6; without +5 bit 1: 5 / 6 (bit 0 set / clear);
// 6 channels: 1; 2 channels: 7 above 24000 Hz (+0x20), else 8; otherwise 3 / 4 (bit 0 set / clear).
uint8_t XB_CDECL SndFormat_GetVoiceType(const uint8_t* format);

// 0x002b6d60 (stdcall manager): the vtable 0x008327a8; every loaded wave bank of every bank (key +0x6c, file +0x54,
// six per 0x84-byte bank from +0xc, count +8 re-read) freed (SndBank_Free, File_Close, both set to -1); then
// ResourceManager_Destruct.
void XB_STDCALL SoundBankManager_Dtor(uint8_t* manager);
// 0x0001c320: the destructor, Z_Free with flags bit 0. Returns the manager.
uint8_t* XB_THISCALL SoundBankManager_DeletingDtor(uint8_t* manager, uint32_t flags);

// 0x002c8c20 (stdcall channel table; the per-frame channel update): SndSys_Update, then per channel (0x200 of 0x30
// bytes) with no solo owner (0x008dda84 = -1): a finished voice with +9 set stopped, a playing or paused (+8) one
// re-applied (S_ReapplyChannelParams), a finished one stopped; with a solo owner the channels of that owner (+0x20)
// re-applied while playing or paused, every other channel with a voice stopped.
void XB_STDCALL S_StopAllChannels(uint8_t* channels);
// 0x000c3700: with both guard strings intact (0x0090a89c "NSL_WORK_GUARD0", 0x0090a8a0 "NSL_WORK_GUARD1"; else
// nothing): the frame time 0x0090a8a4 (0 on the first frame), Snd_UpdateTime into 0x0090a8ac, frame count
// 0x0090a8b0; SndBank_UpdateLoads, SndVoice_ApplyParams, SndSource_CommitDirty, Snd_UpdateListenerAxis; every
// voice record whose sound (+0xc8) is gone while +0xc0 is set freed (SndVoice_Free +0xcc, SndHw_ReleaseSlot); then
// (tail jump) SndHw_Update (Xbox DirectSound layer, title code).
uint32_t XB_CDECL SndSys_Update();

}  // namespace game
