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

// Anim managers (Anim_InitManagers): 0x0090982c anim files, 0x00909830 clips, 0x00909838 skeletons,
// 0x00909828; slot 2 find (32-byte HashStr name), 3 add, 8 release (item, 0, 0).
// 0x0008d950: clip manager find. 0x0008d9a0: p ? p[1] : 0.
uint32_t XB_CDECL NAL_FindClip(uint32_t name);
uint32_t XB_CDECL NAL_PtrField4(const uint32_t* p);
// 0x0008d9b0 (cdecl file): the file's skeletons (+0xc names from +0x48) looked up, the clip chain
// (+0x34, relative links) made absolute, each clip's skeleton, class vtable (class map 0x00903fe0
// by the skeleton's type, record + 0x28) and init (slots 3, 1) set, the clip registered; flags
// (+4) |= 8. Returns AL 1. 0x0008dd40: the same for files with groups (names from +0x50, group
// chain +0x34 with clip chains at +8; clips not registered, +0x3c untouched, no flag).
// 0x0008de20: slot 2 of every clip of such a file.
uint32_t XB_CDECL NAL_AnimFile_Fixup(uint8_t* file);
uint32_t XB_CDECL NAL_GroupedAnimFile_Fixup(uint8_t* file);
void XB_CDECL NAL_GroupedAnimFile_ForEachClip(const uint8_t* file);
// 0x0008dbb0 (cdecl name, file; Stream_LoadXbanim): an already loaded file gets a reference
// (+0x44); else the name is copied to +0x10, references 1, flags |= 4, NAL_AnimFile_Fixup and the
// anim file manager add (0 on failure). 0x0008e130 (cdecl skeleton; Stream_LoadXbskel): the
// same for skeletons (name +8, references +0x48; class vtable record + 0x2c, slots 4 and 2, +0x50 = 0).
uint8_t* XB_CDECL NAL_AnimFile_Register(const uint32_t* name, uint8_t* file);
uint8_t* XB_CDECL NAL_Skeleton_Register(uint8_t* skeleton);
// 0x0008dcb0 / 0x0008e060 / 0x0008e2c0: release through the 0x0090982c / 0x00909828 / 0x00909838
// manager. 0x0008ed30: slot 0 of every registered type in the list 0x00909840 (next at +4).
uint32_t XB_CDECL NAL_AnimFile_Release(uint32_t file);
uint32_t XB_CDECL NAL_Manager828_Release(uint32_t item);
uint32_t XB_CDECL NAL_Skeleton_Release(uint32_t skeleton);
void XB_CDECL NAL_InitRegisteredTypes();
// 0x0008f170 (cache in ECX): every block from the front (+8) freed through the allocator (slot 2:
// block, size +0xc).
void XB_THISCALL NAL_BlockCache_FreeAll(uint32_t* cache);

// 0x00087030 (slot 3 of the generic clip vtable 0x007c23ac, clip in ECX): lays out the clip's
// sections behind its header (pointers +0x64..+0xc4 from the counts and alignments stored there;
// +0xc8 = clip, +0xd0 = +0x94), resolves both group tables' classes (map 0x00903ff4) and runs each
// group class's slot 4 (record, &cursor, &cursor) over them. Returns the second table's count.
uint32_t XB_THISCALL NAL_GenericAnim_Init(uint8_t* clip);

// 0x00087cb0 (skeleton in ECX: +0 clip, +8 data; callee pops 8: out, bone index): the bone's
// transform {quaternion, position} from its record (clip +0x74, 0x30 per bone: +0x20 flags, +0x24
// position / +0x28 rotation offsets into the clip's shared data +0xb0 (flag bits 0/1) or the
// skeleton's data, +0x2c parent) concatenated with its parents' (QuatTrans_Concat). The title writes
// y into out +0x18 as well (z is lost). Returns out.
float* XB_THISCALL NAL_Skeleton_BoneTransform(const uint32_t* skeleton, float* out, int32_t index);

