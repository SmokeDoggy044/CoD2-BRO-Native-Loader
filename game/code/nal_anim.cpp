#include "nal_anim.h"

#include <cstring>

#include "nal_math.h"
#include "qcommon.h"
#include "title_data.h"

// Calls a title function that takes EAX, ESI and EDI (tr_main.cpp).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

namespace game {

namespace {

// MemContext_Pop (0x0028c690) takes the memory system in EDX.
extern "C" PLATFORM_HIDDEN void XB_CDECL anim_memcontext_pop(uint32_t system, uint32_t context);
PLATFORM_ASM_ENTRY(anim_memcontext_pop,
                   "pushl %edi\n"
                   "movl 8(%esp), %edx\n"
                   "movl 12(%esp), %edi\n"
                   "movl $0x0028c690, %eax\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "ret")

}  // namespace

namespace {

// A clip's channel group (0x30 bytes): +0x20 the channel object, +0x28 its channel count,
// +0x2c its offset in the state data.
uint8_t* groups(const uint8_t* clip) { return *reinterpret_cast<uint8_t* const*>(clip + 0x88); }
int32_t group_count(const uint8_t* clip) { return *reinterpret_cast<const int32_t*>(clip + 0x84); }

// The number of groups range `range` covers: all of them for range 0, else the range
// record's count (+0xc of the 20-byte records at clip +0x64).
int32_t range_groups(const uint8_t* clip, int32_t range) {
    if (range - 1 < 0) return group_count(clip);
    return *reinterpret_cast<const int32_t*>(*reinterpret_cast<uint8_t* const*>(clip + 0x64) + range * 20 - 8);
}

uint32_t channel(const uint8_t* group) { return *reinterpret_cast<const uint32_t*>(group + 0x20); }
uint32_t channel_slot(const uint8_t* group, int slot) {
    return reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(channel(group)))))[slot];
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// Channel slots 12/13 (init/free state: group, &cursor) and 14 (copy: group, &dst, &src).
void channel_state(const uint8_t* group, int slot, uint8_t** cursor) {
    title_function<void XB_THISCALL(uint32_t, uint32_t, uint8_t**)>(channel_slot(group, slot))(channel(group),
                                                                                             address_of(group), cursor);
}
void channel_copy(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    title_function<void XB_THISCALL(uint32_t, uint32_t, uint8_t**, uint8_t**)>(channel_slot(group, 14))(
        channel(group), address_of(group), dst, src);
}

// The scratch arena: data 0x00904010..0x00906810, top offset 0x00906814; every block
// keeps the previous top in the word after it.
constexpr uint32_t kArena = 0x00904010;
constexpr uint32_t kArenaEnd = 0x00906810;
uint32_t& arena_top() { return title_global<uint32_t>(0x00906814); }

}  // namespace

int32_t XB_THISCALL NAL_GenericAnim_GetRootTransform(uint8_t* clip, const AnimState* state, uint32_t* out) {
    const int32_t offset = *reinterpret_cast<const int32_t*>(clip + 0xd8);
    const void* source = offset >= 0 ? static_cast<const void*>(state->data + offset) : &title_global<uint32_t>(0x00909850);
    std::memcpy(out, source, 32);
    return offset;
}

void XB_THISCALL NAL_AnimState_Destroy(AnimState* state) {
    const uint8_t* clip = state->clip;
    if (!clip) return;
    uint8_t* cursor = state->data;
    uint8_t* group = groups(clip);
    for (int32_t i = 0; i < group_count(clip); ++i, group += 0x30) channel_state(group, 13, &cursor);
    if (state->owned) {
        title_function<void XB_CDECL(uint8_t*)>(0x000939b0)(state->data);  // Mem_AlignedFree
        return;
    }
    const uint32_t data = address_of(state->data);
    if (data >= kArena && data < kArenaEnd) arena_top() = title_global<uint32_t>(arena_top() + 0x0090400c);
}

void XB_THISCALL NAL_AnimState_Copy(AnimState* dst, const AnimState* src) {
    const uint8_t* clip = dst->clip;
    int32_t count = range_groups(clip, src->range);
    dst->range = src->range;
    uint8_t* src_cursor = src->data;
    uint8_t* dst_cursor = dst->data;
    uint8_t* group = groups(clip);
    for (; count > 0; --count, group += 0x30) channel_copy(group, &dst_cursor, &src_cursor);
}

void XB_THISCALL NAL_AnimState_CopyRange(AnimState* dst, const AnimState* src, int32_t range) {
    const uint8_t* clip = dst->clip;
    int32_t count = range_groups(clip, range);
    dst->range = range;
    uint8_t* src_cursor = src->data;
    uint8_t* dst_cursor = dst->data;
    uint8_t* group = groups(clip);
    for (; count > 0; --count, group += 0x30) channel_copy(group, &dst_cursor, &src_cursor);
}

uint32_t* XB_THISCALL NAL_BlockCache_Touch(uint32_t* cache, uint32_t* block) {
    if (!block || cache[2] == address_of(block)) return block;
    uint32_t* prev = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(block[1]));
    uint32_t* next = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(block[2]));
    if (prev)
        prev[2] = block[2];
    else
        cache[2] = block[2];
    if (next)
        next[1] = block[1];
    else
        cache[1] = block[1];
    block[1] = 0;
    block[2] = cache[2];
    uint32_t* head = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(cache[2]));
    if (head)
        head[1] = address_of(block);
    else
        cache[1] = address_of(block);
    cache[2] = address_of(block);
    return block;
}

void XB_THISCALL NAL_AnimTrack_EvaluateFrame(const uint8_t* track, uint8_t* pose, uint8_t* block, int32_t frame,
                                             uint32_t out, uint32_t extra, int32_t min_level) {
    const uint8_t* clip = *reinterpret_cast<uint8_t* const*>(track + 0x10);
    const uint8_t* skeleton = *reinterpret_cast<uint8_t* const*>(clip + 0x30);
    uint32_t stream_a = *reinterpret_cast<const uint32_t*>(skeleton + 0xc4);
    uint32_t stream_b = *reinterpret_cast<const uint32_t*>(clip + 0x50);
    // A track whose own clip (+0xc) is not the skeleton copies that clip's channel states.
    const uint8_t* own = *reinterpret_cast<uint8_t* const*>(track + 0xc);
    if (own != skeleton) {
        uint8_t* dst = pose;
        uint8_t* src = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(out));
        uint8_t* group = groups(own);
        for (int32_t i = 0; i < group_count(own); ++i, group += 0x30) channel_copy(group, &dst, &src);
    }
    // Levels from the coarsest down to min_level: level i's groups run from its range
    // record's +0xc to the finer level's (all groups for level 0). Level data: the block's
    // +0x20, then the chain of sub-blocks from +0x14 (next at +0, data at +0x10), each
    // frame_size[i] (clip +0x5c) * frame further.
    const int32_t* frame_sizes = *reinterpret_cast<int32_t* const*>(clip + 0x5c);
    int32_t level = *reinterpret_cast<const int32_t*>(skeleton + 0x60) - 1;
    uint32_t data = static_cast<uint32_t>(frame_sizes[level] * frame) + address_of(block) + 0x20;
    uint32_t chain = 0;
    while (level >= min_level) {
        const uint8_t* ranges = *reinterpret_cast<uint8_t* const*>(skeleton + 0x64);
        const int32_t first = *reinterpret_cast<const int32_t*>(ranges + level * 20 + 0xc);
        const int32_t last = level >= 1 ? *reinterpret_cast<const int32_t*>(ranges + level * 20 - 8) : group_count(skeleton);
        uint8_t* group = groups(skeleton) + first * 0x30;
        for (int32_t i = first; i < last; ++i, group += 0x30) {
            const uint32_t context[4] = {address_of(clip), address_of(group), address_of(&stream_a), address_of(&stream_b)};
            title_function<void XB_THISCALL(uint32_t, const uint32_t*, uint8_t*, uint32_t*, uint32_t, uint32_t)>(
                channel_slot(group, 9))(channel(group), context, pose, &data, out, extra);
        }
        --level;
        chain = chain ? *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(chain))
                      : *reinterpret_cast<const uint32_t*>(block + 0x14);
        data = static_cast<uint32_t>(frame_sizes[level] * frame) + chain + 0x10;
    }
}

namespace {

// The block cache's allocator (cache +0): slot 1 allocates (size), slot 2 frees (block, size).
uint32_t* cache_alloc(uint32_t* cache, uint32_t size) {
    const uint32_t allocator = cache[0];
    const uint32_t function = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(allocator))))[1];
    return reinterpret_cast<uint32_t*>(
        static_cast<uintptr_t>(title_function<uint32_t XB_THISCALL(uint32_t, uint32_t)>(function)(allocator, size)));
}
void cache_free(uint32_t* cache, uint32_t* block, uint32_t size) {
    const uint32_t allocator = cache[0];
    const uint32_t function = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(allocator))))[2];
    title_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t)>(function)(allocator, address_of(block), size);
}
// Allocates, evicting the least recently used block (+4) until it fits.
uint32_t* cache_alloc_evicting(uint32_t* cache, uint32_t size) {
    for (;;) {
        if (uint32_t* block = cache_alloc(cache, size)) return block;
        NAL_BlockCache_Free(cache, reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(cache[1])));
    }
}
uint32_t* as_pointer(uint32_t value) { return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(value)); }

// Appends sub-blocks (next, size; data at +0x10) for levels `level` down to min_level.
void append_levels(uint32_t* cache, uint32_t* block, const uint32_t* desc, int32_t level, int32_t min_level,
                   int32_t frames, uint32_t* tail) {
    const int32_t* sizes = reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(desc[1]));
    for (; level >= min_level; --level) {
        uint32_t* sub = cache_alloc_evicting(cache, static_cast<uint32_t>(sizes[level] * frames + 0x10));
        sub[1] = static_cast<uint32_t>(sizes[level] * frames + 0x10);
        if (!block[5])
            block[5] = address_of(sub);
        else
            tail[0] = address_of(sub);
        sub[0] = 0;
        tail = sub;
    }
}

}  // namespace

void XB_THISCALL NAL_BlockCache_Free(uint32_t* cache, uint32_t* block) {
    if (!block) return;
    if (block[0]) *as_pointer(block[0]) = 0;
    if (block[1])
        as_pointer(block[1])[2] = block[2];
    else
        cache[2] = block[2];
    if (block[2])
        as_pointer(block[2])[1] = block[1];
    else
        cache[1] = block[1];
    uint32_t* sub = as_pointer(block[5]);
    cache_free(cache, block, block[3]);
    while (sub) {
        uint32_t* next = as_pointer(sub[0]);
        cache_free(cache, sub, sub[1]);
        sub = next;
    }
}

uint32_t* XB_THISCALL NAL_BlockCache_Alloc(uint32_t* cache, const uint32_t* desc, int32_t min_level, int32_t frames,
                                           uint32_t* owner) {
    const int32_t* sizes = reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(desc[1]));
    const int32_t level = static_cast<int32_t>(desc[0]) - 1;
    uint32_t* block = cache_alloc_evicting(cache, static_cast<uint32_t>(sizes[level] * frames + 0x20));
    block[3] = static_cast<uint32_t>(sizes[level] * frames + 0x20);
    block[0] = address_of(owner);
    if (owner) *owner = address_of(block);
    block[1] = 0;
    block[2] = cache[2];
    block[4] = 0xffffffff;
    block[5] = 0;
    if (cache[2])
        as_pointer(cache[2])[1] = address_of(block);
    else
        cache[1] = address_of(block);
    cache[2] = address_of(block);
    append_levels(cache, block, desc, level - 1, min_level, frames, nullptr);
    return block;
}

