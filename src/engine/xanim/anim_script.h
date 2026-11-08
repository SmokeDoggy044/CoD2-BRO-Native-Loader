#pragma once

// The scripted-animation resource (vtable 0x0083261c: 0 deleting destructor, 2 load with
// its "COD_Note" notetracks, 3 update): plays a NAL clip on an entity's skeleton or, for
// the "camera" kind, into the camera root record 0x00960040.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0001cdc0 (weights in ECX, value in XMM0): sets every channel weight (+8, count at
// (+4)->+0x7c) to value. Returns the count (0 when it is not positive).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL AnimWeights_SetAll(uint8_t* weights, float value);
extern "C" void AnimWeights_SetAll_entry(void);
// 0x000229d0 (clip in EAX): the clip when it is a generic NAL clip (vtable 0x00903fdc), else 0.
uint8_t* XB_REGS1 XB_CDECL NAL_AsGenericClip(uint8_t* clip);
// 0x00023150 / 0x00023180 (reference in EAX, callee pops 4): the weight a channel
// reference {set, channel group (+0x24 first channel), index} names in the weights' float
// array, or a dummy (0x0098ad20 / 0x0098ad1c) when the reference is not set.
float* XB_REGS1 XB_STDCALL AnimWeightRef_GetA(const uint32_t* ref, const uint8_t* weights);
float* XB_REGS1 XB_STDCALL AnimWeightRef_GetB(const uint32_t* ref, const uint8_t* weights);
// 0x0001e150 (this in ECX, callee pops 0x14): samples the script's clip through the
// track (time, previous time) and poses the entity (handle +0x2c) with it - blended in or
// out against the entity's own clip state over +0xcc / +0xd0 seconds of `elapsed` - then
// moves the entity by the root motion and sends the notetracks (+0x30) passed between
// the two times. The "camera" kind (+0x28 bit 0) writes the root record to 0x00960040.
// The asm entry moves the C frame below a gap: every call runs where the original's
// does, at its frame's addresses.
extern "C" void AnimScript_Update_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL AnimScript_Update(uint32_t entry, uint8_t* self, uint8_t* track, uint32_t time,
                                                           uint32_t previous, float elapsed, uint32_t unused,
                                                           uint32_t ebx, uint32_t esi, uint32_t edi);

// 0x00022eb0 / 0x00023000 (generic clip in ESI, callee pops 12): the channel on bone `bone`
// named `name` of the PosOri (0x00909824) / Float3 (0x00909820) class as {clip, group,
// channel in group}; out[0] = 0 when there is none.
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_Clip_FindPosOriChannel(uint8_t* generic, uint32_t* out, const uint32_t* bone,
                                                                   const uint32_t* name, uint32_t stale1, uint32_t stale2,
                                                                   uint32_t stale3);
extern "C" void NAL_Clip_FindPosOriChannel_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_Clip_FindFloat3Channel(uint8_t* generic, uint32_t* out, const uint32_t* bone,
                                                                   const uint32_t* name, uint32_t stale1, uint32_t stale2,
                                                                   uint32_t stale3);
extern "C" void NAL_Clip_FindFloat3Channel_entry(void);
// 0x0001cd80 (weights in ESI, generic clip in EAX): vtable 0x00832758, the clip and a zeroed
// float per channel (+0x7c). Returns the weights.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL AnimWeights_Construct(uint8_t* weights, uint8_t* generic);
extern "C" void AnimWeights_Construct_entry(void);
// 0x0001dc10 (this in EBX, kind in EAX, name in ECX, blend-in time in XMM0; callee pops 4):
// the script for the entity named like `name` (or the camera), with its clip's root and
// pelvis channels and channel weights.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL AnimScript_Construct(uint32_t entry, uint8_t* self, uint32_t kind,
                                                                 const uint32_t* name, uint32_t blend_in,
                                                                 uint32_t blend_out, uint32_t esi, uint32_t edi);
extern "C" void AnimScript_Construct_entry(void);
// 0x0001eb40 (vtable slot 3): weights deleted, the entity's script flags cleared,
// notetracks released, the script freed.
void XB_THISCALL AnimScript_Destroy(uint8_t* self);

// 0x0001de20 (AnimScript vtable slot; thiscall, callee pops 4): the script's notetracks
// (+0x30) rebuilt from the clip's "COD_Note" channel, as XAnim_BuildNotetracks; returns a
// new track over the clip (null without memory).
uint8_t* XB_THISCALL AnimScript_LoadNotetracks(uint8_t* self, uint8_t* clip);

}  // namespace game