// 0x00086cb0 (vtable 0x007c3250 slot 0, callee pops 4): the track base's deleting
// destructor: the clip's track count (+0x3c) down, the track freed when flags bit 0.
uint8_t* XB_THISCALL NAL_AnimTrackBase_DeletingDtor(uint8_t* track, uint32_t flags);
// 0x00087420 (track in ECX): releases the track's channel map (refcount; unlinked from its
// bucket of 0x00906810 and freed at 0) and its state (+0x14), then the base's part.
void XB_THISCALL NAL_AnimTrack_Dtor(uint8_t* track);
// 0x0008b480 (vtable 0x007c3258 slot 0, callee pops 4): NAL_AnimTrack_Dtor, then freed when flags bit 0.
// Not registered: its CALL sits in the first 5 bytes (unhookable).
uint8_t* XB_THISCALL NAL_AnimTrack_DeletingDtor(uint8_t* track, uint32_t flags);
// 0x00087dd0 (track in ECX, callee pops 8): a track of `clip` for `skeleton` (0 = the clip's
// own): duration and rate, state +0x14, time +0x24 = -1e9, and the channel map from the clip
// skeleton's channels to the target's (+0x2c, shared per skeleton pair). Returns the track.
uint8_t* XB_THISCALL NAL_AnimTrack_Construct(uint8_t* track, uint8_t* clip, uint8_t* skeleton);
// 0x000872e0 (clip in ECX; vtable 0x007c23ec slot 2): skips every cached frame of each
// decoded block through the channels (slot 15), frees the blocks, and turns the block
// table (+0x6c) back into offsets.
void XB_THISCALL NAL_GenericAnim_Unload(uint8_t* clip);
// 0x0008b4d0 (registrar vtable 0x00832774 slot 0): adds the channel class {+8 name, +0xc
// prototype} to the class map 0x00903ff4.
uint32_t XB_THISCALL NAL_ChannelClass_Register(const uint8_t* entry);
// 0x0008af60 (clip in ECX, callee pops 0x14; generic clip slot 8 through 0x0007bc80): the
// state that gives `pose` (the bone matrices of `range`): src copied to dst, then the clip's
// byte code (+0x6c, length in the range record +8) writes positions / rotations from `work`
// (a copy of the pose that opcodes 11/13 make bone-relative) and two-bone bend angles.
void XB_THISCALL NAL_GenericAnim_ExtractState(uint8_t* clip, AnimState* dst, const float* pose, float* work,
                                              const AnimState* src, int32_t range);

// Streamed-anim players: a base (vtable 0x007c3c60) holding bindings (+8 head, +0xc tail;
// node {sink, clip, track, next}); the streamed player (0x007c3c70, 0x7c bytes: file
// requests into two buffers) and the clip-set player (0x007c4160, 0x20 bytes).
// 0x0008ed50 (ECX): frees the bindings (sink slot 3, track deleted). EAX = 0.
void XB_THISCALL NAL_AnimPlayer_Dtor(uint8_t* player);
// 0x0008e330 (callee pops 4): NAL_AnimPlayer_Dtor, freed when flags bit 0.
// Not registered (also 0x0008e350): the CALL sits in the first 5 bytes.
uint8_t* XB_THISCALL NAL_AnimPlayer_DeletingDtor(uint8_t* player, uint32_t flags);
// 0x0008e370 (ECX): the bindings (track deleted, then its clip's slot 2 and the sink's
// slot 3), the skeleton table (+0x38), the buffers (+0x48) and the file data (+4), then the base.
void XB_THISCALL NAL_StreamedAnim_Dtor(uint8_t* player);
// 0x0008e350 (callee pops 4): NAL_StreamedAnim_Dtor, freed when flags bit 0.
uint8_t* XB_THISCALL NAL_StreamedAnim_DeletingDtor(uint8_t* player, uint32_t flags);
// 0x0008e3f0 (cdecl): a streamed player for `size` bytes at `offset` of `file`, data up to
// `end`; `bind(file data, clip name, context)` makes each clip's sink.
uint8_t* XB_CDECL NAL_StreamedAnim_Create(uint32_t file, uint32_t offset, uint32_t end, uint32_t size, uint32_t bind,
                                          uint32_t bind_context);