int32_t XB_THISCALL NAL_BlockCache_Extend(uint32_t* cache, uint32_t* block, const uint32_t* desc, int32_t min_level,
                                          int32_t frames) {
    NAL_BlockCache_Touch(cache, block);
    int32_t level = static_cast<int32_t>(desc[0]) - 1;
    const int32_t decoded = static_cast<int32_t>(block[4]);
    uint32_t* tail = nullptr;
    if (level > decoded) {
        // Walk to the sub-block of the finest level decoded so far.
        for (int32_t steps = level - decoded; steps > 0; --steps) tail = tail ? as_pointer(tail[0]) : as_pointer(block[5]);
        level = decoded;
    }
    append_levels(cache, block, desc, level - 1, min_level, frames, tail);
    return min_level;
}

int32_t XB_THISCALL NAL_AnimTrack_DecodeBlock(const uint8_t* track, int32_t index, int32_t min_level) {
    uint8_t* clip = *reinterpret_cast<uint8_t* const*>(track + 0x10);
    const uint8_t* skeleton = *reinterpret_cast<uint8_t* const*>(clip + 0x30);
    const int32_t per_block = *reinterpret_cast<const int32_t*>(clip + 0x68);
    const int32_t total = *reinterpret_cast<const int32_t*>(clip + 0x44);
    int32_t frames = per_block;
    if ((index + 1) * per_block > total) frames = total - per_block * index;
    uint32_t* cache = &title_global<uint32_t>(0x00903f9c);
    const uint32_t* desc = reinterpret_cast<const uint32_t*>(clip + 0x58);
    uint32_t* slot = *reinterpret_cast<uint32_t**>(clip + 0x60) + index;
    uint32_t* block = as_pointer(*slot);
    if (block && min_level < static_cast<int32_t>(block[4]))
        NAL_BlockCache_Extend(cache, block, desc, min_level, frames);
    else
        block = NAL_BlockCache_Alloc(cache, desc, min_level, frames, slot);
    // Channel slot 8 skips levels the block already has, slot 7 decodes the others.
    uint32_t stream = (*reinterpret_cast<uint32_t* const*>(clip + 0x6c))[index];
    uint32_t data = address_of(block) + 0x20;
    uint32_t stream_a = *reinterpret_cast<const uint32_t*>(skeleton + 0xc4);
    uint32_t stream_b = *reinterpret_cast<const uint32_t*>(clip + 0x50);
    const int32_t* frame_sizes = *reinterpret_cast<int32_t* const*>(clip + 0x5c);
    int32_t level = *reinterpret_cast<const int32_t*>(skeleton + 0x60) - 1;
    uint32_t chain = 0;
    do {
        const uint8_t* ranges = *reinterpret_cast<uint8_t* const*>(skeleton + 0x64);
        const int32_t first = *reinterpret_cast<const int32_t*>(ranges + level * 20 + 0xc);
        const int32_t last = level >= 1 ? *reinterpret_cast<const int32_t*>(ranges + level * 20 - 8) : group_count(skeleton);
        uint8_t* group = groups(skeleton) + first * 0x30;
        const int32_t decoded = static_cast<int32_t>(block[4]);
        const bool cached = decoded >= 0 && level >= decoded;
        for (int32_t i = first; i < last; ++i, group += 0x30) {
            const uint32_t context[4] = {address_of(clip), address_of(group), address_of(&stream_a), address_of(&stream_b)};
            if (cached)
                title_function<void XB_THISCALL(uint32_t, const uint32_t*, uint32_t*, uint32_t*, int32_t)>(
                    channel_slot(group, 8))(channel(group), context, &data, &stream, frames);
            else
                title_function<void XB_THISCALL(uint32_t, const uint32_t*, uint32_t*, uint32_t*, int32_t, int32_t)>(
                    channel_slot(group, 7))(channel(group), context, &data, &stream, frames, frame_sizes[level]);
        }
        --level;
        chain = chain ? *as_pointer(chain) : block[5];
        data = chain + 0x10;
    } while (chain);
    block[4] = static_cast<uint32_t>(min_level);
    return min_level;
}

void XB_THISCALL NAL_AnimTrack_SampleFrame(const uint8_t* track, int32_t time, AnimState* dst, const AnimState* src,
                                           int32_t min_level) {
    const uint8_t* clip = *reinterpret_cast<uint8_t* const*>(track + 0x10);
    const int32_t per_block = *reinterpret_cast<const int32_t*>(clip + 0x68);
    const int32_t index = time / per_block;
    const int32_t frame = time % per_block;
    uint32_t* block = as_pointer((*reinterpret_cast<uint32_t* const*>(clip + 0x60))[index]);
    if (block && static_cast<int32_t>(block[4]) <= min_level)
        NAL_BlockCache_Touch(&title_global<uint32_t>(0x00903f9c), block);
    else
        NAL_AnimTrack_DecodeBlock(track, index, min_level);
    const uint8_t* sampler = *reinterpret_cast<uint8_t* const*>(track + 0x2c);
    NAL_AnimTrack_EvaluateFrame(track, dst->data, reinterpret_cast<uint8_t*>(as_pointer((*reinterpret_cast<uint32_t* const*>(clip + 0x60))[index])),
                                frame, address_of(src->data), *reinterpret_cast<const uint32_t*>(sampler + 0x10), min_level);
    dst->range = min_level;
}

PLATFORM_ASM_ENTRY(NAL_AnimState_Blend_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_AnimState_Blend) "addl $4, %esp\n"
                   "ret")

void XB_CDECL NAL_AnimState_Blend(uint32_t* args) {
    AnimState* dst = reinterpret_cast<AnimState*>(static_cast<uintptr_t>(args[0]));
    const uint32_t weight = args[1];
    const AnimState* a = reinterpret_cast<const AnimState*>(static_cast<uintptr_t>(args[2]));
    const AnimState* b = reinterpret_cast<const AnimState*>(static_cast<uintptr_t>(args[3]));
    const int32_t high = a->range <= b->range ? b->range : a->range;
    dst->range = a->range < b->range ? a->range : b->range;
    const uint8_t* clip = a->clip;
    const int32_t blended = range_groups(clip, high);
    uint8_t* group = groups(clip);
    int32_t done = 0;
    if (blended > 0) {
        for (int32_t i = 0; i < blended; ++i, group += 0x30) {
            const int32_t offset = *reinterpret_cast<const int32_t*>(group + 0x2c);
            title_function<void XB_THISCALL(uint32_t, uint32_t, uint8_t*, uint8_t*, uint8_t*, uint32_t)>(
                channel_slot(group, 2))(channel(group), *reinterpret_cast<const uint32_t*>(group + 0x28),
                                        dst->data + offset, a->data + offset, b->data + offset, weight);
        }
        done = blended;
    }
    // The groups only the smaller range has come from the state with that range.
    const AnimState* source = a;
    int32_t low = a->range;
    if (a->range > b->range) {
        low = b->range;
        source = b;
    }
    const int32_t total = range_groups(clip, low);
    for (int32_t i = done; i < total; ++i, group += 0x30) {
        const int32_t offset = *reinterpret_cast<const int32_t*>(group + 0x2c);
        uint8_t* src_cursor = source->data + offset;
        uint8_t* dst_cursor = dst->data + offset;
        args[3] = address_of(src_cursor);
        args[2] = address_of(dst_cursor);
        channel_copy(group, reinterpret_cast<uint8_t**>(&args[2]), reinterpret_cast<uint8_t**>(&args[3]));
    }
}

void XB_THISCALL NAL_AnimState_BlendWeighted(const uint8_t* weights, AnimState* dst, const AnimState* a,
                                             const AnimState* b) {
    const int32_t high = a->range <= b->range ? b->range : a->range;
    dst->range = high;
    const uint8_t* clip = a->clip;
    // One weight per channel, read in order by the channels (slot 3 advances the cursor).
    uint8_t* cursor = *reinterpret_cast<uint8_t* const*>(weights + 8);
    const int32_t blended = range_groups(clip, high);
    uint8_t* group = groups(clip);
    int32_t done = 0;
    if (blended > 0) {
        done = blended;
        for (int32_t i = 0; i < blended; ++i, group += 0x30) {
            const int32_t offset = *reinterpret_cast<const int32_t*>(group + 0x2c);
            title_function<void XB_THISCALL(uint32_t, uint32_t, uint8_t*, uint8_t*, uint8_t*, uint8_t**)>(
                channel_slot(group, 3))(channel(group), *reinterpret_cast<const uint32_t*>(group + 0x28),
                                        dst->data + offset, a->data + offset, b->data + offset, &cursor);
        }
    }
    // The groups only the smaller range has come from the state with that range.
    const AnimState* source = a;
    int32_t low = a->range;
    if (a->range > b->range) {
        low = b->range;
        source = b;
    }
    const int32_t total = range_groups(clip, low);
    for (int32_t i = done; i < total; ++i, group += 0x30) {
        const int32_t offset = *reinterpret_cast<const int32_t*>(group + 0x2c);
        uint8_t* dst_cursor = dst->data + offset;
        uint8_t* src_cursor = source->data + offset;
        channel_copy(group, &dst_cursor, &src_cursor);
    }
}

namespace {

// NAL_AnimState_Init's body (also NAL_AnimState_InitFrom's).
void setup_state(AnimState* state, uint8_t* clip, int32_t range) {
    state->clip = clip;
    state->range = range;
    state->data = nullptr;
    state->owned = 0;
    const uint32_t size = *reinterpret_cast<const uint32_t*>(clip + 0x8c);
    const uint32_t alignment = *reinterpret_cast<const uint32_t*>(clip + 0x90);
    // States inside 0x009098f0..0x009098f4 take their data from the scratch arena.
    const uint32_t self = address_of(state);
    if (self >= title_global<uint32_t>(0x009098f0) && self < title_global<uint32_t>(0x009098f4)) {
        const uint32_t start = (arena_top() + alignment - 1) & ~(alignment - 1);
        state->data = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(start + kArena));
        const uint32_t end = (size + start + 3) & ~3u;
        if (end + 4 > 0x2800) {
            state->data = nullptr;
        } else {
            title_global<uint32_t>(end + kArena) = arena_top();
            arena_top() = end + 4;
        }
    }
    if (!state->data) {
        state->data = static_cast<uint8_t*>(Mem_AlignedAlloc(size, alignment, 0));
        state->owned = 1;
    }
    uint8_t* cursor = state->data;
    uint8_t* group = groups(clip);
    for (int32_t i = 0; i < group_count(clip); ++i, group += 0x30) channel_state(group, 12, &cursor);
}

}  // namespace

AnimState* XB_THISCALL NAL_AnimState_Init(AnimState* state, uint8_t* clip, int32_t range) {
    setup_state(state, clip, range);
    return state;
}

AnimState* XB_THISCALL NAL_AnimState_InitFrom(AnimState* state, const AnimState* src, uint32_t copy) {
    setup_state(state, src->clip, src->range);
    if (copy & 0xff) NAL_AnimState_CopyRange(state, src, src->range);
    return state;
}

