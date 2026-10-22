#include "nal_anim.h"

#include <cstring>

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

}  // namespace game
