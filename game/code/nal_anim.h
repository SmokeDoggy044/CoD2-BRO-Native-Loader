#pragma once

// NAL animation states (docs/nal_animation.md): a clip's per-channel state data and the
// drivers that walk its channel groups through the channel vtable.

#include <cstdint>

#include "compiler.h"

namespace game {

// A clip state: the clip, its group range (0 = all groups) and the channel data (from the
// scratch arena 0x00904010..0x00906810 or Mem_AlignedAlloc, then `owned`).
struct AnimState {
    uint8_t* clip;
    int32_t range;
    uint8_t* data;
    uint8_t owned;
};

// 0x00086d30 (clip in ECX, callee pops 8): the clip's root transform record (32 bytes)
// from the state data at clip +0xd8, or the default 0x00909850 when that is negative.
// Returns the offset.
int32_t XB_THISCALL NAL_GenericAnim_GetRootTransform(uint8_t* clip, const AnimState* state, uint32_t* out);
// 0x00086da0 (state in ECX): frees every channel's state (slot 13), then the data.
void XB_THISCALL NAL_AnimState_Destroy(AnimState* state);
// 0x00086e50 (dst in ECX, callee pops 4): src's range and channel states (slot 14) into
// dst, over the groups of src's range.
void XB_THISCALL NAL_AnimState_Copy(AnimState* dst, const AnimState* src);
// 0x00086ec0 (dst in ECX, callee pops 8): like NAL_AnimState_Copy for an explicit range.
void XB_THISCALL NAL_AnimState_CopyRange(AnimState* dst, const AnimState* src, int32_t range);
// 0x0008f220 (cache in ECX, callee pops 4): moves a block to the front (+8) of the block
// cache's LRU list (+4 = the back; block +4 prev, +8 next). Returns the block.
uint32_t* XB_THISCALL NAL_BlockCache_Touch(uint32_t* cache, uint32_t* block);
// 0x00087690 (track in ECX, callee pops 0x18): evaluates frame `frame` of a decoded block
// into the pose: each channel's slot 9 per LOD level, coarsest first, down to min_level.
void XB_THISCALL NAL_AnimTrack_EvaluateFrame(const uint8_t* track, uint8_t* pose, uint8_t* block, int32_t frame,
                                             uint32_t out, uint32_t extra, int32_t min_level);
// The NAL block cache (0x00903f9c: +0 allocator, +4 LRU back, +8 front). A block: +0 the
// owner's pointer to it, +4 prev, +8 next, +0xc size, +0x10 finest level decoded (-1),
// +0x14 the chain of finer levels' sub-blocks (+0 next, +4 size, data at +0x10); the
// coarsest level's data at +0x20. `desc` = {level count, frame size per level}.
// 0x0008f1a0 (cache in ECX, callee pops 4): unlinks the block (clearing the owner's
// pointer) and frees it and its sub-blocks.
void XB_THISCALL NAL_BlockCache_Free(uint32_t* cache, uint32_t* block);
// 0x0008f280 (cache in ECX, callee pops 0x10): a new front block for `frames` frames of the
// coarsest level plus sub-blocks down to min_level, evicting from the back until each
// allocation fits; *owner = the block.
uint32_t* XB_THISCALL NAL_BlockCache_Alloc(uint32_t* cache, const uint32_t* desc, int32_t min_level, int32_t frames,
                                           uint32_t* owner);
// 0x0008f380 (cache in ECX, callee pops 0x10): moves the block to the front and appends
// sub-blocks for the levels below the finest decoded one, down to min_level. Returns
// min_level (EAX as the original leaves it).
int32_t XB_THISCALL NAL_BlockCache_Extend(uint32_t* cache, uint32_t* block, const uint32_t* desc, int32_t min_level,
                                          int32_t frames);
// 0x000874b0 (track in ECX, callee pops 8): makes block `index` of the track's clip hold
// levels down to min_level (the clip's block table +0x60 is the owner): channel slot 7
// decodes, slot 8 skips levels already there; the block's finest level becomes
// min_level. Returns min_level.
int32_t XB_THISCALL NAL_AnimTrack_DecodeBlock(const uint8_t* track, int32_t index, int32_t min_level);
// 0x00087810 (track in ECX, callee pops 0x10): the pose at frame `time` into dst (from
// src's data where the track's clip is not the skeleton): its block decoded down to
// min_level (or moved to the cache front), then evaluated; dst's range = min_level.
void XB_THISCALL NAL_AnimTrack_SampleFrame(const uint8_t* track, int32_t time, AnimState* dst, const AnimState* src,
                                           int32_t min_level);
// 0x00087890 (cdecl: dst, weight, a, b): blends a and b (slot 2) into dst over the larger
// range's groups, copies the rest of the smaller range from the state that has it; dst
// gets the smaller range. The entry hands the C version the argument block (the original
// keeps its cursors in the a/b slots).
extern "C" void NAL_AnimState_Blend_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_AnimState_Blend(uint32_t* args);
// 0x000879a0 (weights in ECX, callee pops 0xc): blends a and b into dst with one weight
// per channel (slot 3; the weights object's float array at +8) over the larger range's
// groups, copies the rest of the smaller range from the state that has it; dst gets the
// larger range.
void XB_THISCALL NAL_AnimState_BlendWeighted(const uint8_t* weights, AnimState* dst, const AnimState* a,
                                             const AnimState* b);
// 0x00087ad0 (state in ECX, callee pops 8): sets the state up for clip/range and lets each
// channel initialize its data (slot 12). Returns the state.
AnimState* XB_THISCALL NAL_AnimState_Init(AnimState* state, uint8_t* clip, int32_t range);
// 0x00087bb0 (state in ECX, callee pops 8): NAL_AnimState_Init with src's clip and range,
// then src's channel states copied when the low byte of `copy` is set. Returns the state.
AnimState* XB_THISCALL NAL_AnimState_InitFrom(AnimState* state, const AnimState* src, uint32_t copy);
// 0x000881c0 (track in ECX, callee pops 0x18): the track's pose at `time` (0..1 of the
// clip; wrapped when the clip loops (+0x34 bit 0), else clamped) into dst - the nearest
// frame (flags bit 0) or the two frames around it blended; the track's own state
// (+0x14, time +0x24) follows `previous` first. Then the static channels (slot 11) with
// the wrap count, and the track keeps dst's pose.
void XB_THISCALL NAL_AnimTrack_Sample(uint8_t* track, float time, float previous, AnimState* dst, const AnimState* src,
                                      int32_t min_level, uint32_t flags);
// 0x002beb60: node `index` of the anim tree in 0x00928588 into dst: a leaf samples its
// track (src copied without one); other nodes evaluate their weighted children (normal
// ones blended into dst, then additive ones (node flag 0x10) on top). Frees the track of
// a node that no longer has one. arg4/arg5 are only handed down (0, 1 for blended
// children). Returns the root-motion flag (clip +0x34 bit 1) of the normal children.
uint8_t XB_CDECL AnimTree_CalcPose_r(uint32_t index, AnimState* dst, const AnimState* src, uint32_t arg4, uint32_t arg5,
                                     int32_t min_level);
// 0x0008e0d0: the skeleton named `name` (manager 0x00909838, slot 2), or 0.
uint32_t XB_CDECL NAL_FindSkeleton(uint32_t name);
// 0x0001db80 (memory context in EAX, -1 = none; cdecl clip): a new 16-byte state for the
// clip, allocated inside that memory context.
AnimState* XB_REGS1 XB_CDECL Anim_CreateClipState(int32_t context, uint8_t* clip);

}  // namespace game