namespace {

// The frame pair around time t of a clip with `count` frames (looping: the last frame
// blends into the first) and the fraction between them. `wrap_first`: a looping first
// frame past the end wraps to 0 (only the current time's pair does that).
struct FramePair {
    int32_t first, second;
    float fraction;
};
FramePair frame_pair(const uint8_t* clip, float t, bool looping, bool wrap_first) {
    const int32_t count = *reinterpret_cast<const int32_t*>(clip + 0x44);
    FramePair p;
    if (looping) {
        const float f = static_cast<float>(count) * t;
        p.first = static_cast<int32_t>(f);
        if (wrap_first && p.first >= count) p.first = 0;
        p.second = p.first + 1;
        if (p.second >= count) p.second = 0;
        p.fraction = f - static_cast<float>(p.first);
    } else {
        const float f = static_cast<float>(count - 1) * t;
        p.first = static_cast<int32_t>(f);
        if (p.first >= count) p.first = count - 1;
        p.second = p.first + 1;
        if (p.second >= count) p.second = count - 1;
        p.fraction = f - static_cast<float>(p.first);
    }
    return p;
}
int32_t nearest_frame(const uint8_t* clip, float t, bool looping) {
    const int32_t count = *reinterpret_cast<const int32_t*>(clip + 0x44);
    if (looping) {
        const int32_t frame = static_cast<int32_t>(static_cast<float>(count) * t);
        return frame < count ? frame : 0;
    }
    const int32_t frame = static_cast<int32_t>(static_cast<float>(count - 1) * t);
    return frame < count ? frame : count - 1;
}

// Channel slot 10 over the skeleton's first `count` groups: a looping clip's static data
// applied to the state of the frame after the wrap.
void apply_static(const uint8_t* clip, const uint8_t* skeleton, int32_t count, uint8_t* data, uint32_t flag,
                  uint32_t extra) {
    uint32_t stream_a = *reinterpret_cast<const uint32_t*>(skeleton + 0xc4);
    uint32_t stream_b = *reinterpret_cast<const uint32_t*>(clip + 0x50);
    uint8_t* group = groups(skeleton);
    for (; count > 0; --count, group += 0x30) {
        const uint32_t context[4] = {address_of(clip), address_of(group), address_of(&stream_a), address_of(&stream_b)};
        title_function<void XB_THISCALL(uint32_t, const uint32_t*, uint8_t*, uint32_t, uint32_t)>(channel_slot(group, 10))(
            channel(group), context, data, flag, extra);
    }
}

// The pose between two frames into dst: both sampled into temporary states and blended.
void sample_between(const uint8_t* track, const uint8_t* own, const FramePair& p, AnimState* dst, const AnimState* src,
                    int32_t min_level, int32_t static_count, uint32_t flag) {
    const uint8_t* clip = *reinterpret_cast<uint8_t* const*>(track + 0x10);
    const uint8_t* skeleton = *reinterpret_cast<uint8_t* const*>(clip + 0x30);
    AnimState a, b;
    NAL_AnimState_Init(&a, const_cast<uint8_t*>(own), 0);
    NAL_AnimState_Init(&b, const_cast<uint8_t*>(own), 0);
    NAL_AnimTrack_SampleFrame(track, p.first, &a, src, min_level);
    NAL_AnimTrack_SampleFrame(track, p.second, &b, src, min_level);
    if (p.first > p.second) {
        const uint32_t extra = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(track + 0x2c) + 0x10);
        apply_static(clip, skeleton, static_count, b.data, flag, extra);
    }
    uint32_t args[4] = {address_of(dst), 0, address_of(&a), address_of(&b)};
    std::memcpy(&args[1], &p.fraction, 4);
    NAL_AnimState_Blend(args);
    NAL_AnimState_Destroy(&b);
    NAL_AnimState_Destroy(&a);
}

}  // namespace

void XB_THISCALL NAL_AnimTrack_Sample(uint8_t* track, float time, float previous, AnimState* dst, const AnimState* src,
                                      int32_t min_level, uint32_t flags) {
    const uint8_t* clip = *reinterpret_cast<uint8_t* const*>(track + 0x10);
    const uint8_t* skeleton = *reinterpret_cast<uint8_t* const*>(clip + 0x30);
    const uint8_t* own = *reinterpret_cast<uint8_t* const*>(track + 0xc);
    const bool looping = (clip[0x34] & 1) != 0;
    int32_t wraps = 0;
    if (looping) {
        // Both times wrapped into [0, 1); wraps = how many more times `time` went round.
        int32_t whole;
        if (time >= 0.0f) {
            whole = static_cast<int32_t>(time);
            time = time - static_cast<float>(whole);
        } else {
            whole = 1 - static_cast<int32_t>(time);
            time = static_cast<float>(whole) + time;
        }
        if (previous >= 0.0f) {
            const int32_t p = static_cast<int32_t>(previous);
            previous = previous - static_cast<float>(p);
            wraps = whole - p;
        } else {
            const int32_t p = 1 - static_cast<int32_t>(previous);
            wraps = whole - p;
            previous = static_cast<float>(p) + previous;
        }
    } else {
        const float one = title_global<float>(0x0083eb20);
        if (!(one > time))
            time = one;
        else if (0.0f > time)
            time = 0.0f;
        if (!(one > previous))
            previous = one;
        else if (0.0f > previous)
            previous = 0.0f;
    }
    const uint32_t flag = (*reinterpret_cast<const uint32_t*>(clip + 0x34) >> 1) & 1;
    const int32_t levels = *reinterpret_cast<const int32_t*>(own + 0x60);
    const int32_t static_count =
        levels - 2 >= 0 ? *reinterpret_cast<const int32_t*>(*reinterpret_cast<uint8_t* const*>(own + 0x64) + levels * 20 - 0x1c)
                        : group_count(own);
    AnimState* track_state = reinterpret_cast<AnimState*>(track + 0x14);
    float& last_time = *reinterpret_cast<float*>(track + 0x24);
    // The track's own state follows `previous` (only when that changed).
    if (!(previous == last_time)) {
        if (flags & 1)
            NAL_AnimTrack_SampleFrame(track, nearest_frame(clip, previous, looping), track_state, src, min_level);
        else
            sample_between(track, own, frame_pair(clip, previous, looping, false), track_state, src, min_level,
                           static_count, flag);
        last_time = previous;
    }
    if (flags & 1)
        NAL_AnimTrack_SampleFrame(track, nearest_frame(clip, time, looping), dst, src, min_level);
    else
        sample_between(track, own, frame_pair(clip, time, looping, true), dst, src, min_level, static_count, flag);
    // The static channels (slot 11) step from the track's state to dst; the track keeps a
    // copy of dst for the next call.
    AnimState copy;
    NAL_AnimState_InitFrom(&copy, dst, 1);
    copy.range = levels - 1;
    uint32_t stream_a = *reinterpret_cast<const uint32_t*>(skeleton + 0xc4);
    uint32_t stream_b = *reinterpret_cast<const uint32_t*>(clip + 0x50);
    const uint32_t extra = *reinterpret_cast<const uint32_t*>(*reinterpret_cast<uint8_t* const*>(track + 0x2c) + 0x10);
    uint8_t* group = groups(skeleton);
    for (int32_t i = static_count; i > 0; --i, group += 0x30) {
        const uint32_t context[4] = {address_of(clip), address_of(group), address_of(&stream_a), address_of(&stream_b)};
        title_function<void XB_THISCALL(uint32_t, const uint32_t*, uint8_t*, uint32_t, int32_t, uint32_t, uint32_t)>(
            channel_slot(group, 11))(channel(group), context, dst->data, *reinterpret_cast<const uint32_t*>(track + 0x1c),
                                     wraps, flag, extra);
    }
    NAL_AnimState_Copy(track_state, &copy);
    last_time = time;
    NAL_AnimState_Destroy(&copy);
}

namespace {

// The anim tree being evaluated (0x00928588): +8 its nodes {+4 count, +8 0x38-byte nodes},
// +0x10 its memory context, +0x18 a uint16 anim-info index per node. Node: +0x20 child
// count (uint16), +0x24 set when it has no track, +0x34 flags (0x10: an additive child),
// +0x36 first child (uint16). Anim info (0x00965d00, 0x30 bytes): +0/+8 the time (float
// + int16), +4/+0xa the previous time, +0x14 weight, +0x1c the track.
uint8_t* tree_node(const uint8_t* tree, uint32_t index) {
    const uint8_t* nodes = *reinterpret_cast<uint8_t* const*>(tree + 8);
    if (index >= *reinterpret_cast<const uint32_t*>(nodes + 4)) index = 0;
    return *reinterpret_cast<uint8_t* const*>(nodes + 8) + index * 0x38;
}
uint8_t* anim_info(const uint8_t* tree, uint32_t index) {
    return &title_global<uint8_t>(0x00965d00 + *reinterpret_cast<const uint16_t*>(tree + 0x18 + index * 2) * 0x30);
}
uint8_t* current_tree() { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00928588))); }

// The children of `node` that take part: an anim info, a non-zero weight (NaN counts) and
// the additive flag as asked. Fills indices (relative to the first child) and weights.
int32_t active_children(const uint8_t* node, bool additive, int32_t* indices, float* weights) {
    const int32_t count = *reinterpret_cast<const uint16_t*>(node + 0x20);
    const uint32_t first = *reinterpret_cast<const uint16_t*>(node + 0x36);
    int32_t n = 0;
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* tree = current_tree();
        if (!*reinterpret_cast<const uint16_t*>(tree + 0x18 + (first + i) * 2)) continue;
        const float weight = *reinterpret_cast<const float*>(anim_info(tree, first + i) + 0x14);
        if (weight == 0.0f) continue;
        if (((tree_node(tree, first + i)[0x34] & 0x10) != 0) != additive) continue;
        indices[n] = i;
        weights[n] = weight;
        ++n;
    }
    float total = 0.0f;
    for (int32_t i = 0; i < n; ++i) total = weights[i] + total;
    if (total > 0.0f)
        for (int32_t i = 0; i < n; ++i) weights[i] = weights[i] / total;
    return n;
}

// Children n-1..0 blended into `into` (from the last one back, each weighted against what
// is left); the first evaluation reads `src`, the others the blend so far.
uint8_t blend_children(const uint8_t* node, int32_t n, const int32_t* indices, const float* weights, AnimState* into,
                       AnimState* other, const AnimState* src, int32_t min_level) {
    const uint32_t first = *reinterpret_cast<const uint16_t*>(node + 0x36);
    const uint8_t result = AnimTree_CalcPose_r(first + indices[n - 1], into, src, 0, 1, min_level);
    float left = title_global<float>(0x0083eb20);
    for (int32_t i = n - 1; i >= 1; --i) {
        const float factor = weights[i] / left;
        AnimTree_CalcPose_r(first + indices[i - 1], other, into, 0, 1, min_level);
        uint32_t args[4] = {address_of(into), 0, address_of(other), address_of(into)};
        std::memcpy(&args[1], &factor, 4);
        NAL_AnimState_Blend(args);
        left = (title_global<float>(0x0083eb20) - factor) * left;
    }
    return result;
}

}  // namespace