// 0x0008eda0 (ECX, callee pops 8): appends a binding {sink, clip, 0, 0}. Returns it.
uint32_t* XB_THISCALL NAL_AnimPlayer_AddBinding(uint8_t* player, uint32_t sink, uint32_t clip);
// 0x0008f0d0 (ECX, callee pops 0xc): a clip-set player of `group` (its sets at +0x34):
// one binding per clip of the first set that `bind(group, clip name, context)` gives a sink.
uint8_t* XB_THISCALL NAL_ClipSetPlayer_Init(uint8_t* player, uint8_t* group, uint32_t bind, uint32_t bind_context);
// 0x0008ee40 (group in ECX, callee pops 8): allocates and initializes a clip-set player (0 on failure).
uint8_t* XB_THISCALL NAL_ClipSetPlayer_Create(uint8_t* group, uint32_t bind, uint32_t bind_context);
// 0x0008e480 (ECX; slot 2 of 0x007c3c70): the file's skeletons looked up, its clips fixed up
// (links, skeleton, class vtable, slots 3 and 1) and bound; state +0x1c = 6.
void XB_THISCALL NAL_StreamedAnim_Setup(uint8_t* player);
// 0x0008e560 (ECX): when the pending request (+0x6c) is done, queues the next block into
// the other buffer (File_QueueRequest), from the start again when the file data loops.
void XB_THISCALL NAL_StreamedAnim_RequestNext(uint8_t* player);
// 0x0008ee80 (ECX, callee pops 4; slot 3 of 0x007c4160): advances the player by dt through
// the clip sets (the next set at the clip's end; the first again when the group loops) and
// hands every binding's sink the clip fractions and times (sink slot 1).
void XB_THISCALL NAL_ClipSetPlayer_Update(uint8_t* player, float dt);

// 0x0008e630 (ECX, callee pops 4; slot 3 of 0x007c3c70): the streamed player's state machine
// (+0x1c): 0-2 read the header (aligned to the file's alignment), 3 sets up two stream
// buffers of whole blocks, 4 waits for the first block, 6 plays: advances by dt through the
// blocks (each block's clips fixed up and bound), keeps the reads going, loops the file.
void XB_THISCALL NAL_StreamedAnim_Update(uint8_t* player, float dt);

// 0x0007bd20: generic anim vtable slot (this unused): destroys and frees a state (null: nothing).
void XB_STDCALL NAL_GenericAnim_DestroyState(AnimState* state);
// 0x0007bd40: deleting destructor: the state at +0xc8 destroyed, the base vtable 0x007c2370
// stored, freed on flags & 1.
uint8_t* XB_THISCALL NAL_GenericAnim_DeletingDtor(uint8_t* self, uint32_t flags);

// 0x0007bac0: the anim system's managers (0x0090982c/30/28/38, skip lists), its 16 KB work
// block (first user), the two shader caches, the NAL types; `config` (null: the default at
// 0x008bb124, 1 MB) stored in 0x00903d78/0x00903f9c. Returns it. CALL in the first 5 bytes:
// not hookable.
uint32_t XB_CDECL Anim_InitManagers(uint32_t config);


struct NglSkipList;
// 0x00025230 / 0x00025750 (thiscall, callee pops 12): the anim managers' pass walk, the
// twin of nglTextureManager_ForEachPass: per pass 1..passes every item the manager's
// iterator gives visited (slot 8: item, pass, context), restarting unless it returns > 0;
// a zero result with `notify` calls the manager's slot 1.
void XB_THISCALL NalManagerKey8_ForEachPass(void** self, uint8_t notify, uint32_t context, int32_t passes);
void XB_THISCALL AnimManager_ForEachPass(void** self, uint8_t notify, uint32_t context, int32_t passes);
// 0x00083db0 (thiscall, callee pops 4): nglSkipList_Insert on the object's name at +0x10;
// returns the listed object of that name or null after inserting.
const uint32_t* XB_THISCALL AnimManager_SkipListInsert(NglSkipList* self, const uint32_t* object);
// 0x00083f70 (thiscall, callee pops 4): the listed object whose name (+0x10) is `key`, or null.
const uint32_t* XB_THISCALL AnimManager_SkipListFind(const NglSkipList* self, const uint32_t* key);
// 0x00378830: the "generic" clip class's registry entry 0x00903fac (vtable 0x007c23ac, name
// hash, class words) pushed on the class list 0x00909840.
void XB_CDECL StaticInit_AnimHashStrings();
// 0x00378ca0: the identity quaternion at 0x00909850 (the default root record).
void XB_CDECL StaticInit_Vec4_909850();

}  // namespace game