uint8_t XB_CDECL AnimTree_CalcPose_r(uint32_t index, AnimState* dst, const AnimState* src, uint32_t arg4, uint32_t arg5,
                                     int32_t min_level) {
    uint8_t* tree = current_tree();
    const uint8_t* node = tree_node(tree, index);
    uint8_t root_motion = 0;
    // A node without a track frees the one its anim info still has.
    if (!*reinterpret_cast<const uint32_t*>(node + 0x24) && *reinterpret_cast<const uint32_t*>(anim_info(tree, index) + 0x1c)) {
        const int32_t context = *reinterpret_cast<const int32_t*>(tree + 0x10);
        uint8_t saved = 0;
        if (context != -1) {
            saved = title_global<uint8_t>(0x0095f5d0);
            const uint32_t system = title_global<uint32_t>(0x0095f638);
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = 1;
            title_call_regs(0x0028f6c0, (static_cast<uint32_t>(context) & ~0xffu) | saved, system,
                            static_cast<uint32_t>(context));  // MemContext_Push
            tree = current_tree();
        }
        const uint32_t track = *reinterpret_cast<const uint32_t*>(anim_info(tree, index) + 0x1c);
        if (track) {
            const uint32_t destroy = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(
                static_cast<uintptr_t>(track))));
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(destroy)(track, 1);  // deleting destructor
            tree = current_tree();
        }
        *reinterpret_cast<uint32_t*>(anim_info(tree, index) + 0x1c) = 0;
        if (context != -1) {
            const uint32_t system = title_global<uint32_t>(0x0095f638);
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = saved;
            anim_memcontext_pop(system, static_cast<uint32_t>(context));
        }
    }
    const int32_t count = *reinterpret_cast<const uint16_t*>(node + 0x20);
    if (count == 0) {
        // A leaf: its track sampled at the info's times (or src copied).
        const uint8_t* info = anim_info(current_tree(), index);
        const float time = static_cast<float>(*reinterpret_cast<const int16_t*>(info + 8)) + *reinterpret_cast<const float*>(info);
        const float previous =
            static_cast<float>(*reinterpret_cast<const int16_t*>(info + 0xa)) + *reinterpret_cast<const float*>(info + 4);
        uint8_t* track = *reinterpret_cast<uint8_t* const*>(info + 0x1c);
        if (track) {
            NAL_AnimTrack_Sample(track, time, previous, dst, src, min_level, 0);
            const uint8_t* clip = *reinterpret_cast<uint8_t* const*>(track + 0x10);
            return static_cast<uint8_t>((*reinterpret_cast<const uint32_t*>(clip + 0x34) >> 1) & 1);
        }
        NAL_AnimState_Copy(dst, src);
        return 0;
    }
    int32_t indices[64];
    float weights[64];
    const uint32_t first = *reinterpret_cast<const uint16_t*>(node + 0x36);
    // The normal children: one evaluated into dst, several blended by weight.
    int32_t n = active_children(node, false, indices, weights);
    if (n == 1) {
        root_motion = AnimTree_CalcPose_r(first + indices[0], dst, src, arg4, arg5, min_level);
    } else if (n > 1) {
        AnimState a, b;
        NAL_AnimState_Init(&a, dst->clip, 0);
        NAL_AnimState_Init(&b, dst->clip, 0);
        root_motion = blend_children(node, n, indices, weights, &a, &b, src, min_level);
        NAL_AnimState_Copy(dst, &a);
        NAL_AnimState_Destroy(&b);
        NAL_AnimState_Destroy(&a);
    }
    // The additive children, evaluated on top of that pose.
    n = active_children(node, true, indices, weights);
    if (n == 1) {
        AnimState a;
        NAL_AnimState_Init(&a, dst->clip, 0);
        AnimTree_CalcPose_r(first + indices[0], &a, dst, arg4, arg5, min_level);
        NAL_AnimState_Copy(dst, &a);
        NAL_AnimState_Destroy(&a);
    } else if (n > 1) {
        AnimState a, b;
        NAL_AnimState_Init(&a, dst->clip, 0);
        NAL_AnimState_Init(&b, dst->clip, 0);
        blend_children(node, n, indices, weights, &a, &b, dst, min_level);
        NAL_AnimState_Copy(dst, &a);
        NAL_AnimState_Destroy(&b);
        NAL_AnimState_Destroy(&a);
    }
    return root_motion;
}

uint32_t XB_CDECL NAL_FindSkeleton(uint32_t name) {
    const uint32_t manager = title_global<uint32_t>(0x00909838);
    const uint32_t find = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(manager)))[2];
    return title_function<uint32_t XB_THISCALL(uint32_t, uint32_t)>(find)(manager, name);
}


AnimState* XB_REGS1 XB_CDECL Anim_CreateClipState(int32_t context, uint8_t* clip) {
    uint8_t saved = 0;
    if (context != -1) {
        saved = title_global<uint8_t>(0x0095f5d0);
        const uint32_t system = title_global<uint32_t>(0x0095f638);
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = 1;
        title_call_regs(0x0028f6c0, (static_cast<uint32_t>(context) & ~0xffu) | saved, system,
                        static_cast<uint32_t>(context));  // MemContext_Push
    }
    AnimState* state = static_cast<AnimState*>(Mem_AlignedAlloc(0x10, 4, 0));
    if (state) state = NAL_AnimState_Init(state, clip, 0);
    if (context != -1) {
        const uint32_t system = title_global<uint32_t>(0x0095f638);
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = saved;
        anim_memcontext_pop(system, static_cast<uint32_t>(context));
    }
    return state;
}

namespace {

// Slot `slot` of a NAL manager (Anim_InitManagers: 0x0090982c anim files, 0x00909830 clips,
// 0x00909828, 0x00909838 skeletons), thiscall.
template <typename... Args>
uint32_t manager_call(uint32_t global, uint32_t slot, Args... args) {
    const uint32_t manager = title_global<uint32_t>(global);
    const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(manager)))[slot];
    return title_function<uint32_t XB_THISCALL(uint32_t, Args...)>(method)(manager, args...);
}

// An object's own vtable slot, thiscall without arguments.
void object_call(uint8_t* object, uint32_t slot) {
    const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<uint32_t*>(object)))[slot];
    title_function<void XB_THISCALL(uint8_t*)>(method)(object);
}

// The class vtable registered for a clip's or skeleton's type: the class map 0x00903fe0 looked up
// by the type name at `type` + 0x28, then its record + 0x20 -> `offset`.
uint32_t class_vtable(uint32_t type, uint32_t offset) {
    using Find = uint8_t* XB_THISCALL(uint32_t, uint32_t);
    const uint8_t* entry = title_function<Find>(0x00093f10)(0x00903fe0, type + 0x28);  // HashStrMap_Find
    const uint8_t* record = *reinterpret_cast<uint8_t* const*>(entry + 0x20);
    return *reinterpret_cast<const uint32_t*>(record + offset);
}

using FreeFn = void XB_CDECL(void*);

// Relative links (offsets from the item itself) made absolute; 0 stays 0.
void fix_link(uint8_t* item, uint32_t field) {
    uint32_t& link = *reinterpret_cast<uint32_t*>(item + field);
    if (link) link += static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item));
}

// The clip chain of an anim file: links made absolute, each clip's skeleton (+0x30) from the
// file's skeleton table by index (+0x28), its class vtable installed and initialized (slot 3,
// +0x3c = 0 for registered clips, slot 1); registered clips go to the clip manager.
void fix_clips(uint8_t* clip, uint32_t* skeletons, bool register_clips) {
    for (; clip; clip = *reinterpret_cast<uint8_t**>(clip + 4)) {
        fix_link(clip, 4);
        const uint32_t skeleton = skeletons[*reinterpret_cast<uint32_t*>(clip + 0x28)];
        *reinterpret_cast<uint32_t*>(clip + 0x30) = skeleton;
        *reinterpret_cast<uint32_t*>(clip) = class_vtable(skeleton, 0x28);
        object_call(clip, 3);
        if (register_clips) *reinterpret_cast<uint32_t*>(clip + 0x3c) = 0;
        object_call(clip, 1);
        if (register_clips) manager_call(0x00909830, 3, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip)));
    }
}

// The file's skeletons (count +0xc, 32-byte names from `names`) looked up in the skeleton manager.
uint32_t* find_skeletons(uint8_t* file, uint32_t names) {
    const int32_t count = *reinterpret_cast<int32_t*>(file + 0xc);
    uint32_t* skeletons = static_cast<uint32_t*>(Mem_AlignedAlloc(static_cast<uint32_t>(count) << 2, 4, 0));
    for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(file + 0xc); ++i)
        skeletons[i] = manager_call(0x00909838, 2, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file + names + 0x20 * i)));
    return skeletons;
}

}  // namespace

uint32_t XB_CDECL NAL_FindClip(uint32_t name) { return manager_call(0x00909830, 2, name); }

uint32_t XB_CDECL NAL_PtrField4(const uint32_t* p) { return p ? p[1] : 0; }

uint32_t XB_CDECL NAL_AnimFile_Fixup(uint8_t* file) {
    uint32_t* skeletons = find_skeletons(file, 0x48);
    uint32_t& clips = *reinterpret_cast<uint32_t*>(file + 0x34);
    if (clips) {
        clips += static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file));
        fix_clips(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(clips)), skeletons, true);
    }
    title_function<FreeFn>(0x000939b0)(skeletons);  // Mem_AlignedFree
    *reinterpret_cast<uint32_t*>(file + 4) |= 8;
    return 1;
}

uint32_t XB_CDECL NAL_GroupedAnimFile_Fixup(uint8_t* file) {
    uint32_t* skeletons = find_skeletons(file, 0x50);
    uint32_t& groups = *reinterpret_cast<uint32_t*>(file + 0x34);
    if (groups) {
        groups += static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file));
        for (uint8_t* group = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(groups)); group;
             group = *reinterpret_cast<uint8_t**>(group)) {
            fix_link(group, 0);
            uint32_t& clips = *reinterpret_cast<uint32_t*>(group + 8);
            if (!clips) continue;
            clips += static_cast<uint32_t>(reinterpret_cast<uintptr_t>(group));
            fix_clips(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(clips)), skeletons, false);
        }
    }
    title_function<FreeFn>(0x000939b0)(skeletons);  // Mem_AlignedFree
    return 1;
}

void XB_CDECL NAL_GroupedAnimFile_ForEachClip(const uint8_t* file) {
    for (uint8_t* group = *reinterpret_cast<uint8_t* const*>(file + 0x34); group; group = *reinterpret_cast<uint8_t**>(group))
        for (uint8_t* clip = *reinterpret_cast<uint8_t**>(group + 8); clip; clip = *reinterpret_cast<uint8_t**>(clip + 4))
            object_call(clip, 2);
}

uint8_t* XB_CDECL NAL_AnimFile_Register(const uint32_t* name, uint8_t* file) {
    if (uint8_t* found = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(manager_call(0x0090982c, 2, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)))))) {
        ++*reinterpret_cast<int32_t*>(found + 0x44);
        return found;
    }
    std::memcpy(file + 0x10, name, 0x20);
    *reinterpret_cast<int32_t*>(file + 0x44) = 1;
    *reinterpret_cast<uint32_t*>(file + 4) |= 4;
    if (!(NAL_AnimFile_Fixup(file) & 0xff)) return nullptr;
    manager_call(0x0090982c, 3, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)));
    return file;
}

uint8_t* XB_CDECL NAL_Skeleton_Register(uint8_t* skeleton) {
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(skeleton));
    if (uint8_t* found = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(manager_call(0x00909838, 2, self + 8)))) {
        ++*reinterpret_cast<int32_t*>(found + 0x48);
        return found;
    }
    *reinterpret_cast<uint32_t*>(skeleton) = class_vtable(self, 0x2c);
    object_call(skeleton, 4);
    object_call(skeleton, 2);
    *reinterpret_cast<uint32_t*>(skeleton + 0x50) = 0;
    manager_call(0x00909838, 3, self);
    return skeleton;
}

uint32_t XB_CDECL NAL_AnimFile_Release(uint32_t file) { return manager_call(0x0090982c, 8, file, 0u, 0u); }

uint32_t XB_CDECL NAL_Manager828_Release(uint32_t item) { return manager_call(0x00909828, 8, item, 0u, 0u); }

uint32_t XB_CDECL NAL_Skeleton_Release(uint32_t skeleton) { return manager_call(0x00909838, 8, skeleton, 0u, 0u); }

void XB_CDECL NAL_InitRegisteredTypes() {
    for (uint8_t* type = title_global<uint8_t*>(0x00909840); type; type = *reinterpret_cast<uint8_t**>(type + 4))
        object_call(type, 0);
}

void XB_THISCALL NAL_BlockCache_FreeAll(uint32_t* cache) {
    while (cache[2]) {
        uint32_t* block = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(cache[2]));
        cache[2] = block[2];
        const uint32_t size = block[3];
        const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<uint32_t*>(
            static_cast<uintptr_t>(cache[0]))))[2];
        title_function<void XB_THISCALL(uint32_t, uint32_t*, uint32_t)>(method)(cache[0], block, size);
    }
}

namespace {

uint32_t align_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

// The group records of a clip (0x30 bytes, type name first): the type's class from the map
// 0x00903ff4 into record + 0x20.
void resolve_group_classes(uint8_t* clip, uint32_t count_at, uint32_t records_at) {
    using Find = uint8_t* XB_THISCALL(uint32_t, uint32_t);
    for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(clip + count_at); ++i) {
        const uint32_t record = *reinterpret_cast<uint32_t*>(clip + records_at) + 0x30 * i;
        const uint8_t* entry = title_function<Find>(0x00093f10)(0x00903ff4, record);  // HashStrMap_Find
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(record) + 0x20) = *reinterpret_cast<const uint32_t*>(entry + 0x20);
    }
}

// Slot 4 of each group's class (thiscall: record, &cursor A, &cursor B) over `count` records.
void fixup_groups(uint8_t* clip, uint32_t count_at, uint32_t records_at, uint32_t a, uint32_t b) {
    uint32_t cursors[2] = {b, a};  // [esp+0xc], [esp+0x10] in the original
    uint32_t record = *reinterpret_cast<uint32_t*>(clip + records_at);
    for (int32_t i = 0; i < *reinterpret_cast<int32_t*>(clip + count_at); ++i, record += 0x30) {
        const uint32_t object = *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(record) + 0x20);
        const uint32_t method = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*reinterpret_cast<uint32_t*>(
            static_cast<uintptr_t>(object))))[4];
        title_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t*, uint32_t*)>(method)(object, record, &cursors[1], &cursors[0]);
    }
}

}  // namespace

uint32_t XB_THISCALL NAL_GenericAnim_Init(uint8_t* clip) {
    auto field = [clip](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(clip + offset); };
    const uint32_t a = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip)) + 0xe3) & ~3u;
    field(0x64) = a;
    field(0x68) = a + field(0x60) * 20;
    field(0x6c) = field(0x68) + reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(a))[1];
    field(0x74) = (reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(a))[2] + field(0x6c) + 3) & ~3u;
    field(0x80) = (field(0x70) * 48 + field(0x74) + 3) & ~3u;
    field(0x88) = (field(0x78) * 40 + field(0x80) + 3) & ~3u;
    field(0x94) = align_up(field(0x84) * 48 + field(0x88), field(0x90));
    field(0x9c) = (field(0x8c) + field(0x94) + 3) & ~3u;
    field(0xa4) = (field(0x98) + field(0x9c) + 3) & ~3u;
    field(0xb0) = align_up(field(0xa0) * 48 + field(0xa4), field(0xac));
    field(0xb8) = (field(0xa8) + field(0xb0) + 3) & ~3u;
    field(0xc4) = align_up(field(0xb4) + field(0xb8), field(0xc0));
    field(0xc8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clip));
    field(0xd0) = field(0x94);
    resolve_group_classes(clip, 0x84, 0x88);
    resolve_group_classes(clip, 0xa0, 0xa4);
    fixup_groups(clip, 0x84, 0x88, field(0x94), field(0x9c));
    fixup_groups(clip, 0xa0, 0xa4, field(0xb0), field(0xb8));
    return field(0xa0);
}

float* XB_THISCALL NAL_Skeleton_BoneTransform(const uint32_t* skeleton, float* out, int32_t index) {
    const uint8_t* clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(skeleton[0]));
    const uint8_t* bone = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(clip + 0x74))) +
                          0x30 * index;
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(bone + 0x20);
    const uint32_t shared = *reinterpret_cast<const uint32_t*>(clip + 0xb0);
    // Position (+0x24) and rotation (+0x28) offsets: into the clip's shared data when the flag
    // bit is set, else into this skeleton's own data (+8).
    const uint8_t* position = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        (flags & 1 ? shared : skeleton[2]) + *reinterpret_cast<const uint32_t*>(bone + 0x24)));
    const uint8_t* rotation = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        (flags & 2 ? shared : skeleton[2]) + *reinterpret_cast<const uint32_t*>(bone + 0x28)));
    alignas(16) float local[8] = {};  // {quaternion, position, w never set}
    std::memcpy(&local[4], position, 12);
    std::memcpy(local, rotation, 16);
    const int32_t parent = *reinterpret_cast<const int32_t*>(bone + 0x2c);
    if (parent >= 0) {
        alignas(16) float parent_transform[8] = {};
        alignas(16) float world[8];
        NAL_Skeleton_BoneTransform(skeleton, parent_transform, parent);
        std::memcpy(local, QuatTrans_Concat(world, local, parent_transform), 32);
    }
    std::memcpy(out, local, 16);
    out[4] = local[4];
    out[5] = local[5];
    out[6] = local[5];  // the title stores y again where z belongs
    return out;
}

namespace {

uint32_t& word_at(uint32_t address) { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(address)); }
uint32_t& word_of(const void* base, uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(base) + offset);
}
template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A thiscall method without arguments in slot `slot` of an object's vtable.
uint32_t method_call(uint32_t object, uint32_t slot) {
    return title_function<uint32_t XB_THISCALL(uint32_t)>(word_at(word_at(object) + slot * 4))(object);
}

// The channel maps (0x00906810: 0x43 buckets): per (clip skeleton, target skeleton) pair
// {refcount, next, clip skeleton, target skeleton, &map, map[clip skeleton +0x7c]} -
// each channel's offset in the target's state data (-1 = the target lacks it).
uint32_t*& channel_maps() { return title_global<uint32_t*>(0x00906810); }
uint32_t channel_map_bucket(uint32_t target, uint32_t source) { return (((target >> 3) ^ source) >> 3) % 0x43; }

}  // namespace

uint8_t* XB_THISCALL NAL_AnimTrackBase_DeletingDtor(uint8_t* track, uint32_t flags) {
    word_of(track, 0) = 0x007c3250;
    --word_of(pointer<uint8_t>(word_of(track, 0x10)), 0x3c);
    if (flags & 1) title_function<FreeFn>(0x000939b0)(track);  // Mem_AlignedFree
    return track;
}

void XB_THISCALL NAL_AnimTrack_Dtor(uint8_t* track) {
    word_of(track, 0) = 0x007c3258;
    const uint32_t bucket = channel_map_bucket(word_of(track, 0xc), word_of(pointer<uint8_t>(word_of(track, 0x10)), 0x30));
    uint32_t* previous = nullptr;
    for (uint32_t* node = pointer<uint32_t>(channel_maps()[bucket]); node; previous = node, node = pointer<uint32_t>(node[1])) {
        if (address_of(node) != word_of(track, 0x2c)) continue;
        --node[0];
        if (!pointer<uint32_t>(word_of(track, 0x2c))[0]) {
            if (previous)
                previous[1] = node[1];
            else
                channel_maps()[bucket] = node[1];
            title_function<FreeFn>(0x000939b0)(pointer<void>(word_of(track, 0x2c)));  // Mem_AlignedFree
        }
        break;
    }
    NAL_AnimState_Destroy(reinterpret_cast<AnimState*>(track + 0x14));
    word_of(track, 0) = 0x007c3250;
    --word_of(pointer<uint8_t>(word_of(track, 0x10)), 0x3c);
}

uint8_t* XB_THISCALL NAL_AnimTrack_DeletingDtor(uint8_t* track, uint32_t flags) {
    NAL_AnimTrack_Dtor(track);
    if (flags & 1) title_function<FreeFn>(0x000939b0)(track);  // Mem_AlignedFree
    return track;
}

uint8_t* XB_THISCALL NAL_AnimTrack_Construct(uint8_t* track, uint8_t* clip, uint8_t* skeleton) {
    word_of(track, 0) = 0x007c3250;
    const float duration = *reinterpret_cast<const float*>(clip + 0x38);
    float rate = 0.0f;
    *reinterpret_cast<float*>(track + 4) = duration;
    if (!(duration == 0.0f)) rate = title_global<float>(0x0083eb20) / duration;
    *reinterpret_cast<float*>(track + 8) = rate;
    word_of(track, 0x10) = address_of(clip);
    word_of(track, 0xc) = skeleton ? address_of(skeleton) : word_of(clip, 0x30);
    ++word_of(clip, 0x3c);
    word_of(track, 0) = 0x007c3258;
    NAL_AnimState_Init(reinterpret_cast<AnimState*>(track + 0x14), skeleton, 0);
    word_of(track, 0x24) = title_global<uint32_t>(0x007c3260);
    const uint32_t target = word_of(track, 0xc);
    const uint32_t source = word_of(pointer<uint8_t>(word_of(track, 0x10)), 0x30);
    if (!channel_maps()) {
        channel_maps() = static_cast<uint32_t*>(Mem_AlignedAlloc(0x10c, 4, 0));
        std::memset(channel_maps(), 0, 0x43 * 4);
    }
    const uint32_t bucket = channel_map_bucket(target, source);
    for (uint32_t* node = pointer<uint32_t>(channel_maps()[bucket]); node; node = pointer<uint32_t>(node[1])) {
        if (node[2] == source && node[3] == target) {
            word_of(track, 0x2c) = address_of(node);
            ++node[0];
            return track;
        }
    }
    uint8_t* src = pointer<uint8_t>(source);
    uint32_t* node = static_cast<uint32_t*>(Mem_AlignedAlloc(word_of(src, 0x7c) * 4 + 0x14, 4, 0));
    word_of(track, 0x2c) = address_of(node);
    node[0] = 1;
    node[2] = source;
    node[3] = target;
    node[1] = channel_maps()[bucket];
    node[4] = address_of(node + 5);
    channel_maps()[bucket] = address_of(node);
    auto map = [track]() { return pointer<uint32_t>(pointer<uint32_t>(word_of(track, 0x2c))[4]); };
    if (target == source) {
        // The same skeleton: the channels' own offsets.
        int32_t k = 0;
        for (int32_t g = 0; g < static_cast<int32_t>(word_of(src, 0x84)); ++g) {
            const uint32_t group = word_of(src, 0x88) + g * 0x30;
            uint32_t offset = word_at(group + 0x2c);
            for (int32_t j = 0; j < static_cast<int32_t>(word_at(group + 0x28)); ++j, ++k) {
                map()[k] = offset;
                offset += method_call(word_at(group + 0x20), 1);
            }
        }
        return track;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(word_of(src, 0x7c)); ++i) map()[i] = 0xffffffff;
    uint8_t* dst = pointer<uint8_t>(target);
    // Channels matched by bone name (records +0x74, 0x30 each) and channel record (+0x80, 40
    // bytes, first 32 compared) within groups of the same channel type.
    for (int32_t a = 0; a < static_cast<int32_t>(word_of(src, 0x84)); ++a) {
        const uint32_t group_a = word_of(src, 0x88) + a * 0x30;
        for (int32_t b = 0; b < static_cast<int32_t>(word_of(dst, 0x84)); ++b) {
            const uint32_t group_b = word_of(dst, 0x88) + b * 0x30;
            const uint32_t type_a = method_call(word_at(group_a + 0x20), 0);
            if (method_call(word_at(group_b + 0x20), 0) != type_a) continue;
            for (int32_t j = 0; j < static_cast<int32_t>(word_at(group_a + 0x28)); ++j) {
                const int32_t count_b = static_cast<int32_t>(word_at(group_b + 0x28));
                int32_t index = static_cast<int32_t>(word_at(group_b + 0x24));
                if (count_b <= 0) continue;
                const uint32_t record_a = word_of(src, 0x80) + (word_at(group_a + 0x24) + j) * 40;
                const uint32_t* name_a = pointer<uint32_t>(word_at(record_a + 0x20) * 0x30 + word_of(src, 0x74));
                for (int32_t m = 0; m < count_b; ++m, ++index) {
                    const uint32_t record_b = word_of(dst, 0x80) + index * 40;
                    const uint32_t* name_b = pointer<uint32_t>(word_at(record_b + 0x20) * 0x30 + word_of(dst, 0x74));
                    bool same = true;
                    for (int n = 0; n < 8 && same; ++n) same = name_b[n] == name_a[n];
                    for (int n = 0; n < 8 && same; ++n) same = pointer<uint32_t>(record_b)[n] == pointer<uint32_t>(record_a)[n];
                    if (!same) continue;
                    const uint32_t size = method_call(word_at(group_b + 0x20), 1);
                    map()[word_at(group_a + 0x24) + j] =
                        static_cast<uint32_t>(index - static_cast<int32_t>(word_at(group_b + 0x24))) * size + word_at(group_b + 0x2c);
                    break;
                }
            }
        }
    }
    return track;
}

void XB_THISCALL NAL_GenericAnim_Unload(uint8_t* clip) {
    const uint8_t* skeleton = pointer<uint8_t>(word_of(clip, 0x30));
    for (int32_t i = 0; i < static_cast<int32_t>(word_of(clip, 0x64)); ++i) {
        if (!pointer<uint32_t>(word_of(clip, 0x60))[i]) continue;
        // Each cached frame of the block skipped once through every channel (slot 15).
        int32_t frames = static_cast<int32_t>(word_of(clip, 0x68));
        const int32_t total = static_cast<int32_t>(word_of(clip, 0x44));
        if ((i + 1) * frames > total) frames = total - frames * i;
        for (int32_t f = 0; f < frames; ++f) {
            uint32_t cursor = word_at(word_of(clip, 0x5c)) * f + pointer<uint32_t>(word_of(clip, 0x60))[i] + 0x20;
            uint32_t a = word_of(skeleton, 0xc4);
            uint32_t b = word_of(clip, 0x50);
            uint32_t group = word_of(skeleton, 0x88);
            for (int32_t g = 0; g < static_cast<int32_t>(word_of(skeleton, 0x84)); ++g, group += 0x30) {
                uint32_t context[4] = {address_of(clip), group, address_of(&a), address_of(&b)};
                const uint32_t channel_object = word_at(group + 0x20);
                title_function<void XB_THISCALL(uint32_t, uint32_t*, uint32_t*)>(word_at(word_at(channel_object) + 0x3c))(
                    channel_object, context, &cursor);
            }
        }
        NAL_BlockCache_Free(&title_global<uint32_t>(0x00903f9c), pointer<uint32_t>(pointer<uint32_t>(word_of(clip, 0x60))[i]));
        pointer<uint32_t>(word_of(clip, 0x60))[i] = 0;
    }
    // The block table's pointers back to offsets.
    const uint32_t base = word_of(clip, 0x6c) + word_of(clip, 0x64) * 4;
    for (int32_t i = 0; i < static_cast<int32_t>(word_of(clip, 0x64)); ++i) pointer<uint32_t>(word_of(clip, 0x6c))[i] -= base;
}

uint32_t XB_THISCALL NAL_ChannelClass_Register(const uint8_t* entry) {
    alignas(4) uint8_t name[0x20];
    title_function<uint8_t* XB_THISCALL(uint8_t*, uint32_t)>(0x00011060)(name, word_of(entry, 8));  // HashStr_Init
    return title_function<uint32_t XB_THISCALL(uint32_t, uint8_t*, uint32_t)>(0x00093c70)(0x00903ff4, name,
                                                                                    word_of(entry, 0xc));  // map insert
}

namespace {

// The title CRT's __CIacos (0x001f0ff4): the float in ST, the result stored as float.
float title_ci_acos(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x001f0ff4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

}  // namespace

void XB_THISCALL NAL_GenericAnim_ExtractState(uint8_t* clip, AnimState* dst, const float* pose, float* work,
                                              const AnimState* src, int32_t range) {
    const uint32_t entry = word_of(clip, 0x64) + static_cast<uint32_t>(range) * 20;
    std::memcpy(work, pose, word_at(entry) << 6);
    NAL_AnimState_Copy(dst, src);
    const uint8_t* code = pointer<uint8_t>(word_of(clip, 0x6c));
    const uint8_t* const end = code + word_at(entry + 8);
    uint8_t* out = dst->data;
    while (code < end) {
        const uint32_t op = static_cast<uint32_t>(*code++) - 6;
        switch (op) {
        case 0: {  // 6: a bone's position
            const float* m = work + *code++ * 16;
            std::memcpy(out, m + 12, 12);
            out += 0xc;
            break;
        }
        case 1: {  // 7: a bone's rotation
            alignas(16) float q[4];
            const float* r = Quat_FromAxis3(q, work + *code++ * 16);
            std::memcpy(out, r, 16);
            out += 0x10;
            break;
        }
        case 2: {  // 8: rotation and position (the eighth word left as it is)
            alignas(16) float q[4];
            const float* m = work + *code++ * 16;
            const float* r = Quat_FromAxis3(q, m);
            std::memcpy(out, r, 16);
            std::memcpy(out + 0x10, m + 12, 12);
            out += 0x20;
            break;
        }
        case 3: {  // 9: a unit scale
            const float one = title_global<float>(0x0083eb20);
            std::memcpy(out, &one, 4);
            std::memcpy(out + 4, &one, 4);
            std::memcpy(out + 8, &one, 4);
            out += 0xc;
            break;
        }
        case 5: {  // 11: a two-bone chain's bend angle and rotations
            const uint32_t b0 = code[0], b1 = code[1], b2 = code[2], b3 = code[3];
            code += 4;
            const float* a = pose + b1 * 16;
            const float* c = pose + b2 * 16;
            const float a0 = 0.0f - a[0], a1 = 0.0f - a[1], a2 = 0.0f - a[2];
            const float c0 = 0.0f - c[0], c1 = 0.0f - c[1], c2 = 0.0f - c[2];
            const float dot = ((c2 * a2) + (c1 * a1)) + (c0 * a0);
            float clamped;
            if (title_global<float>(0x0083eb34) > dot)
                clamped = title_global<float>(0x0083eb34);
            else if (dot > title_global<float>(0x0083eb20))
                clamped = title_global<float>(0x0083eb20);
            else
                clamped = dot;
            float angle = title_ci_acos(clamped);
            const float x = c2 * a1 - c1 * a2;
            const float y = a2 * c0 - c2 * a0;
            const float z = c1 * a0 - a1 * c0;
            const float side = ((c[6] * z) + (c[5] * y)) + (x * c[4]);
            if (0.0f > side) angle = 0.0f - angle;
            alignas(16) float inverse[16];
            alignas(16) float product[16];
            Mat44_MultiplyInPlace(work + b3 * 16, product, Mat44_InverseRigid(c, inverse));
            Mat44_MultiplyInPlace(work + b2 * 16, product, Mat44_InverseRigid(a, inverse));
            Mat44_MultiplyInPlace(work + b1 * 16, product, Mat44_InverseRigid(pose + b0 * 16, inverse));
            alignas(16) float q3[4];
            alignas(16) float q1[4];
            const float* r3 = Quat_FromAxis3(q3, work + b3 * 16);
            const float* r1 = Quat_FromAxis3(q1, work + b1 * 16);
            // 0x50 bytes; the words the original never sets are its (zeroed) dead stack.
            uint32_t record[20] = {};
            std::memcpy(record, r1, 16);
            std::memcpy(record + 4, r3, 16);
            const float zero = 0.0f;
            std::memcpy(record + 15, &zero, 4);
            const float scaled = ((angle * title_global<float>(0x007c3268)) * title_global<float>(0x0083f118)) +
                                 title_global<float>(0x0083eb24);
            const uint16_t code16 = static_cast<uint16_t>(static_cast<int32_t>(scaled));
            std::memcpy(record + 16, &code16, 2);
            std::memcpy(out, record, 0x50);
            out += 0x50;
            break;
        }
        case 7: {  // 13: a bone made relative to another
            const uint32_t b0 = code[0], b1 = code[1];
            code += 2;
            alignas(16) float inverse[16];
            alignas(16) float product[16];
            Mat44_MultiplyInPlace(work + b1 * 16, product, Mat44_InverseRigid(pose + b0 * 16, inverse));
            break;
        }
        case 8:  // 14: skip bytes of the state
            out += *code++;
            break;
        default:  // 10, 12 and the others: nothing
            break;
        }
    }
}

// The streamed-anim players: a base (vtable 0x007c3c60) with a list of channel bindings
// (+8 head, +0xc tail; node {sink, clip, track, next}), the streamed player (0x007c3c70,
// 0x7c bytes) and the clip-set player (0x007c4160, 0x20 bytes).

void XB_THISCALL NAL_AnimPlayer_Dtor(uint8_t* player) {
    word_of(player, 0) = 0x007c3c60;
    while (word_of(player, 8)) {
        uint8_t* node = pointer<uint8_t>(word_of(player, 8));
        word_of(player, 8) = word_of(node, 0xc);
        method_call(word_of(node, 0), 3);
        if (const uint32_t track = word_of(node, 8))
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(word_at(word_at(track)))(track, 1);
        title_function<FreeFn>(0x000939b0)(node);  // Mem_AlignedFree
    }
}

uint8_t* XB_THISCALL NAL_AnimPlayer_DeletingDtor(uint8_t* player, uint32_t flags) {
    NAL_AnimPlayer_Dtor(player);
    if (flags & 1) title_function<FreeFn>(0x000939b0)(player);  // Mem_AlignedFree
    return player;
}

void XB_THISCALL NAL_StreamedAnim_Dtor(uint8_t* player) {
    word_of(player, 0) = 0x007c3c70;
    while (word_of(player, 8)) {
        uint8_t* node = pointer<uint8_t>(word_of(player, 8));
        word_of(player, 8) = word_of(node, 0xc);
        if (const uint32_t track = word_of(node, 8)) {
            const uint32_t clip = word_at(track + 0x10);
            title_function<void XB_THISCALL(uint32_t, uint32_t)>(word_at(word_at(track)))(track, 1);
            method_call(clip, 2);
        }
        if (const uint32_t sink = word_of(node, 0)) method_call(sink, 3);
        title_function<FreeFn>(0x000939b0)(node);  // Mem_AlignedFree
    }
    title_function<FreeFn>(0x000939b0)(pointer<void>(word_of(player, 0x38)));
    title_function<FreeFn>(0x000939b0)(pointer<void>(word_of(player, 0x48)));
    title_function<FreeFn>(0x000939b0)(pointer<void>(word_of(player, 4)));
    NAL_AnimPlayer_Dtor(player);
}

uint8_t* XB_THISCALL NAL_StreamedAnim_DeletingDtor(uint8_t* player, uint32_t flags) {
    NAL_StreamedAnim_Dtor(player);
    if (flags & 1) title_function<FreeFn>(0x000939b0)(player);  // Mem_AlignedFree
    return player;
}

uint8_t* XB_CDECL NAL_StreamedAnim_Create(uint32_t file, uint32_t offset, uint32_t end, uint32_t size, uint32_t bind,
                                          uint32_t bind_context) {
    uint8_t* player = static_cast<uint8_t*>(Mem_AlignedAlloc(0x7c, 4, 0));
    if (!player) return nullptr;
    word_of(player, 0x20) = bind;
    word_of(player, 0x24) = bind_context;
    word_of(player, 0x28) = file;
    word_of(player, 0x44) = size;
    word_of(player, 4) = 0;
    word_of(player, 8) = 0;
    word_of(player, 0xc) = 0;
    word_of(player, 0x18) = 0;
    word_of(player, 0x1c) = 0;
    word_of(player, 0x38) = 0;
    player[0x78] = 0;
    player[0x79] = 0;
    word_of(player, 0x48) = 0;
    *reinterpret_cast<float*>(player + 0x10) = 0.0f;
    *reinterpret_cast<float*>(player + 0x14) = 0.0f;
    word_of(player, 0) = 0x007c3c70;
    word_of(player, 0x58) = offset;
    word_of(player, 0x70) = end;
    return player;
}

uint32_t* XB_THISCALL NAL_AnimPlayer_AddBinding(uint8_t* player, uint32_t sink, uint32_t clip) {
    uint32_t* node = static_cast<uint32_t*>(Mem_AlignedAlloc(0x10, 4, 0));
    node[0] = sink;
    node[1] = clip;
    node[2] = 0;
    node[3] = 0;
    if (uint32_t* tail = pointer<uint32_t>(word_of(player, 0xc))) {
        tail[3] = address_of(node);
        word_of(player, 0xc) = address_of(node);
        return node;
    }
    word_of(player, 8) = address_of(node);
    word_of(player, 0xc) = address_of(node);
    return node;
}

uint8_t* XB_THISCALL NAL_ClipSetPlayer_Init(uint8_t* player, uint8_t* group, uint32_t bind, uint32_t bind_context) {
    word_of(player, 0x18) = 0;
    word_of(player, 4) = address_of(group);
    word_of(player, 8) = 0;
    word_of(player, 0xc) = 0;
    *reinterpret_cast<float*>(player + 0x10) = 0.0f;
    *reinterpret_cast<float*>(player + 0x14) = 0.0f;
    word_of(player, 0) = 0x007c4160;
    const uint32_t set = word_of(group, 0x34);
    word_of(player, 0x1c) = set;
    word_of(player, 0x18) = set;
    for (uint32_t clip = word_at(set + 8); clip; clip = NAL_PtrField4(pointer<uint32_t>(clip))) {
        const uint32_t sink = title_function<uint32_t XB_CDECL(uint8_t*, uint32_t, uint32_t)>(bind)(group, clip + 8, bind_context);
        if (!sink) continue;
        uint32_t* node = static_cast<uint32_t*>(Mem_AlignedAlloc(0x10, 4, 0));
        node[0] = sink;
        node[1] = clip;
        node[2] = 0;
        node[3] = 0;
        if (uint32_t* tail = pointer<uint32_t>(word_of(player, 0xc)))
            tail[3] = address_of(node);
        else
            word_of(player, 8) = address_of(node);
        word_of(player, 0xc) = address_of(node);
    }
    return player;
}

uint8_t* XB_THISCALL NAL_ClipSetPlayer_Create(uint8_t* group, uint32_t bind, uint32_t bind_context) {
    uint8_t* player = static_cast<uint8_t*>(Mem_AlignedAlloc(0x20, 4, 0));
    if (!player) return nullptr;
    return NAL_ClipSetPlayer_Init(player, group, bind, bind_context);
}

void XB_THISCALL NAL_StreamedAnim_Setup(uint8_t* player) {
    uint8_t* file = pointer<uint8_t>(word_of(player, 4));
    uint32_t name = address_of(file) + 0x50;
    word_of(player, 0x38) = address_of(Mem_AlignedAlloc(word_of(file, 0xc) << 2, 4, 0));
    for (int32_t i = 0; i < static_cast<int32_t>(word_of(pointer<uint8_t>(word_of(player, 4)), 0xc)); ++i, name += 0x20)
        pointer<uint32_t>(word_of(player, 0x38))[i] = manager_call(0x00909838, 2, name);
    uint8_t* header = pointer<uint8_t>(word_of(player, 0x18));
    word_of(player, 0x74) = word_of(header, 0);
    if (word_of(header, 8)) {
        word_of(header, 8) += address_of(header);
        for (uint8_t* clip = pointer<uint8_t>(word_of(pointer<uint8_t>(word_of(player, 0x18)), 8)); clip;
             clip = pointer<uint8_t>(word_of(clip, 4))) {
            fix_link(clip, 4);
            const uint32_t skeleton = pointer<uint32_t>(word_of(player, 0x38))[word_of(clip, 0x28)];
            word_of(clip, 0x30) = skeleton;
            word_of(clip, 0) = class_vtable(skeleton, 0x28);
            object_call(clip, 3);
            object_call(clip, 1);
            const uint32_t sink = title_function<uint32_t XB_CDECL(uint32_t, uint8_t*, uint32_t)>(word_of(player, 0x20))(
                word_of(player, 4), clip + 8, word_of(player, 0x24));
            NAL_AnimPlayer_AddBinding(player, sink, address_of(clip));
        }
    }
    word_of(player, 0x1c) = 6;
}

void XB_THISCALL NAL_StreamedAnim_RequestNext(uint8_t* player) {
    using Status = uint32_t XB_CDECL(uint32_t);
    using Queue = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t position;
    uint8_t buffer;
    if (word_of(player, 0x6c) == 0xffffffff) {
        if (player[0x79] == player[0x78]) return;
        position = word_of(player, 0x5c);
        buffer = player[0x79];
    } else {
        if (title_function<Status>(0x000c9db0)(word_of(player, 0x6c)) == 4) return;
        position = word_of(player, 0x5c) + word_of(player, 0x68);
        word_of(player, 0x5c) = position;
        buffer = player[0x79] ^ 1;
        word_of(player, 0x6c) = 0xffffffff;
        player[0x79] = buffer;
        if (buffer == player[0x78]) return;
    }
    if (static_cast<int32_t>(position) < static_cast<int32_t>(word_of(player, 0x70))) {
        word_of(player, 0x50 + buffer * 4) = position - word_of(player, 0x3c);
        const uint32_t length = word_of(player, 0x44) - word_of(player, 0x3c);
        word_of(player, 0x68) = length;
        word_of(player, 0x6c) = title_function<Queue>(0x000caed0)(
            word_of(player, 0x28), word_of(player, 0x58) + word_of(player, 0x5c),
            word_of(player, 0x48 + player[0x79] * 4) + word_of(player, 0x3c), length);  // File_QueueRequest
        return;
    }
    // At the end: start over when the animation loops.
    if (!(word_of(pointer<uint8_t>(word_of(player, 4)), 4) & 2)) return;
    word_of(player, 0x5c) = 0;
    word_of(player, 0x50 + buffer * 4) = 0;
    word_of(player, 0x68) = word_of(player, 0x44);
    word_of(player, 0x6c) = title_function<Queue>(0x000caed0)(word_of(player, 0x28), word_of(player, 0x58),
                                                              word_of(player, 0x48 + player[0x79] * 4),
                                                              word_of(player, 0x44));  // File_QueueRequest
}

void XB_THISCALL NAL_ClipSetPlayer_Update(uint8_t* player, float dt) {
    using Make = uint32_t XB_THISCALL(uint32_t, uint32_t);
    using Apply = void XB_THISCALL(uint32_t, uint32_t, float, float, float, float);
    auto real = [player](uint32_t offset) -> float& { return *reinterpret_cast<float*>(player + offset); };
    // Each binding's track (made by the sink's slot 0 when missing) gets the sink's slot 1:
    // (track, fraction, previous fraction, time, time at the start of the clip).
    auto apply = [player](uint32_t* node, float fraction, float previous, float time, float start) {
        if (!node[2]) node[2] = title_function<Make>(word_at(word_at(node[0])))(node[0], node[1]);
        title_function<Apply>(word_at(word_at(node[0]) + 4))(node[0], node[2], fraction, previous, time, start);
    };
    const uint8_t* group = pointer<uint8_t>(word_of(player, 4));
    if (!((word_of(group, 4) >> 1) & 1) && real(0x10) >= *reinterpret_cast<const float*>(group + 0x3c)) return;
    const float one = title_global<float>(0x0083eb20);
    float start = real(0x10);
    float previous = real(0x14);
    real(0x14) = previous + dt;
    float duration = *reinterpret_cast<const float*>(word_at(word_of(pointer<uint8_t>(word_of(player, 0x18)), 8)) + 0x38);
    if (real(0x14) >= duration) {
        // Past the clip's end: finish it, then go on with the next clip set (or the first
        // again when the group loops).
        for (;;) {
            const float step = duration - previous;
            dt = dt - step;
            real(0x10) = real(0x10) + step;
            const float inverse = one / duration;
            const float scaled = inverse * real(0x14);
            float fraction = one;
            if (!(scaled > one)) fraction = scaled;
            if (word_of(player, 8)) {
                const float from = inverse * previous;
                for (uint32_t* node = pointer<uint32_t>(word_of(player, 8)); node; node = pointer<uint32_t>(node[3]))
                    apply(node, fraction, from, real(0x10), start);
            }
            const uint8_t* set = pointer<uint8_t>(word_of(player, 0x18));
            uint32_t next;
            if (word_of(set, 0)) {
                real(0x14) = real(0x14) - duration;
                next = word_of(set, 0);
            } else {
                if (!((word_of(pointer<uint8_t>(word_of(player, 4)), 4) >> 1) & 1)) break;
                real(0x14) = real(0x14) - duration;
                next = word_of(player, 0x1c);
            }
            word_of(player, 0x18) = next;
            uint32_t clip = word_at(next + 8);
            previous = 0.0f;
            for (uint32_t* node = pointer<uint32_t>(word_of(player, 8)); node; node = pointer<uint32_t>(node[3])) {
                if (const uint32_t track = node[2]) title_function<void XB_THISCALL(uint32_t, uint32_t)>(word_at(word_at(track)))(track, 1);
                node[1] = clip;
                node[2] = 0;
                clip = NAL_PtrField4(pointer<uint32_t>(clip));
            }
            start = real(0x10);
            duration = *reinterpret_cast<const float*>(word_at(word_of(pointer<uint8_t>(word_of(player, 0x18)), 8)) + 0x38);
            if (!(real(0x14) >= duration)) break;
        }
    }
    real(0x10) = real(0x10) + dt;
    if (!word_of(player, 8)) return;
    const float inverse = one / duration;
    const float from = inverse * previous;
    for (uint32_t* node = pointer<uint32_t>(word_of(player, 8)); node; node = pointer<uint32_t>(node[3]))
        apply(node, inverse * real(0x14), from, real(0x10), start);
}

namespace {

// The file-request helpers the streamed player uses (title code, cdecl).
uint32_t file_status(uint32_t request) { return title_function<uint32_t XB_CDECL(uint32_t)>(0x000c9db0)(request); }
void file_progress(uint32_t request, uint32_t* out) {
    title_function<uint32_t* XB_CDECL(uint32_t, uint32_t*)>(0x000c9ea0)(request, out);
}
void file_pump() { title_function<void XB_CDECL()>(0x000ca690)(); }  // File_PumpRequests
uint32_t file_queue(uint32_t file, uint32_t offset, uint32_t buffer, uint32_t length) {
    return title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x000caed0)(file, offset, buffer,
                                                                                          length);  // File_QueueRequest
}

}  // namespace

void XB_THISCALL NAL_StreamedAnim_Update(uint8_t* player, float dt) {
    using Make = uint32_t XB_THISCALL(uint32_t, uint32_t);
    using Apply = void XB_THISCALL(uint32_t, uint32_t, float, float, float, float);
    auto field = [player](uint32_t offset) -> uint32_t& { return word_of(player, offset); };
    auto real = [player](uint32_t offset) -> float& { return *reinterpret_cast<float*>(player + offset); };
    auto buffer = [player](uint32_t which) { return word_of(player, 0x48 + which * 4); };
    // The original's locals the file helpers write (its zeroed dead stack when they do not).
    uint32_t local[3] = {0, 0, 0};
    switch (field(0x1c)) {
    case 0: {
        // The header: one aligned read of 0x50 bytes.
        const uint32_t id = title_function<uint32_t XB_CDECL(uint32_t)>(0x000c9390)(field(0x28));
        title_function<uint32_t XB_CDECL(uint32_t, uint32_t*)>(0x000c9940)(id, local);
        uint32_t alignment = local[0] <= local[1] ? local[1] : local[0];
        if (alignment < local[2]) alignment = local[2];
        const uint32_t size = (alignment + 0x4f) & ~(alignment - 1);
        field(0x34) = alignment;
        field(0x30) = size;
        field(0x2c) = address_of(Mem_AlignedAlloc(size, alignment, 0));
        field(0x6c) = file_queue(field(0x28), field(0x58), field(0x2c), field(0x30));
        field(0x1c) = 1;
        return;
    }
    case 1: {
        file_progress(field(0x6c), local);
        if (file_status(field(0x6c)) == 4) return;
        const int32_t header = static_cast<int32_t>(word_of(pointer<uint8_t>(field(0x2c)), 0x34));
        if (header <= static_cast<int32_t>(field(0x30))) {
            field(0x1c) = 3;
            return;
        }
        // The whole header: grow the buffer and read the rest.
        const uint32_t alignment = field(0x34);
        const uint32_t size = (static_cast<uint32_t>(header) + alignment + 3) & ~(alignment - 1);
        uint8_t* grown = static_cast<uint8_t*>(Mem_AlignedAlloc(size, alignment, 0));
        std::memcpy(grown, pointer<uint8_t>(field(0x2c)), field(0x30));
        title_function<FreeFn>(0x000939b0)(pointer<void>(field(0x2c)));  // Mem_AlignedFree
        const uint32_t done = field(0x30);
        field(0x2c) = address_of(grown);
        const uint32_t request = file_queue(field(0x28), field(0x58) + done, address_of(grown) + done, size - done);
        field(0x30) = size;
        field(0x6c) = request;
        field(0x1c) = 2;
        return;
    }
    case 2:
        file_progress(field(0x6c), local);
        if (file_status(field(0x6c)) == 4) return;
        field(0x1c) = 3;
        return;
    case 3: {
        // Keep the header (+4); two stream buffers of whole blocks (+0x3c each, +0x44 per
        // buffer); the first data after the header goes to the current buffer.
        const uint8_t* header = pointer<uint8_t>(field(0x2c));
        field(4) = address_of(Mem_AlignedAlloc(word_of(header, 0x34), 4, 0));
        std::memcpy(pointer<void>(field(4)), header, word_of(header, 0x34));
        const uint32_t alignment = field(0x34);
        const int32_t block = static_cast<int32_t>((word_of(pointer<uint8_t>(field(4)), 0x38) + alignment - 1) & ~(alignment - 1));
        const int32_t blocks = static_cast<int32_t>(field(0x44)) / block;
        field(0x3c) = static_cast<uint32_t>(block);
        field(0x40) = static_cast<uint32_t>(blocks & ~1);
        if (static_cast<int32_t>(field(0x40)) < 4) field(0x40) = 4;
        field(0x44) = static_cast<uint32_t>(static_cast<int32_t>(field(0x40)) / 2 * static_cast<int32_t>(field(0x3c)));
        const uint32_t buffers = address_of(Mem_AlignedAlloc(field(0x44) * 2, alignment, 0));
        field(0x4c) = field(0x44) + buffers;
        field(0x48) = buffers;
        field(0x50) = 0;
        std::memcpy(pointer<void>(buffer(player[0x78])), pointer<void>(field(0x2c)), field(0x30));
        title_function<FreeFn>(0x000939b0)(pointer<void>(field(0x2c)));  // Mem_AlignedFree
        const uint32_t done = field(0x30);
        const uint32_t length = field(0x44) - done;
        field(0x5c) = done;
        field(0x68) = length;
        field(0x6c) = file_queue(field(0x28), field(0x58) + done, buffer(player[0x79]) + done, length);
        field(0x60) = word_of(pointer<uint8_t>(field(4)), 0x34);
        field(0x1c) = 4;
        field(0x18) = buffer(player[0x78]) + field(0x60);
        return;
    }
    case 4: {
        // Wait until the first block is in.
        file_progress(field(0x6c), local);
        const uint32_t start = word_of(player, 0x50 + player[0x78] * 4);
        const int32_t read = static_cast<int32_t>(field(0x5c) + local[0]);
        bool ready = false;
        if (read >= static_cast<int32_t>(start + field(0x60) + 0xc) &&
            read >= static_cast<int32_t>(word_at(field(0x18)) + start + field(0x60)))
            ready = true;
        if (!ready && file_status(field(0x6c)) == 4) return;
        field(0x1c) = 5;
        return;
    }
    case 6:
        break;
    default:
        return;
    }

    // Playing: the current block's clips through the bindings' sinks (slot 1: track,
    // fraction, previous fraction, time, time at the block's start).
    const uint8_t* file = pointer<uint8_t>(field(4));
    if (!((word_of(file, 4) >> 1) & 1) && real(0x10) >= *reinterpret_cast<const float*>(file + 0x3c)) return;
    auto ensure_track = [](uint32_t* node) {
        if (!node[2]) node[2] = title_function<Make>(word_at(word_at(node[0])))(node[0], node[1]);
    };
    auto sink_apply = [](uint32_t* node, float fraction, float previous, float time, float start) {
        title_function<Apply>(word_at(word_at(node[0]) + 4))(node[0], node[2], fraction, previous, time, start);
    };
    float start, previous, duration;
    for (;;) {
        NAL_StreamedAnim_RequestNext(player);
        start = real(0x10);
        previous = real(0x14);
        duration = *reinterpret_cast<const float*>(word_at(field(0x18) + 8) + 0x38);
        if (!(dt + real(0x14) >= duration)) break;
        // The block ends within dt: wait for the next one's data.
        while (field(0x6c) != 0xffffffff) {
            file_progress(field(0x6c), local);
            const uint32_t next = word_at(field(0x18));
            const int32_t needed = next ? static_cast<int32_t>(word_of(player, 0x50 + player[0x78] * 4) + field(0x60) + next)
                                        : static_cast<int32_t>(field(0x70));
            if (static_cast<int32_t>(field(0x5c) + local[0]) >= needed && file_status(field(0x6c)) == 0) break;
            NAL_StreamedAnim_RequestNext(player);
            file_pump();
        }
        const float step = duration - real(0x14);
        real(0x10) = step + real(0x10);
        dt = dt - step;
        real(0x14) = duration;
        for (uint32_t* node = pointer<uint32_t>(field(8)); node; node = pointer<uint32_t>(node[3])) {
            if (!node[0]) continue;
            ensure_track(node);
            sink_apply(node, 1.0f, previous / duration, real(0x10), start);
        }
        real(0x14) = 0.0f;
        // The next block: in this buffer, wrapped into the other one, or the file again.
        const uint32_t next = word_at(field(0x18));
        if (next) {
            field(0x60) = field(0x60) + next;
            if (static_cast<int32_t>(field(0x60)) >= static_cast<int32_t>(field(0x44))) {
                player[0x78] ^= 1;
                field(0x60) = field(0x3c);
                field(0x18) = buffer(player[0x78]) + field(0x60);
            } else {
                const uint32_t current = player[0x78];
                field(0x18) = field(0x60) + buffer(current);
                const uint32_t after = word_at(field(0x18));
                const int32_t end = after ? static_cast<int32_t>(field(0x60) + after)
                                          : static_cast<int32_t>(field(0x70) - word_of(player, 0x50 + current * 4));
                if (end > static_cast<int32_t>(field(0x44))) {
                    // A block across the buffer's end: its head moves to the other buffer.
                    const int32_t rest = static_cast<int32_t>(field(0x60)) % static_cast<int32_t>(field(0x3c));
                    std::memcpy(pointer<void>(buffer(current ^ 1) + rest), pointer<void>(field(0x60) + buffer(current)),
                                field(0x3c) - rest);
                    player[0x78] ^= 1;
                    field(0x60) = static_cast<uint32_t>(rest);
                    field(0x18) = buffer(player[0x78]) + rest;
                }
            }
        } else {
            if (!(word_of(pointer<uint8_t>(field(4)), 4) & 2)) {
                real(0x10) = dt + real(0x10);
                return;
            }
            // Looping: read the file's data from the start again.
            if (field(0x5c) && field(0x6c) != 0xffffffff) {
                title_function<void XB_CDECL(uint32_t)>(0x000ca0e0)(field(0x6c));  // File_AbortRequest
                if (file_status(field(0x6c)) == 4) {
                    do file_pump();
                    while (file_status(field(0x6c)) == 4);
                }
                const uint8_t other = player[0x78] ^ 1;
                player[0x79] = other;
                field(0x5c) = 0;
                word_of(player, 0x50 + other * 4) = 0;
                field(0x68) = field(0x44);
                field(0x6c) = file_queue(field(0x28), field(0x58), buffer(player[0x79]), field(0x44));
                if (file_status(field(0x6c)) == 4) {
                    do file_pump();
                    while (file_status(field(0x6c)) == 4);
                }
            }
            player[0x78] ^= 1;
            field(0x60) = word_of(pointer<uint8_t>(field(4)), 0x34);
            field(0x18) = buffer(player[0x78]) + field(0x60);
        }
        // The new block's clips fixed up and bound in place of the old ones.
        uint8_t* block = pointer<uint8_t>(field(0x18));
        if (word_of(block, 8)) {
            word_of(block, 8) += address_of(block);
            for (uint8_t* clip = pointer<uint8_t>(word_of(pointer<uint8_t>(field(0x18)), 8)); clip;
                 clip = pointer<uint8_t>(word_of(clip, 4))) {
                fix_link(clip, 4);
                const uint32_t skeleton = pointer<uint32_t>(field(0x38))[word_of(clip, 0x28)];
                word_of(clip, 0x30) = skeleton;
                word_of(clip, 0) = class_vtable(skeleton, 0x28);
                object_call(clip, 3);
                object_call(clip, 1);
            }
        }
        uint32_t clip = word_at(field(0x18) + 8);
        for (uint32_t* node = pointer<uint32_t>(field(8)); node; node = pointer<uint32_t>(node[3])) {
            if (const uint32_t track = node[2]) {
                const uint32_t old = word_at(track + 0x10);
                title_function<void XB_THISCALL(uint32_t, uint32_t)>(word_at(word_at(track)))(track, 1);
                method_call(old, 2);
            }
            node[1] = clip;
            node[2] = 0;
            clip = NAL_PtrField4(pointer<uint32_t>(clip));
        }
        if (!(dt > title_global<float>(0x0083eafc))) return;
    }
    real(0x10) = dt + real(0x10);
    real(0x14) = dt + real(0x14);
    for (uint32_t* node = pointer<uint32_t>(field(8)); node; node = pointer<uint32_t>(node[3])) {
        if (!node[0]) continue;
        ensure_track(node);
        const float inverse = title_global<float>(0x0083eb20) / duration;
        sink_apply(node, inverse * real(0x14), inverse * previous, real(0x10), start);
    }
}

}  // namespace game
