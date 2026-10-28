#include "nal_codec.h"

#include <cstring>

#include "lanes.h"
#include "nal_entropy.h"
#include "nal_math.h"
#include "title_data.h"

namespace game {

void XB_THISCALL NAL_RleReader_ReadBytes(RleReader* reader, uint8_t* dst, int32_t stride, uint32_t count) {
    for (; count; --count) {
        if (!reader->run) {
            // The title stores the pointer after each byte it reads (the stream may overlap the reader).
            const uint8_t* ptr = reader->ptr;
            const uint8_t run = *ptr++;
            reader->ptr = ptr;
            reader->run = run;
            reader->value = *ptr++;
            reader->ptr = ptr;
        }
        *dst = reader->value;
        dst += stride;
        --reader->run;
    }
}

void XB_THISCALL NAL_EntropyReader_Decode(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count) {
    if (reader->zeros) {
        uint32_t zeros = reader->zeros;
        if (count < zeros) {
            reader->zeros = static_cast<uint16_t>(zeros - count);
            zeros = count;
            count = 0;
        } else {
            count -= zeros;
            reader->zeros = 0;
        }
        for (; zeros; --zeros) {
            *dst = 0;
            dst = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(dst) + stride);
        }
    }
    if (count) {
        reader->zeros = static_cast<uint16_t>(kEntropyModeDecoders[reader->mode](reader, dst, stride, count));
    }
}

namespace {

// Copies `bytes` from *src to *dst and advances both cursors.
uint32_t copy_advance(uint8_t** dst, uint8_t** src, uint32_t bytes) {
    platform_rep_movs(*dst, *src, bytes);  // the title's inline REP MOVSD/MOVSB
    *dst += bytes;
    *src += bytes;
    return bytes;
}
uint8_t* align4(uint8_t* p) {
    return reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(p) + 3) & ~static_cast<uintptr_t>(3));
}
uint32_t channel_count(const uint8_t* group) { return *reinterpret_cast<const uint32_t*>(group + 0x28); }

}  // namespace

namespace {

bool channel_active(const uint32_t* context, int32_t i) {
    const uint8_t* clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(context[0]));
    const uint8_t* group = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(context[1]));
    const int32_t index = *reinterpret_cast<const int32_t*>(group + 0x24) + i;
    const uint32_t* active = *reinterpret_cast<uint32_t* const*>(clip + 0x54);
    return (active[index / 32] & (1u << (index & 31))) != 0;
}
const uint8_t* context_group(const uint32_t* context) {
    return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(context[1]));
}

// Steps over one cached frame: `size` bytes per active channel.
uint32_t skip_cached_frame(const uint32_t* context, uint8_t** cursor, uint32_t size) {
    *cursor = align4(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) *cursor += size;
    return context[1];
}

}  // namespace

uint32_t XB_CDECL NAL_Int1_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    return copy_advance(dst, src, channel_count(group));
}

uint32_t XB_CDECL NAL_Float1_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    *dst = align4(*dst);
    *src = align4(*src);
    return copy_advance(dst, src, channel_count(group) * 4);
}

uint32_t XB_CDECL NAL_Float3_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    *dst = align4(*dst);
    *src = align4(*src);
    return copy_advance(dst, src, channel_count(group) * 12);
}

void XB_CDECL NAL_SignalCounter_SkipStatic(const uint32_t* context) {
    uint32_t* stream = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(context[3]));
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) ++*stream;
}


uint32_t XB_STDCALL NAL_Float3_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    return skip_cached_frame(context, cursor, 12);
}

uint32_t XB_STDCALL NAL_Float4_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    return skip_cached_frame(context, cursor, 16);
}

uint32_t XB_CDECL NAL_Entropy_SkipStatic(const uint32_t* context) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    uint8_t** stream_b = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[3]));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) *stream_a += 4;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream_a));  // read before the stores
}

uint32_t XB_STDCALL NAL_EntropyFloat1_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    return skip_cached_frame(context, cursor, 4);
}

uint32_t XB_CDECL NAL_Packed8Float1_SkipStatic(const uint32_t* context) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) *stream_a += 8;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream_a));
}

namespace {

uint32_t skip_block(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames, int32_t size) {
    *cursor = align4(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream) + frames * size;
        *cursor += size;
    }
    return context[1];
}

template <typename T>
void dequantize3(float* out, const T* in, const float* record) {
    // every input read before the first store, as the original does (out may overlap in)
    const float scale = record[2], bias = record[1];
    const float x = static_cast<float>(in[0]), y = static_cast<float>(in[1]), z = static_cast<float>(in[2]);
    out[0] = scale * x + bias;
    out[1] = y * scale + bias;
    out[2] = z * scale + bias;
}

}  // namespace

uint32_t XB_STDCALL NAL_Float3_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames) {
    return skip_block(context, cursor, stream, frames, 12);
}

uint32_t XB_STDCALL NAL_Float4_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames) {
    return skip_block(context, cursor, stream, frames, 16);
}

void XB_CDECL NAL_Packed8_DequantizeFloat3(float* out, const int8_t* in, uint32_t, uint32_t, const float* record) {
    dequantize3(out, in, record);
}

void XB_CDECL NAL_Packed16_DequantizeFloat3(float* out, const int16_t* in, uint32_t, uint32_t, const float* record) {
    dequantize3(out, in, record);
}

uint32_t XB_STDCALL NAL_RLE8Int1_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t) {
    *readers = align4(*readers);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        const uint32_t length = *reinterpret_cast<const uint32_t*>(*stream);
        *stream += 4;
        if (RleReader* reader = reinterpret_cast<RleReader*>(*readers)) {
            reader->ptr = *stream;
            reader->run = 0;
        }
        *stream += length;
        *readers += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_RLE8Int1_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t, uint32_t,
                                              uint32_t count, int32_t stride) {
    *readers = align4(*readers);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        NAL_RleReader_ReadBytes(reinterpret_cast<RleReader*>(*readers), *dst, stride, count);
        *readers += 8;
        ++*dst;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_RLE8Int1_SkipBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, uint32_t) {
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        *stream += *reinterpret_cast<const uint32_t*>(*stream) + 4;
        ++*dst;
    }
    return context[1];
}

uint8_t** XB_CDECL NAL_EntropyFloat3_InitReaders(uint8_t* readers, uint8_t** stream) {
    // Three component streams, each a length byte and its bits; readers 0x10 apart.
    for (uint32_t i = 0; i < 3; ++i) {
        // The length byte is read before the stream pointer moves past it (they may overlap).
        uint8_t* const at = *stream;
        const uint8_t length = *at;
        *stream = at + 1;
        EntropyReader* reader = reinterpret_cast<EntropyReader*>(readers + i * 0x10);
        if (reader) {
            reader->ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
            reader->bit = 0;
            reader->mode = 0xff;
            reader->zeros = 0;
        }
        *stream += length;
    }
    return stream;
}

uint32_t XB_STDCALL NAL_Packed8Float1_SkipBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, int32_t bytes) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            *stream = align4(*stream) + bytes;
            ++*dst;
        }
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed8Float1_DecodeBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, int32_t frames,
                                                  int32_t stride) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            *stream = align4(*stream);
            uint8_t* out = *dst;
            for (int32_t f = frames; f > 0; --f) {
                *out = **stream;
                ++*stream;
                out += stride;
            }
            ++*dst;
        }
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_SignalCounter_DecodeBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, uint32_t count,
                                                  int32_t stride) {
    uint32_t* stream_b = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(context[3]));
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        const uint32_t length = *reinterpret_cast<const uint32_t*>(*stream);
        *stream += 4;
        RleReader reader;
        reader.ptr = *stream;
        reader.run = 0;
        reader.value = 0;
        NAL_RleReader_ReadBytes(&reader, *dst, stride, count);
        *stream += length;
        ++*dst;
        ++*stream_b;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_SignalCounter_SkipBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, uint32_t) {
    uint32_t* stream_b = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(context[3]));
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        *stream += *reinterpret_cast<const uint32_t*>(*stream) + 4;
        ++*dst;
        ++*stream_b;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat1_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                               const int32_t* offsets) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    uint8_t** stream_b = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[3]));
    *cursor = align4(*cursor);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    const uint8_t* group = context_group(context);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) {
        const int32_t offset = offsets[*reinterpret_cast<const int32_t*>(group + 0x24) + i];
        const bool active = channel_active(context, i);
        if (offset >= 0) {
            // The decoded value, or the default for channels without data.
            std::memcpy(pose + offset, active ? *cursor : defaults + offset, 4);
        }
        if (active) *cursor += 4;
        *stream_a += 4;
    }
    return context[1];
}

namespace {

// The entropy stream as a bit position (LSB first in little-endian words; the reader keeps
// the byte address and the bit in that byte).
struct BitStream {
    uint64_t pos;  // the byte address * 8 overflows 32 bits
    explicit BitStream(const EntropyReader* r) : pos(static_cast<uint64_t>(r->ptr) * 8 + r->bit) {}
    uint32_t peek(uint32_t n) const {
        uint32_t value = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const uint8_t byte = *reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>((pos + k) >> 3));
            value |= ((byte >> ((pos + k) & 7)) & 1u) << k;
        }
        return value;
    }
    uint32_t read(uint32_t n) {
        const uint32_t value = peek(n);
        pos += n;
        return value;
    }
    void store(EntropyReader* r) const {
        r->ptr = static_cast<uint32_t>(pos >> 3);
        r->bit = static_cast<uint8_t>(pos & 7);
    }
};

// A 32-bit raw value (flag 1) or a 16-bit one biased by 0x8000.
int32_t read_value(BitStream& bits) {
    if (bits.read(1)) return static_cast<int32_t>(bits.read(32));
    return static_cast<int32_t>(bits.read(16)) - 0x8000;
}

}  // namespace

void XB_THISCALL NAL_EntropyReader_DecodeFloats(EntropyReader* reader, float* dst, int32_t stride, uint32_t count, float scale) {
    if (!count) return;
    int32_t* prev = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(reader) + 8);  // v[n-2], v[n-1]
    const auto advance = [stride](float* p) {
        return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(p) + stride);
    };
    if (reader->mode == 0xff) {
        // Header: the residual mode (5 bits; + 0x20 for a negative scale), the first value
        // and the first delta.
        BitStream bits(reader);
        reader->mode = static_cast<uint8_t>(bits.read(5));
        if (0.0f > scale) {
            reader->mode = static_cast<uint8_t>(reader->mode + 0x20);
            scale = 0.0f - scale;
        }
        prev[0] = read_value(bits);
        int32_t delta;
        if (reader->mode & 0x20) {
            delta = read_value(bits);
        } else {
            const int32_t code = static_cast<int32_t>(bits.peek(12));
            if (!(code & 0x1f)) {
                int32_t m = code >> 9;
                if (!(m & 4)) m -= 7;
                delta = m << (((code >> 5) & 0xf) + 0x11);
                bits.pos += 12;
            } else if (code & 1) {
                int32_t m = (code >> 5) & 7;
                if (!(m & 4)) m -= 7;
                delta = m << (((code >> 1) & 0xf) + 1);
                bits.pos += 8;
            } else {
                delta = ((code >> 1) & 0xf) - 8;
                bits.pos += 5;
            }
        }
        prev[1] = static_cast<int32_t>(static_cast<uint32_t>(prev[0]) + static_cast<uint32_t>(delta));
        bits.store(reader);
        *dst = static_cast<float>(prev[0]) * scale;
        if (count == 1) {
            reader->mode |= 0x80;  // the second value is still to come
            return;
        }
        dst = advance(dst);
        *dst = static_cast<float>(prev[1]) * scale;
        dst = advance(dst);
        count -= 2;
        if (!count) return;
    } else {
        if (0.0f > scale) scale = 0.0f - scale;
        if (reader->mode & 0x80) {
            *dst = static_cast<float>(prev[1]) * scale;
            dst = advance(dst);
            --count;
            reader->mode &= 0x7f;
            if (!count) return;
        }
    }
    // The residuals, then v[n] = 2 v[n-1] - v[n-2] + residual.
    NAL_EntropyReader_Decode(reader, reinterpret_cast<uint32_t*>(dst), stride, count);
    int32_t a = prev[0], b = prev[1];
    for (; count; --count) {
        const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(b) + static_cast<uint32_t>(b) - static_cast<uint32_t>(a) +
                                               *reinterpret_cast<const uint32_t*>(dst));
        *dst = static_cast<float>(v) * scale;
        dst = advance(dst);
        a = b;
        b = v;
    }
    prev[0] = a;
    prev[1] = b;
}

void XB_CDECL NAL_EntropyFloat3_DecodeChannelBlock(float* dst, uint8_t** stream, uint32_t count, int32_t stride, uint32_t,
                                                   const float* block_scale, const float* channel_scale, uint32_t) {
    // x, y and z each from its own length-prefixed stream with a fresh reader.
    for (uint32_t c = 0; c < 3; ++c) {
        const uint8_t length = **stream;
        ++*stream;
        EntropyReader reader;
        reader.ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
        reader.bit = 0;
        reader.mode = 0xff;
        reader.zeros = 0;
        NAL_EntropyReader_DecodeFloats(&reader, dst + c, stride, count, *block_scale * *channel_scale);
        *stream += length;
    }
}

uint32_t XB_STDCALL NAL_EntropyFloat3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                  int32_t frame_size) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    uint8_t** stream_b = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[3]));
    *data = align4(*data);
    const uint8_t* block_scale = align4(*stream_b);  // one float for the block
    *stream_b = const_cast<uint8_t*>(block_scale);
    const uint32_t first_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_b += 4;
    *stream_a = align4(*stream_a);  // one float per channel
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_EntropyFloat3_DecodeChannelBlock(reinterpret_cast<float*>(*data), stream, frames, frame_size, first_a,
                                                 reinterpret_cast<const float*>(block_scale),
                                                 reinterpret_cast<const float*>(*stream_a),
                                                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b)));
            *data += 12;
        }
        *stream_a += 4;
    }
    return context[1];
}

namespace {

uint8_t* align16(uint8_t* p) {
    return reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(p) + 15) & ~static_cast<uintptr_t>(15));
}
uint8_t** context_stream(const uint32_t* context, int32_t i) {
    return reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[i]));
}
int32_t channel_index(const uint32_t* context, int32_t i) {
    return *reinterpret_cast<const int32_t*>(context_group(context) + 0x24) + i;
}

}  // namespace

uint32_t XB_CDECL NAL_PosOri_TypeId() { return 0x00909824; }

uint32_t XB_CDECL NAL_PosOri_ValueSize() { return 0x20; }

void XB_STDCALL NAL_PosOri_Blend(int32_t count, float* dst, const float* a, const float* b, float t) {
    const float one_minus = title_global<float>(0x0083eb20) - t;
    platform_keep(one_minus);  // computed before the count test (MXCSR flags)
    for (; count > 0; --count) {
        dst[4] = a[4] * one_minus + b[4] * t;
        dst[5] = a[5] * one_minus + b[5] * t;
        dst[6] = a[6] * one_minus + b[6] * t;
        alignas(16) float q[4];
        Quat_Slerp(q, a, b, t);
        std::memcpy(dst, q, 16);
        dst += 8;
        a += 8;
        b += 8;
    }
}

void XB_STDCALL NAL_PosOri_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights) {
    for (; count > 0; --count) {
        const float* const weight = *weights;  // read before the pointer moves on (they may overlap)
        const float t = *weight;
        *weights = weight + 1;
        const float one_minus = title_global<float>(0x0083eb20) - t;
        dst[4] = a[4] * one_minus + b[4] * t;
        dst[5] = a[5] * one_minus + b[5] * t;
        dst[6] = a[6] * one_minus + b[6] * t;
        alignas(16) float q[4];
        Quat_Slerp(q, a, b, t);
        std::memcpy(dst, q, 16);
        dst += 8;
        a += 8;
        b += 8;
    }
}

uint8_t** XB_STDCALL NAL_PosOri_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0x20;
    return cursor;
}

uint32_t XB_STDCALL NAL_PosOri_InitState(const uint8_t* group, uint8_t** cursor) {
    uint8_t* start = align16(*cursor);
    *cursor = start + channel_count(group) * 0x20;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start));
}

uint8_t** XB_STDCALL NAL_PosOri_FreeState(const uint8_t* group, uint8_t** cursor) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0x20;
    return cursor;
}

uint32_t XB_CDECL NAL_PosOri_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    *dst = align16(*dst);
    *src = align16(*src);
    return copy_advance(dst, src, channel_count(group) * 0x20);
}

uint32_t XB_STDCALL NAL_PosOri_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) *cursor += 0x20;
    return context[1];
}

uint8_t** XB_STDCALL NAL_Cursor_Align4(uint8_t** cursor) {
    *cursor = align4(*cursor);
    return cursor;
}

uint8_t** XB_STDCALL NAL_Cursor_Skip8(uint8_t** cursor) {
    *cursor += 8;
    return cursor;
}

uint8_t** XB_STDCALL NAL_Cursor_Skip4(uint8_t** cursor) {
    *cursor += 4;
    return cursor;
}

uint8_t** XB_STDCALL NAL_Cursor_Align16(uint8_t** cursor) {
    *cursor = align16(*cursor);
    return cursor;
}

uint8_t** XB_STDCALL NAL_Cursor_Skip32(uint8_t** cursor) {
    *cursor += 0x20;
    return cursor;
}

uint8_t** XB_CDECL NAL_SkipPrefixedStreams4(uint8_t** stream) {
    for (int32_t i = 0; i < 4; ++i) *stream += **stream + 1;
    return stream;
}

uint32_t XB_STDCALL NAL_EntropyTrajPosOri_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = align16(*cursor);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_SkipPrefixedStreams4(stream);
            *cursor += 0x20;
            *stream_b += 0x20;
        }
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyTrajPosOri_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                                   const uint8_t* defaults, const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = align16(*cursor);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) std::memcpy(pose + offset, active ? *cursor : defaults + offset, 0x20);
        if (active) {
            *cursor += 0x20;
            *stream_b += 0x20;
        }
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyTrajPosOri_ApplyStatic(const uint32_t* context, uint8_t* pose, bool skip, const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0 && active && !skip) std::memcpy(pose + offset, *stream_b, 0x20);
        if (active) *stream_b += 0x20;
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_CDECL NAL_EntropyTrajPosOri_ApplyWraps(const uint32_t* context, uint8_t* dst, uint8_t* current, int32_t wraps,
                                                   bool skip, const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    uint8_t* word = align4(*stream_b);
    *stream_b = word + 4;
    const uint32_t first_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0 && active) {
            NAL_Trajectory_AccumulateRoot(reinterpret_cast<float*>(dst + offset), reinterpret_cast<const float*>(current + offset),
                                          wraps, first_a, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(word)),
                                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a)),
                                          reinterpret_cast<const float*>(*stream_b), skip);
        }
        if (active) *stream_b += 0x20;
        *stream_a += 8;
    }
    return context[1];
}

void XB_CDECL NAL_Trajectory_AccumulateRoot(float* dst, const float* current, int32_t wraps, uint32_t, uint32_t, uint32_t,
                                            const float* loop, bool skip) {
    if (skip) return;
    alignas(16) float inverse[8] = {};
    alignas(16) float moved[8] = {};
    alignas(16) float result[8] = {};
    QuatTrans_Inverse(current, inverse);
    if (wraps == 0) {
        QuatTrans_Concat(result, dst, inverse);
    } else if (wraps == 1) {
        QuatTrans_Concat(moved, dst, loop);
        QuatTrans_Concat(result, moved, inverse);
    } else {
        alignas(16) float power[8] = {};
        if (wraps > 0) {
            QuatTrans_Power(power, loop, wraps);
        } else {
            alignas(16) float forward[8] = {};
            QuatTrans_Power(forward, loop, -wraps);
            QuatTrans_Inverse(forward, power);
        }
        QuatTrans_Concat(moved, dst, power);
        QuatTrans_Concat(result, moved, inverse);
    }
    std::memcpy(dst, result, 0x20);
}

void XB_CDECL NAL_Trajectory_AccumulateRoot2(float* dst, const float* current, int32_t wraps, uint32_t a, uint32_t b, uint32_t c,
                                             const float* loop, bool skip) {
    NAL_Trajectory_AccumulateRoot(dst, current, wraps, a, b, c, loop, skip);
}

uint32_t XB_CDECL NAL_TrajPosOri_ApplyWraps(const uint32_t* context, uint8_t* dst, uint8_t* current, int32_t wraps, bool skip,
                                            const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    const uint32_t first_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    const uint32_t first_b = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b));
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0 && active) {
            NAL_Trajectory_AccumulateRoot2(reinterpret_cast<float*>(dst + offset), reinterpret_cast<const float*>(current + offset),
                                           wraps, first_a, first_b, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a)),
                                           reinterpret_cast<const float*>(*stream_b), skip);
        }
        if (active) *stream_b += 0x20;
    }
    return context[1];
}

uint32_t XB_CDECL NAL_Float3_TypeId() { return 0x00909820; }

uint32_t XB_CDECL NAL_Float3_ValueSize() { return 0xc; }

uint32_t XB_CDECL NAL_Const4() { return 4; }

uint32_t XB_CDECL NAL_Quat_TypeId() { return 0x00909826; }

uint32_t XB_CDECL NAL_Const16() { return 0x10; }

uint8_t** XB_STDCALL NAL_Float3_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t) {
    *cursor = align4(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0xc;
    return cursor;
}

uint32_t XB_STDCALL NAL_Float3_InitState(const uint8_t* group, uint8_t** cursor) {
    uint8_t* end = align4(*cursor) + channel_count(group) * 0xc;
    *cursor = end;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end));
}

uint8_t** XB_STDCALL NAL_Float3_FreeState(const uint8_t* group, uint8_t** cursor) {
    *cursor = align4(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0xc;
    return cursor;
}

uint32_t XB_STDCALL NAL_Packed16Quat_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) *cursor += 8;
    return context[1];
}

uint8_t** XB_STDCALL NAL_Quat_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0x10;
    return cursor;
}

uint32_t XB_STDCALL NAL_Entropy_SkipStatic4(const uint32_t* context, uint8_t*, uint32_t, const int32_t*) {
    return NAL_Entropy_SkipStatic(context);
}

uint32_t XB_STDCALL NAL_Quat_InitState(const uint8_t* group, uint8_t** cursor) {
    uint8_t* start = align16(*cursor);
    *cursor = start + channel_count(group) * 0x10;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start));
}

uint8_t** XB_STDCALL NAL_Quat_FreeState(const uint8_t* group, uint8_t** cursor) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0x10;
    return cursor;
}

uint32_t XB_CDECL NAL_Quat_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    *dst = align16(*dst);
    *src = align16(*src);
    return copy_advance(dst, src, channel_count(group) * 0x10);
}

uint32_t XB_STDCALL NAL_Packed16Quat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t) {
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            *stream += **stream + 1;
            *cursor += 8;
        }
        *stream_a += 4;
    }
    return context[1];
}

void XB_STDCALL NAL_Quat_Blend(int32_t count, float* dst, const float* a, const float* b, float t) {
    for (; count > 0; --count) {
        alignas(16) float q[4];
        std::memcpy(dst, Quat_Slerp(q, a, b, t), 16);
        dst += 4;
        a += 4;
        b += 4;
    }
}

void XB_STDCALL NAL_Quat_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights) {
    for (; count > 0; --count) {
        const float* const weight = *weights;  // read before the pointer moves on (they may overlap)
        const float t = *weight;
        *weights = weight + 1;
        alignas(16) float q[4];
        std::memcpy(dst, Quat_Slerp(q, a, b, t), 16);
        dst += 4;
        a += 4;
        b += 4;
    }
}

void XB_STDCALL NAL_Float3_Blend(int32_t count, float* dst, const float* a, const float* b, float t) {
    const float one_minus = title_global<float>(0x0083eb20) - t;
    platform_keep(one_minus);  // computed before the count test (MXCSR flags)
    for (; count > 0; --count) {
        dst[0] = a[0] * one_minus + b[0] * t;
        dst[1] = a[1] * one_minus + b[1] * t;
        dst[2] = a[2] * one_minus + b[2] * t;
        dst += 3;
        a += 3;
        b += 3;
    }
}

void XB_STDCALL NAL_Float3_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights) {
    for (; count > 0; --count) {
        const float t = **weights;
        const float one_minus = title_global<float>(0x0083eb20) - t;
        dst[0] = a[0] * one_minus + b[0] * t;
        dst[1] = a[1] * one_minus + b[1] * t;
        dst[2] = a[2] * one_minus + b[2] * t;
        ++*weights;
        dst += 3;
        a += 3;
        b += 3;
    }
}

namespace {

// x * y with x as the destination operand (a NaN there wins over one in y).
float mul_first(float x, float y) {
    float p = x;
    p *= y;
    return p;
}

// One four-float value of the Float4 blends: dst = a * u + b * t per lane, the eight inputs
// read before the stores. The title's unrolled copies put the operands in different
// orders, which only shows in NaN payloads: `u_first` lanes compute u * a (else a * u),
// `t_first` lanes t * b (else b * t).
void blend_value(float* dst, const float* a, const float* b, float u, float t, uint32_t u_first, uint32_t t_first) {
    const float x[4] = {a[0], a[1], a[2], a[3]};
    const float y[4] = {b[0], b[1], b[2], b[3]};
    float r[4];
    for (uint32_t k = 0; k < 4; ++k) {
        const float p = (u_first >> k) & 1 ? mul_first(u, x[k]) : mul_first(x[k], u);
        const float q = (t_first >> k) & 1 ? mul_first(t, y[k]) : mul_first(y[k], t);
        float sum = p;
        sum += q;
        r[k] = sum;
    }
    std::memcpy(dst, r, 16);
}

}  // namespace

void XB_STDCALL NAL_Float4_Blend(int32_t count, float* dst, const float* a, const float* b, float t) {
    // The Float3 blend over four lanes, unrolled four values at a time as the title does.
    const float one_minus = title_global<float>(0x0083eb20) - t;
    platform_keep(one_minus);  // computed before the count test (MXCSR flags)
    static const uint8_t kUFirst[4] = {1, 8, 0, 0};
    static const uint8_t kTFirst[4] = {0, 4, 0, 0};
    int32_t done = 0;
    if (count >= 4) {
        const uint32_t groups = (static_cast<uint32_t>(count - 4) >> 2) + 1;
        for (uint32_t g = 0; g < groups; ++g)
            for (uint32_t v = 0; v < 4; ++v, dst += 4, a += 4, b += 4)
                blend_value(dst, a, b, one_minus, t, kUFirst[v], kTFirst[v]);
        done = static_cast<int32_t>(groups * 4);
    }
    for (int32_t left = count - done; left > 0; --left, dst += 4, a += 4, b += 4)
        blend_value(dst, a, b, one_minus, t, 1, 0);
}

void XB_STDCALL NAL_Float4_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights) {
    // As NAL_Float4_Blend with t from **weights (*weights advanced per value).
    const auto value = [&](uint32_t u_first) {
        const float t = **weights;
        const float x[4] = {a[0], a[1], a[2], a[3]};
        const float y[4] = {b[0], b[1], b[2], b[3]};
        const float one_minus = title_global<float>(0x0083eb20) - **weights;
        blend_value(dst, x, y, one_minus, t, u_first, 0);
        ++*weights;
        dst += 4;
        a += 4;
        b += 4;
    };
    int32_t done = 0;
    if (count >= 4) {
        const uint32_t groups = (static_cast<uint32_t>(count - 4) >> 2) + 1;
        for (uint32_t g = 0; g < groups; ++g)
            for (uint32_t v = 0; v < 4; ++v) value(v == 0 ? 1 : 0);
        done = static_cast<int32_t>(groups * 4);
    }
    for (int32_t left = count - done; left > 0; --left) value(1);
}

uint32_t XB_STDCALL NAL_Float3_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, int32_t frames) {
    *readers = align4(*readers);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        if (*readers) *reinterpret_cast<uint8_t**>(*readers) = *stream;
        *stream += frames * 0xc;
        *readers += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Float3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t, uint32_t,
                                            int32_t count, int32_t stride) {
    *readers = align4(*readers);
    *dst = align4(*dst);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        uint8_t* out = *dst;
        uint8_t** reader = reinterpret_cast<uint8_t**>(*readers);
        for (int32_t n = count; n > 0; --n) {
            std::memcpy(out, *reader, 0xc);
            *reader += 0xc;
            out += stride;
        }
        *readers += 4;
        *dst += 0xc;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Float3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, int32_t frames,
                                           int32_t stride) {
    *data = align4(*data);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        uint8_t* out = *data;
        for (int32_t n = frames; n > 0; --n) {
            std::memcpy(out, *stream, 0xc);
            *stream += 0xc;
            out += stride;
        }
        *data += 0xc;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Float3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                        const int32_t* offsets) {
    *cursor = align4(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) std::memcpy(pose + offset, active ? *cursor : defaults + offset, 0xc);
        if (active) *cursor += 0xc;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat3_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_EntropyFloat3_InitReaders(*readers, stream);
            *readers += 0x30;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                   uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    *dst = align4(*dst);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* channel_scale = reinterpret_cast<const float*>(*stream_a);
            float* out = reinterpret_cast<float*>(*dst);
            EntropyReader* reader = reinterpret_cast<EntropyReader*>(*readers);
            for (int32_t c = 0; c < 3; ++c)
                NAL_EntropyReader_DecodeFloats(reader + c, out + c, stride, count, *block_scale * *channel_scale);
            *readers += 0x30;
            *dst += 0xc;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat3_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = align4(*cursor);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            for (int32_t c = 0; c < 3; ++c) *stream += **stream + 1;
            *cursor += 0xc;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                               const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = align4(*cursor);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) std::memcpy(pose + offset, active ? *cursor : defaults + offset, 0xc);
        if (active) *cursor += 0xc;
        *stream_a += 4;
    }
    return context[1];
}

namespace {

// A signed value: 2 bits pick its width (table 0x008bb490: 3, 5, 8 or 21 bits), the low bit
// of the value is the sign.
int32_t read_folded(BitStream& bits) {
    const uint32_t width = title_global<uint32_t>(0x008bb490 + 4 * bits.read(2));
    const int32_t v = static_cast<int32_t>(bits.read(width));
    return (v & 1) ? -(v >> 1) : v >> 1;
}

// The first delta for a positive scale: a 12/7/5-bit code (low bits pick the form).
int32_t read_delta_code(BitStream& bits) {
    const int32_t code = static_cast<int32_t>(bits.peek(12));
    if (!(code & 0x1f)) {
        int32_t m = code >> 9;
        if (!(m & 4)) m -= 7;
        bits.pos += 12;
        return m << (((code >> 5) & 0xf) + 9);
    }
    if (code & 1) {
        int32_t m = (code >> 4) & 7;
        if (!(m & 4)) m -= 7;
        bits.pos += 7;
        return m << (((code >> 1) & 7) + 1);
    }
    bits.pos += 5;
    return ((code >> 1) & 0xf) - 8;
}

}  // namespace

QuatReader* XB_THISCALL NAL_QuatReader_Init(QuatReader* q, uint32_t count, float scale, uint32_t ptr, uint8_t bit) {
    for (int32_t c = 0; c < 3; ++c) {
        q->readers[c].mode = 0xff;
        q->readers[c].zeros = 0;
    }
    q->readers[0].ptr = ptr;
    q->readers[0].bit = bit;
    float v[3];
    for (int32_t c = 0; c < 3; ++c) {
        QuatReader::Reader& r = q->readers[c];
        BitStream bits(reinterpret_cast<const EntropyReader*>(&r));
        uint32_t length = 0;  // the component stream's length in bits, 0 = not stored
        if (c < 2 && bits.read(1)) length = bits.read(11) + 0x100;
        r.mode = static_cast<uint8_t>(bits.read(5));
        float magnitude = scale;
        if (0.0f > scale) {
            r.mode = static_cast<uint8_t>(r.mode + 0x20);
            magnitude = 0.0f - scale;
        }
        const int32_t first = read_folded(bits);
        v[c] = magnitude * title_global<float>(0x0083ec24) * static_cast<float>(first);
        q->deltas[c] = (0.0f > scale) ? read_folded(bits) : read_delta_code(bits);
        bits.store(reinterpret_cast<EntropyReader*>(&r));
        if (c == 2) break;
        QuatReader::Reader& next = q->readers[c + 1];
        if (length) {
            BitStream end = bits;
            end.pos += length;
            end.store(reinterpret_cast<EntropyReader*>(&next));
        } else {
            // No length: decode the residuals once to find where the next stream starts.
            const QuatReader::Reader start = r;
            if (count > 2) {
                uint32_t scratch;
                NAL_EntropyReader_Decode(reinterpret_cast<EntropyReader*>(&r), &scratch, 0, count - 2);
            }
            next.ptr = r.ptr;
            next.bit = r.bit;
            r.ptr = start.ptr;
            r.bit = start.bit;
            r.zeros = 0;
        }
    }
    // w from the unit length: sqrt(|1 - (x^2 + y^2 + z^2)|) by the rsqrt estimate and one
    // Newton step (0 and NaN stay as they are).
    const Lanes squares = mul(Lanes{{v[0], v[1], v[2], 0.0f}}, Lanes{{v[0], v[1], v[2], 0.0f}});
    platform_keep(squares);
    const float a = abs_bits(title_global<float>(0x007c2190) - ((squares.v[0] + squares.v[1]) + squares.v[2]));
    float w = a;
    if (!(a == 0.0f || a != a)) {
        const float r = platform_rsqrt_estimate(a);
        const float t = a * r;
        w = ((title_global<float>(0x007c2140) - t * r) * t) * title_global<float>(0x007c2150);
    }
    q->value[0] = v[0];
    q->value[1] = v[1];
    q->value[2] = v[2];
    q->value[3] = w;
    q->readers[0].mode |= 0x80;
    return q;
}

int16_t* XB_THISCALL Quat_ToShort4(int16_t* out, const float* q) {
    const Lanes v = mul(lanes(q), title_global<float>(0x0083f258));
    for (int32_t i = 0; i < 4; ++i) out[i] = static_cast<int16_t>(static_cast<int32_t>(v.v[i]));
    return out;
}

namespace {

// The rotation step of one frame: x, y, z = the accumulated deltas * scale, w rebuilt from the
// unit length as in NAL_QuatReader_Init. Lane 3 of the packed product is x again.
Lanes delta_quat(int32_t a, int32_t b, int32_t c, const Lanes& scale) {
    const float fa = static_cast<float>(a);
    const Lanes p = mul(Lanes{{fa, static_cast<float>(b), static_cast<float>(c), fa}}, scale);
    const Lanes squares = mul(p, p);
    platform_keep(squares);
    const float a2 = abs_bits(title_global<float>(0x007c2190) - ((squares.v[0] + squares.v[1]) + squares.v[2]));
    float w = a2;
    if (!(a2 == 0.0f || a2 != a2)) {
        const float r = platform_rsqrt_estimate(a2);
        const float t = a2 * r;
        w = ((title_global<float>(0x007c2140) - t * r) * t) * title_global<float>(0x007c2150);
    }
    return {{p.v[0], p.v[1], p.v[2], w}};
}

// q * m as the title's packed code computes it, lane for lane: m * q.w + q * m.w + q x m,
// minus the dot product times the constant 0x008375d0 (w only).
Lanes quat_product(const Lanes& q, const Lanes& m) {
    const Lanes qm = mul(q, m);
    const Lanes pairs = add(Lanes{{qm.v[2], qm.v[3], qm.v[0], qm.v[1]}}, qm);
    const Lanes x4 = mul(Lanes{{q.v[1], q.v[2], q.v[0], q.v[3]}}, m);
    const Lanes x5 = mul(q, Lanes{{m.v[1], m.v[2], m.v[0], m.v[3]}});
    const Lanes c = sub(x5, x4);
    Lanes dot = add(Lanes{{pairs.v[1], pairs.v[0], pairs.v[1], pairs.v[0]}}, pairs);
    dot = mul(dot, lanes(&title_global<float>(0x008375d0)));
    const Lanes cross{{c.v[1], c.v[2], c.v[0], c.v[3]}};
    const Lanes qw = mul(q, m.v[3]);
    Lanes r = mul(m, q.v[3]);
    r = add(r, qw);
    r = add(r, cross);
    return sub(r, dot);
}

void store_short4(uint8_t* out, const float* q) {
    int16_t packed[4];
    Quat_ToShort4(packed, q);
    std::memcpy(out, packed, 8);
}

}  // namespace

namespace {

// NAL_QuatReader_DecodeFrames and its byte form: `store` packs one quaternion at the output.
template <typename Store>
void decode_quat_frames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale, int32_t* deltas,
                        Store store) {
    if (!count) return;
    float s = scale;
    if (0.0f > s) s = 0.0f - s;
    const Lanes s4{{s, s, s, s}};
    // Frames still held by the reader (top bits of x's mode, set by NAL_QuatReader_Init): the
    // first value as it is, then one step with the first deltas.
    uint8_t& held = q->readers[0].mode;
    if (held & 0xc0) {
        for (;;) {
            if (held & 0x40) {
                const Lanes v = quat_product(delta_quat(q->deltas[0], q->deltas[1], q->deltas[2], s4), lanes(q->value));
                std::memcpy(q->value, v.v, 16);
            }
            store(out, q->value);
            out += stride;
            held = static_cast<uint8_t>(held + 0xc0);
            if (!--count) return;
            if (!(held & 0xc0)) break;
        }
    }
    for (int32_t c = 0; c < 3; ++c)
        NAL_EntropyReader_Decode(reinterpret_cast<EntropyReader*>(&q->readers[c]), reinterpret_cast<uint32_t*>(deltas + c),
                                 0xc, count);
    int32_t a = q->deltas[0];
    int32_t b = q->deltas[1];
    int32_t c = q->deltas[2];
    alignas(16) Lanes m = lanes(q->value);
    const int32_t* d = deltas;
    do {
        a += d[0];
        b += d[1];
        c += d[2];
        d += 3;
        m = quat_product(delta_quat(a, b, c, s4), m);
        store(out, m.v);
        out += stride;
    } while (--count);
    q->deltas[0] = a;
    q->deltas[1] = b;
    q->deltas[2] = c;
    std::memcpy(q->value, m.v, 16);
}

}  // namespace

void XB_THISCALL NAL_QuatReader_DecodeFrames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale,
                                             int32_t* deltas) {
    decode_quat_frames(q, out, stride, count, scale, deltas, store_short4);
}

uint8_t* XB_THISCALL Quat_ToByte3(uint8_t* out, const float* q) {
    Lanes v = lanes(q);
    if (!(q[3] >= title_global<float>(0x0083eafc))) v = neg(v);  // w >= 0 (the same rotation)
    v = mul(v, title_global<float>(0x0083ed88));
    for (int32_t i = 0; i < 3; ++i) out[i] = static_cast<uint8_t>(static_cast<int32_t>(v.v[i]));
    return out;
}

void XB_THISCALL NAL_QuatReader_DecodeByteFrames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale,
                                                 int32_t* deltas) {
    decode_quat_frames(q, out, stride, count, scale, deltas, [](uint8_t* dst, const float* value) {
        uint8_t packed[3];
        Quat_ToByte3(packed, value);
        std::memcpy(dst, packed, 3);
    });
}

namespace {

// q / |q| as the title's packed code: |q|^2 summed as (x^2 + z^2) + (y^2 + w^2), then the rsqrt
// estimate with one Newton step.
Lanes quat_normalize(const Lanes& q) {
    const Lanes squares = mul(q, q);
    const Lanes pairs = add(squares, Lanes{{squares.v[2], squares.v[3], squares.v[0], squares.v[0]}});
    platform_keep(pairs);
    const float n = pairs.v[0] + pairs.v[1];
    const float r = platform_rsqrt_estimate(n);
    const float s = ((title_global<float>(0x007c2140) - (r * r) * n) * r) * title_global<float>(0x007c2150);
    return mul(q, s);
}

}  // namespace

void XB_THISCALL NAL_QuatReader_DecodeFloatFrames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale) {
    if (!count) return;
    float s = scale;
    if (0.0f > s) s = 0.0f - s;
    const Lanes s4{{s, s, s, s}};
    uint8_t& held = q->readers[0].mode;
    if (held & 0xc0) {
        for (;;) {
            if (held & 0x40) {
                const Lanes v = quat_product(delta_quat(q->deltas[0], q->deltas[1], q->deltas[2], s4), lanes(q->value));
                std::memcpy(q->value, v.v, 16);
            }
            std::memcpy(out, q->value, 16);
            out += stride;
            held = static_cast<uint8_t>(held + 0xc0);
            if (!--count) return;
            if (!(held & 0xc0)) break;
        }
    }
    // The residuals go straight to the output; each frame reads its three, then overwrites them.
    for (int32_t c = 0; c < 3; ++c)
        NAL_EntropyReader_Decode(reinterpret_cast<EntropyReader*>(&q->readers[c]), reinterpret_cast<uint32_t*>(out + 4 * c),
                                 stride, count);
    int32_t a = q->deltas[0];
    int32_t b = q->deltas[1];
    int32_t c = q->deltas[2];
    Lanes m = lanes(q->value);
    do {
        int32_t residual[3];
        std::memcpy(residual, out, 12);
        a += residual[0];
        b += residual[1];
        c += residual[2];
        m = quat_normalize(quat_product(delta_quat(a, b, c, s4), m));
        std::memcpy(out, m.v, 16);
        out += stride;
    } while (--count);
    q->deltas[0] = a;
    q->deltas[1] = b;
    q->deltas[2] = c;
    std::memcpy(q->value, m.v, 16);
}

uint32_t XB_STDCALL NAL_Quat_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, int32_t frames) {
    *readers = align4(*readers);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        if (*readers) *reinterpret_cast<uint8_t**>(*readers) = *stream;
        *stream += frames * 0x10;
        *readers += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Quat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t, uint32_t,
                                          int32_t count, int32_t stride) {
    *readers = align4(*readers);
    *dst = align16(*dst);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        uint8_t* out = *dst;
        uint8_t** reader = reinterpret_cast<uint8_t**>(*readers);
        for (int32_t n = count; n > 0; --n) {
            std::memcpy(out, *reader, 0x10);
            *reader += 0x10;
            out += stride;
        }
        *readers += 4;
        *dst += 0x10;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Quat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, int32_t frames, int32_t stride) {
    *data = align16(*data);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream);
        uint8_t* out = *data;
        for (int32_t n = frames; n > 0; --n) {
            std::memcpy(out, *stream, 0x10);
            *stream += 0x10;
            out += stride;
        }
        *data += 0x10;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Quat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        *stream = align4(*stream) + frames * 0x10;
        *cursor += 0x10;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Quat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                      const int32_t* offsets) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) std::memcpy(pose + offset, active ? *cursor : defaults + offset, 0x10);
        if (active) *cursor += 0x10;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyQuat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t) {
    *cursor = align16(*cursor);
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            *stream += **stream + 1;
            *cursor += 0x10;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyQuat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                             const int32_t* offsets) {
    *cursor = align16(*cursor);
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) std::memcpy(pose + offset, active ? *cursor : defaults + offset, 0x10);
        if (active) *cursor += 0x10;
        *stream_a += 4;
    }
    return context[1];
}

float* XB_THISCALL Byte3_ToQuat(const int8_t* in, float* out) {
    const float k = title_global<float>(0x0083ec60);
    const Lanes q = delta_quat(in[0], in[1], in[2], Lanes{{k, k, k, k}});
    std::memcpy(out, q.v, 16);
    return out;
}

uint32_t XB_STDCALL NAL_Packed8Quat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, int32_t* deltas,
                                                 uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float scale = *reinterpret_cast<const float*>(*stream_a) * *block_scale;
            NAL_QuatReader_DecodeByteFrames(reinterpret_cast<QuatReader*>(*readers), *dst, stride, count, scale, deltas);
            *readers += 0x40;
            *dst += 3;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed8Quat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* channel_scale = reinterpret_cast<const float*>(*stream_a);
            uint8_t* out = *data;
            const uint8_t length = **stream;
            *stream += 1;
            alignas(16) QuatReader reader;
            NAL_QuatReader_Init(&reader, frames, *channel_scale * *block_scale,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream)), 0);
            NAL_QuatReader_DecodeByteFrames(&reader, out, stride, frames, *channel_scale * *block_scale,
                                            &title_global<int32_t>(0x00906820));
            *stream += length;
            *data += 3;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed8Quat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            *stream += **stream + 1;
            *cursor += 3;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed8Quat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                             const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) {
            if (active) {
                alignas(16) float q[4];
                Byte3_ToQuat(reinterpret_cast<const int8_t*>(*cursor), q);
                std::memcpy(pose + offset, q, 16);
            } else {
                std::memcpy(pose + offset, defaults + offset, 16);
            }
        }
        if (active) *cursor += 3;
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed8Quat_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) *cursor += 3;
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyQuat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                      uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    *dst = align16(*dst);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float scale = *reinterpret_cast<const float*>(*stream_a) * *block_scale;
            NAL_QuatReader_DecodeFloatFrames(reinterpret_cast<QuatReader*>(*readers), *dst, stride, count, scale);
            *readers += 0x40;
            *dst += 0x10;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyQuat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                     int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *data = align16(*data);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* channel_scale = reinterpret_cast<const float*>(*stream_a);
            uint8_t* out = *data;
            const uint8_t length = **stream;
            *stream += 1;
            alignas(16) QuatReader reader;
            NAL_QuatReader_Init(&reader, frames, *channel_scale * *block_scale,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream)), 0);
            NAL_QuatReader_DecodeFloatFrames(&reader, out, stride, frames, *channel_scale * *block_scale);
            *stream += length;
            *data += 0x10;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed16Quat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, int32_t* deltas,
                                                 uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    *dst = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*dst) + 1) & ~static_cast<uintptr_t>(1));
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float scale = *reinterpret_cast<const float*>(*stream_a) * *block_scale;
            NAL_QuatReader_DecodeFrames(reinterpret_cast<QuatReader*>(*readers), *dst, stride, count, scale, deltas);
            *readers += 0x40;
            *dst += 8;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_Packed16Quat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *data = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*data) + 1) & ~static_cast<uintptr_t>(1));
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* channel_scale = reinterpret_cast<const float*>(*stream_a);
            uint8_t* out = *data;
            const uint8_t length = **stream;
            *stream += 1;
            alignas(16) QuatReader reader;
            NAL_QuatReader_Init(&reader, frames, *channel_scale * *block_scale,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream)), 0);
            NAL_QuatReader_DecodeFrames(&reader, out, stride, frames, *channel_scale * *block_scale,
                                        &title_global<int32_t>(0x00906820));
            *stream += length;
            *data += 8;
        }
        *stream_a += 4;
    }
    return context[1];
}

float* XB_THISCALL Short4_ToQuat(const int16_t* in, float* out) {
    const Lanes v{{static_cast<float>(in[0]), static_cast<float>(in[1]), static_cast<float>(in[2]),
                   static_cast<float>(in[3])}};
    const Lanes q = mul(v, title_global<float>(0x007c2410));
    std::memcpy(out, q.v, 16);
    return out;
}

uint32_t XB_STDCALL NAL_Packed16Quat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                             const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) {
            if (active) {
                alignas(16) float q[4];
                Short4_ToQuat(reinterpret_cast<const int16_t*>(*cursor), q);
                std::memcpy(pose + offset, q, 16);
            } else {
                std::memcpy(pose + offset, defaults + offset, 16);
            }
        }
        if (active) *cursor += 8;
        *stream_a += 4;
    }
    return context[1];
}

void XB_THISCALL NAL_EntropyReader_DecodeShorts(EntropyReader* reader, int16_t* out, int32_t stride, uint32_t count, float scale,
                                                float factor, float bias, float* buffer) {
    NAL_EntropyReader_DecodeFloats(reader, buffer, 4, count, scale);
    for (; count; --count) {
        const float v = (*buffer + bias) * factor;
        if (title_global<float>(0x007c240c) > v)
            *out = static_cast<int16_t>(0x8001);
        else if (v > title_global<float>(0x0083f258))
            *out = 0x7fff;
        else
            *out = static_cast<int16_t>(static_cast<int32_t>(v));
        ++buffer;
        out = reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(out) + stride);
    }
}

void XB_CDECL NAL_Packed16EntropyIKSpin_DecodeChannel(uint8_t* readers, uint8_t* out, int32_t* deltas, uint32_t, uint32_t count,
                                             int32_t stride, uint32_t, const float* block_scale, const float* ranges,
                                             const uint8_t* has_position) {
    EntropyReader* floats = reinterpret_cast<EntropyReader*>(readers + 0x80);
    float* buffer = reinterpret_cast<float*>(deltas);
    if (*has_position) {
        NAL_QuatReader_DecodeFrames(reinterpret_cast<QuatReader*>(readers), out + 8, stride, count, ranges[4] * *block_scale,
                                    deltas);
        for (int32_t c = 0; c < 3; ++c)
            NAL_EntropyReader_DecodeShorts(floats + c, reinterpret_cast<int16_t*>(out + 0x10 + 2 * c), stride, count,
                                           *block_scale * ranges[0], ranges[2], ranges[1], buffer);
        NAL_EntropyReader_DecodeShorts(floats + 3, reinterpret_cast<int16_t*>(out + 0x16), stride, count,
                                       ranges[3] * *block_scale, from_bits(0x45a2f8e0), from_bits(0x38c910a4), buffer);
    } else {
        NAL_QuatReader_DecodeFrames(reinterpret_cast<QuatReader*>(readers), out, stride, count, ranges[4] * *block_scale,
                                    deltas);
        NAL_QuatReader_DecodeFrames(reinterpret_cast<QuatReader*>(readers + 0x40), out + 8, stride, count,
                                    ranges[4] * *block_scale, deltas);
        NAL_EntropyReader_DecodeShorts(reinterpret_cast<EntropyReader*>(readers + 0x80), reinterpret_cast<int16_t*>(out + 0x16),
                                       stride, count, ranges[3] * *block_scale, from_bits(0x45a2f8e0),
                                       from_bits(0x38c910a4), buffer);
    }
}

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, int32_t* deltas,
                                                  uint32_t unused, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    *dst = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*dst) + 1) & ~static_cast<uintptr_t>(1));
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_Packed16EntropyIKSpin_DecodeChannel(*readers, *dst, deltas, unused, count, stride, old_a, block_scale,
                                           reinterpret_cast<const float*>(*stream_a), *stream_b);
            *readers += 0xc0;
            *dst += 0x1a;
            *stream_b += 1;
        }
        *stream_a += 0x14;
    }
    return context[1];
}

namespace {

// One byte-length-prefixed entropy stream: a fresh reader (mode 0xff = read the header) at
// `reader` unless that address is 0, then the stream skipped.
void begin_entropy_stream(uintptr_t reader, uint8_t** stream) {
    const uint8_t length = **stream;
    *stream += 1;
    if (reader) {
        EntropyReader* r = reinterpret_cast<EntropyReader*>(reader);
        r->ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
        r->bit = 0;
        r->mode = 0xff;
        r->zeros = 0;
    }
    *stream += length;
}

// A quaternion stream: NAL_QuatReader_Init at `reader` unless 0. The length byte is kept in the
// low byte of the caller's argument slot `scratch` (if given), as the title does.
void begin_quat_stream(uintptr_t reader, uint8_t** stream, uint32_t count, float scale, uint32_t* scratch) {
    const uint8_t length = **stream;
    if (scratch) *reinterpret_cast<uint8_t*>(scratch) = length;
    *stream += 1;
    if (reader)
        NAL_QuatReader_Init(reinterpret_cast<QuatReader*>(reader), count, scale,
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream)), 0);
    *stream += length;
}

}  // namespace

PLATFORM_ASM_ENTRY(NAL_Packed16EntropyIKSpin_BeginChannel_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_Packed16EntropyIKSpin_BeginChannel) "addl $4, %esp\n"
                   "ret")

void XB_CDECL NAL_Packed16EntropyIKSpin_BeginChannel(uint32_t* args) {
    const uintptr_t readers = args[0];
    uint8_t** stream = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(args[1]));
    const uint32_t count = args[2];
    const float* block_scale = reinterpret_cast<const float*>(static_cast<uintptr_t>(args[4]));
    const float* ranges = reinterpret_cast<const float*>(static_cast<uintptr_t>(args[5]));
    const uint8_t* has_position = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(args[6]));
    if (*has_position) {
        begin_quat_stream(readers, stream, count, ranges[4] * *block_scale, &args[6]);
        for (uintptr_t c = 0; c < 4; ++c) begin_entropy_stream(readers + 0x80 + 0x10 * c, stream);
    } else {
        begin_quat_stream(readers, stream, count, ranges[4] * *block_scale, &args[6]);
        begin_quat_stream(readers + 0x40, stream, count, ranges[4] * *block_scale, &args[6]);
        begin_entropy_stream(readers + 0x80, stream);
    }
}

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            uint32_t args[7] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*readers)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream)),
                                count,
                                old_a,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block_scale)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b))};
            NAL_Packed16EntropyIKSpin_BeginChannel(args);
            *readers += 0xc0;
            *stream_b += 1;
        }
        *stream_a += 0x14;
    }
    return context[1];
}

namespace {

// Slots 7 and 8 of the int16 position + orientation class: nothing to decode, only the
// pointers move (dst 0x1a and one flag byte per active channel, stream A 0x14 per channel).
uint32_t packed_posori_skip(const uint32_t* context, uint8_t** data) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *data = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*data) + 1) & ~static_cast<uintptr_t>(1));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            *data += 0x1a;
            *stream_b += 1;
        }
        *stream_a += 0x14;
    }
    return context[1];
}

}  // namespace

uint8_t** XB_STDCALL NAL_Packed16EntropyIKSpin_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0x50;
    return cursor;
}

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_InitState(const uint8_t* group, uint8_t** cursor) {
    uint8_t* start = align16(*cursor);
    *cursor = start;
    *cursor = start + channel_count(group) * 0x50;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start));
}

uint8_t** XB_STDCALL NAL_Packed16EntropyIKSpin_FreeState(const uint8_t* group, uint8_t** cursor) {
    *cursor = align16(*cursor);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(group)); ++i) *cursor += 0x50;
    return cursor;
}

uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    *dst = align16(*dst);
    *src = align16(*src);
    return copy_advance(dst, src, channel_count(group) * 0x50);
}

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) *cursor += 0x1a;
    return context[1];
}

namespace {

// Slots 10 and 11 (static data): only the stream pointers move - one flag byte per active channel
// after stream B's block scale, 0x14 bytes of stream A per channel.
uint32_t packed_posori_skip_static(const uint32_t* context) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) *stream_b += 1;
        *stream_a += 0x14;
    }
    return context[1];
}

}  // namespace

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_ApplyStatic(const uint32_t* context, uint8_t*, uint32_t, const int32_t*) {
    return packed_posori_skip_static(context);
}

uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_ApplyWraps(const uint32_t* context, uint8_t*, uint8_t*, int32_t, uint32_t, const int32_t*) {
    return packed_posori_skip_static(context);
}

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t**, uint32_t, int32_t) {
    return packed_posori_skip(context, data);
}

uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t**, uint32_t) {
    return packed_posori_skip(context, cursor);
}

PLATFORM_ASM_ENTRY(NAL_Packed16EntropyIKSpin_Dequantize_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 28(%ebp)\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_Packed16EntropyIKSpin_Dequantize) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL NAL_Packed16EntropyIKSpin_Dequantize(uint32_t entry, uint8_t* dst, const int16_t* src, uint32_t, const float*,
                                          const float* ranges, const uint8_t* has_position) {
    // The record sits where the original's frame has it (push ebp; and esp,-16; sub 0x78; two
    // pushes): the fields this path does not write keep that stack's old contents.
    uint8_t* record = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(((entry - 4) & ~15u) - 0x50));
    const float k = title_global<float>(0x007c2410);
    const Lanes q1 = mul(Lanes{{static_cast<float>(src[4]), static_cast<float>(src[5]), static_cast<float>(src[6]),
                                static_cast<float>(src[7])}},
                         k);
    if (*has_position) {
        const float one = title_global<float>(0x0083eb20);
        const float inv = one / ranges[2];
        const float pos[4] = {inv * static_cast<float>(src[8]) - ranges[1], static_cast<float>(src[9]) * inv - ranges[1],
                              static_cast<float>(src[10]) * inv - ranges[1], one};
        std::memcpy(record + 0x20, q1.v, 16);
        std::memcpy(record + 0x30, pos, 16);
        std::memcpy(record + 0x42, &src[11], 2);
    } else {
        const Lanes q0 = mul(Lanes{{static_cast<float>(src[0]), static_cast<float>(src[1]), static_cast<float>(src[2]),
                                    static_cast<float>(src[3])}},
                             k);
        const float zero = 0.0f;
        std::memcpy(record, q0.v, 16);
        std::memcpy(record + 0x10, q1.v, 16);
        std::memcpy(record + 0x3c, &zero, 4);
        std::memcpy(record + 0x40, &src[11], 2);
    }
    std::memcpy(dst, record, 0x50);
}

PLATFORM_ASM_ENTRY(NAL_Packed16EntropyIKSpin_Evaluate_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_Packed16EntropyIKSpin_Evaluate) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret $0x14")

uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_Evaluate(uint32_t entry, const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                            const uint8_t* defaults, const int32_t* offsets) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) {
            if (active) {
                // The original calls it 0x40 below its own entry (4 locals, 4 saved registers, 6 arguments).
                NAL_Packed16EntropyIKSpin_Dequantize(entry - 0x40, pose + offset, reinterpret_cast<const int16_t*>(*cursor), old_a,
                                            block_scale, reinterpret_cast<const float*>(*stream_a), *stream_b);
                *cursor += 0x1a;
                *stream_b += 1;
            } else {
                std::memcpy(pose + offset, defaults + offset, 0x50);
            }
        } else if (active) {
            *cursor += 0x1a;
            *stream_b += 1;
        }
        *stream_a += 0x14;
    }
    return context[1];
}

void XB_CDECL NAL_EntropyTrajPosOri_BeginChannel(uint8_t* readers, uint8_t** stream, uint32_t count, uint32_t,
                                                 const float* block_scale, const float* ranges, uint32_t) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(readers);
    for (uintptr_t c = 0; c < 3; ++c) begin_entropy_stream(base + 0x40 + 0x10 * c, stream);
    begin_quat_stream(base, stream, count, ranges[1] * *block_scale, nullptr);
}

uint32_t XB_STDCALL NAL_EntropyTrajPosOri_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream,
                                                     uint32_t count) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_EntropyTrajPosOri_BeginChannel(*readers, stream, count, old_a, block_scale,
                                               reinterpret_cast<const float*>(*stream_a),
                                               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b)));
            *readers += 0x70;
            *stream_b += 0x20;
        }
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyTrajPosOri_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                       uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    *dst = align16(*dst);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* ranges = reinterpret_cast<const float*>(*stream_a);
            uint8_t* out = *dst;
            uint8_t* reader = *readers;
            for (int32_t c = 0; c < 3; ++c)
                NAL_EntropyReader_DecodeFloats(reinterpret_cast<EntropyReader*>(reader + 0x40 + 0x10 * c),
                                               reinterpret_cast<float*>(out + 0x10 + 4 * c), stride, count,
                                               *block_scale * ranges[0]);
            NAL_QuatReader_DecodeFloatFrames(reinterpret_cast<QuatReader*>(reader), out, stride, count, ranges[1] * *block_scale);
            *readers += 0x70;
            *dst += 0x20;
            *stream_b += 0x20;
        }
        *stream_a += 8;
    }
    return context[1];
}

void XB_CDECL NAL_EntropyTrajPosOri_DecodeChannelBlock(uint8_t* out, uint8_t** stream, uint32_t frames, int32_t stride, uint32_t,
                                                       const float* block_scale, const float* ranges, uint32_t) {
    for (int32_t c = 0; c < 3; ++c) {
        const uint8_t length = **stream;
        *stream += 1;
        EntropyReader reader;  // prev is set by the header (mode 0xff) before it is read
        reader.ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
        reader.bit = 0;
        reader.mode = 0xff;
        reader.zeros = 0;
        NAL_EntropyReader_DecodeFloats(&reader, reinterpret_cast<float*>(out + 0x10 + 4 * c), stride, frames,
                                       *block_scale * ranges[0]);
        *stream += length;
    }
    const uint8_t length = **stream;
    *stream += 1;
    alignas(16) QuatReader q;
    NAL_QuatReader_Init(&q, frames, ranges[1] * *block_scale, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream)), 0);
    NAL_QuatReader_DecodeFloatFrames(&q, out, stride, frames, ranges[1] * *block_scale);
    *stream += length;
}

uint32_t XB_STDCALL NAL_EntropyTrajPosOri_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                      int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *data = align16(*data);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    *stream_b = align16(*stream_b);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_EntropyTrajPosOri_DecodeChannelBlock(*data, stream, frames, stride, old_a, block_scale,
                                                     reinterpret_cast<const float*>(*stream_a),
                                                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b)));
            *data += 0x20;
            *stream_b += 0x20;
        }
        *stream_a += 8;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat1_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                  int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *data = align4(*data);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const uint8_t length = **stream;
            *stream += 1;
            EntropyReader reader;  // prev is set by the header (mode 0xff) before it is read
            reader.ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
            reader.bit = 0;
            reader.mode = 0xff;
            reader.zeros = 0;
            NAL_EntropyReader_DecodeFloats(&reader, reinterpret_cast<float*>(*data), stride, frames,
                                           *reinterpret_cast<const float*>(*stream_a) * *block_scale);
            *stream += length;
            *data += 4;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_PackedEntropyFloat3_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_EntropyFloat3_InitReaders(*readers, stream);
            *readers += 0x30;
        }
        *stream_a += 0xc;
    }
    return context[1];
}

namespace {

// One channel's key block of a packed entropy Float3 class: per component its stream decoded
// into the buffer 0x00906820, then (v + ranges[1]) * ranges[2] clamped to the element range,
// `stride` apart. The title keeps its temporaries in its own argument slots (cdecl): the length
// byte in &stream's low byte, the buffer cursor and the ranges in the out/frames/ranges slots -
// written here as it leaves them.
template <typename T>
void packed_float3_channel_block(uint32_t* args, uint32_t low_at, uint32_t high_at, T low_code, T high_code) {
    uint8_t* const out = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[0]));
    uint8_t** stream = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(args[1]));
    const uint32_t frames = args[2];
    const int32_t stride = static_cast<int32_t>(args[3]);
    const float* block_scale = reinterpret_cast<const float*>(static_cast<uintptr_t>(args[5]));
    const float* ranges = reinterpret_cast<const float*>(static_cast<uintptr_t>(args[6]));
    float* const buffer = &title_global<float>(0x00906820);
    const float bias = ranges[1];
    const float factor = ranges[2];
    for (int32_t c = 0; c < 3; ++c) {
        const uint8_t length = **stream;
        *stream += 1;
        *reinterpret_cast<uint8_t*>(&args[1]) = length;
        EntropyReader reader;  // prev is set by the header (mode 0xff) before it is read
        reader.ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
        reader.bit = 0;
        reader.mode = 0xff;
        reader.zeros = 0;
        NAL_EntropyReader_DecodeFloats(&reader, buffer, 4, frames, *block_scale * ranges[0]);
        T* dst = reinterpret_cast<T*>(out + c * sizeof(T));
        for (uint32_t n = 0; n < frames; ++n) {
            const float v = (buffer[n] + bias) * factor;
            if (title_global<float>(low_at) > v)
                *dst = low_code;
            else if (v > title_global<float>(high_at))
                *dst = high_code;
            else
                *dst = static_cast<T>(static_cast<int32_t>(v));
            dst = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(dst) + stride);
        }
        *stream += length;
    }
    uint32_t bits;
    args[0] = 0x00906820 + 4 * frames;
    std::memcpy(&bits, &factor, 4);
    args[2] = bits;
    std::memcpy(&bits, &bias, 4);
    args[6] = bits;
}

template <typename Channel>
uint32_t packed_float3_decode_block(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames, int32_t stride,
                                    uint32_t size, Channel channel) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    if (size == 6) *data = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*data) + 1) & ~static_cast<uintptr_t>(1));
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            uint32_t args[8] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*data)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream)),
                                frames,
                                static_cast<uint32_t>(stride),
                                old_a,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block_scale)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b))};
            channel(args);
            *data += size;
        }
        *stream_a += 0xc;
    }
    return context[1];
}

template <typename T, typename Dequantize>
uint32_t packed_float3_evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                const int32_t* offsets, Dequantize dequantize) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    if (sizeof(T) == 2)
        *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        const int32_t offset = offsets[channel_index(context, i)];
        const bool active = channel_active(context, i);
        if (offset >= 0) {
            if (active)
                dequantize(reinterpret_cast<float*>(pose + offset), reinterpret_cast<const T*>(*cursor), 0, 0,
                           reinterpret_cast<const float*>(*stream_a));
            else
                std::memcpy(pose + offset, defaults + offset, 0xc);
        }
        if (active) *cursor += 3 * sizeof(T);
        *stream_a += 0xc;
    }
    return context[1];
}

}  // namespace

PLATFORM_ASM_ENTRY(NAL_Packed16EntropyFloat3_DecodeChannelBlock_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_Packed16EntropyFloat3_DecodeChannelBlock) "addl $4, %esp\n"
                   "ret")

void XB_CDECL NAL_Packed16EntropyFloat3_DecodeChannelBlock(uint32_t* args) {
    packed_float3_channel_block<int16_t>(args, 0x007c240c, 0x0083f258, static_cast<int16_t>(0x8001), 0x7fff);
}

PLATFORM_ASM_ENTRY(NAL_Packed8EntropyFloat3_DecodeChannelBlock_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_Packed8EntropyFloat3_DecodeChannelBlock) "addl $4, %esp\n"
                   "ret")

void XB_CDECL NAL_Packed8EntropyFloat3_DecodeChannelBlock(uint32_t* args) {
    packed_float3_channel_block<int8_t>(args, 0x0083ed84, 0x0083ed88, static_cast<int8_t>(0x81), 0x7f);
}

uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                          int32_t stride) {
    return packed_float3_decode_block(context, data, stream, frames, stride, 6, NAL_Packed16EntropyFloat3_DecodeChannelBlock);
}

uint32_t XB_STDCALL NAL_Packed8EntropyFloat3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                         int32_t stride) {
    return packed_float3_decode_block(context, data, stream, frames, stride, 3, NAL_Packed8EntropyFloat3_DecodeChannelBlock);
}

uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                                       const uint8_t* defaults, const int32_t* offsets) {
    return packed_float3_evaluate<int16_t>(context, pose, cursor, defaults, offsets, NAL_Packed16_DequantizeFloat3);
}

uint32_t XB_STDCALL NAL_Packed8EntropyFloat3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                                      const uint8_t* defaults, const int32_t* offsets) {
    return packed_float3_evaluate<int8_t>(context, pose, cursor, defaults, offsets, NAL_Packed8_DequantizeFloat3);
}

uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 1) & ~static_cast<uintptr_t>(1));
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i)
        if (channel_active(context, i)) *cursor += 6;
    return context[1];
}

PLATFORM_ASM_ENTRY(NAL_EntropyFloat4_InitReaders_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_EntropyFloat4_InitReaders) "addl $4, %esp\n"
                   "ret")

uint8_t** XB_CDECL NAL_EntropyFloat4_InitReaders(uint32_t* args) {
    const uintptr_t readers = args[0];
    uint8_t** stream = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(args[1]));
    // Four component streams, readers 0x10 apart (a null address skipped). The title keeps the
    // second and third length bytes in the low byte of its &stream argument slot.
    for (uintptr_t c = 0; c < 4; ++c) {
        const uint8_t length = **stream;
        if (c == 1 || c == 2) *reinterpret_cast<uint8_t*>(&args[1]) = length;
        *stream += 1;
        if (readers + 0x10 * c) {
            EntropyReader* r = reinterpret_cast<EntropyReader*>(readers + 0x10 * c);
            r->ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
            r->bit = 0;
            r->mode = 0xff;
            r->zeros = 0;
        }
        *stream += length;
    }
    return stream;
}

namespace {

uint32_t float4_begin_block(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count, uint32_t record) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            uint32_t args[7] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*readers)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream)),
                                count,
                                old_a,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block_scale)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b))};
            NAL_EntropyFloat4_InitReaders(args);
            *readers += 0x40;
        }
        *stream_a += record;
    }
    return context[1];
}

}  // namespace

uint32_t XB_STDCALL NAL_EntropyFloat4_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count) {
    return float4_begin_block(context, readers, stream, count, 4);
}

uint32_t XB_STDCALL NAL_PackedEntropyFloat4_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream,
                                                       uint32_t count) {
    return float4_begin_block(context, readers, stream, count, 0xc);
}

PLATFORM_ASM_ENTRY(NAL_EntropyFloat4_DecodeChannelBlock_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_EntropyFloat4_DecodeChannelBlock) "addl $4, %esp\n"
                   "ret")

void XB_CDECL NAL_EntropyFloat4_DecodeChannelBlock(uint32_t* args) {
    uint8_t* const out = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[0]));
    uint8_t** stream = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(args[1]));
    const uint32_t frames = args[2];
    const int32_t stride = static_cast<int32_t>(args[3]);
    const float* block_scale = reinterpret_cast<const float*>(static_cast<uintptr_t>(args[5]));
    const float* ranges = reinterpret_cast<const float*>(static_cast<uintptr_t>(args[6]));
    for (int32_t c = 0; c < 4; ++c) {
        const uint8_t length = **stream;
        *stream += 1;
        *reinterpret_cast<uint8_t*>(&args[1]) = length;  // the title's scratch: its &stream slot
        EntropyReader reader;  // prev is set by the header (mode 0xff) before it is read
        reader.ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream));
        reader.bit = 0;
        reader.mode = 0xff;
        reader.zeros = 0;
        NAL_EntropyReader_DecodeFloats(&reader, reinterpret_cast<float*>(out + 4 * c), stride, frames, *block_scale * ranges[0]);
        *stream += length;
    }
}

uint32_t XB_STDCALL NAL_EntropyFloat4_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                  int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *data = align4(*data);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    const uint32_t old_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            uint32_t args[8] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*data)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream)),
                                frames,
                                static_cast<uint32_t>(stride),
                                old_a,
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block_scale)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a)),
                                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b))};
            NAL_EntropyFloat4_DecodeChannelBlock(args);
            *data += 0x10;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat4_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *cursor = align4(*cursor);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            NAL_SkipPrefixedStreams4(stream);
            *cursor += 0x10;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyQuat_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align16(*readers);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            QuatReader* reader = reinterpret_cast<QuatReader*>(*readers);
            const uint8_t length = **stream;
            *stream += 1;
            if (reader) {
                const float scale = *reinterpret_cast<const float*>(*stream_a) * *block_scale;
                NAL_QuatReader_Init(reader, count, scale, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream)), 0);
            }
            *stream += length;
            *readers += 0x40;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat1_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                   uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    *dst = align4(*dst);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* channel_scale = reinterpret_cast<const float*>(*stream_a);
            NAL_EntropyReader_DecodeFloats(reinterpret_cast<EntropyReader*>(*readers), reinterpret_cast<float*>(*dst), stride,
                                           count, *channel_scale * *block_scale);
            *readers += 0x10;
            *dst += 4;
        }
        *stream_a += 4;
    }
    return context[1];
}

uint32_t XB_STDCALL NAL_EntropyFloat4_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                   uint32_t, uint32_t count, int32_t stride) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    *dst = align4(*dst);
    const float* block_scale = reinterpret_cast<const float*>(align4(*stream_b));
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            const float* channel_scale = reinterpret_cast<const float*>(*stream_a);
            float* out = reinterpret_cast<float*>(*dst);
            EntropyReader* reader = reinterpret_cast<EntropyReader*>(*readers);
            for (int32_t c = 0; c < 4; ++c)
                NAL_EntropyReader_DecodeFloats(reader + c, out + c, stride, count, *block_scale * *channel_scale);
            *readers += 0x40;
            *dst += 0x10;
        }
        *stream_a += 4;
    }
    return context[1];
}

namespace {

// The packed entropy Float3 channels: per component the entropy reader decodes into the float
// buffer (scale block * ranges[0]), then each (value + ranges[1]) * ranges[2] is clamped to
// +-kMax and truncated into T at out, `stride` apart. All three components share the ranges.
template <typename T>
void decode_packed_float3(uint8_t* readers, uint8_t* out, float* buffer, uint32_t count, int32_t stride,
                          const float* block_scale, const float* ranges, uint32_t low_global, uint32_t high_global,
                          T low_value, T high_value) {
    for (int c = 0; c < 3; ++c) {
        const float bias = ranges[1];
        const float factor = ranges[2];
        NAL_EntropyReader_DecodeFloats(reinterpret_cast<EntropyReader*>(readers) + c, buffer, 4, count,
                                       *block_scale * ranges[0]);
        T* dst = reinterpret_cast<T*>(out) + c;
        const float* in = buffer;
        for (uint32_t n = count; n; --n) {
            const float v = (*in + bias) * factor;
            if (title_global<float>(low_global) > v)
                *dst = low_value;
            else if (v > title_global<float>(high_global))
                *dst = high_value;
            else
                *dst = static_cast<T>(static_cast<int32_t>(v));
            ++in;
            dst = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(dst) + stride);
        }
    }
}

// Their DecodeFrames: per active channel the channel decoder on (readers, out, the float buffer,
// -, count, stride, the unaligned stream A, &block scale, the channel's ranges, stream B); readers
// +0x30, out + 3 components; stream A moves 12 (three ranges) per channel.
template <uint32_t Size, typename Decode>
uint32_t decode_packed_float3_frames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t buffer,
                                     uint32_t arg5, uint32_t count, int32_t stride, Decode decode) {
    uint8_t** stream_a = context_stream(context, 2);
    uint8_t** stream_b = context_stream(context, 3);
    *readers = align4(*readers);
    if (Size == 2) *dst = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*dst) + 1) & ~static_cast<uintptr_t>(1));
    uint8_t* block_scale = align4(*stream_b);
    *stream_b = block_scale;
    const uint32_t raw_a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_a));
    *stream_b = block_scale + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) {
            decode(*readers, *dst, buffer, arg5, count, stride, raw_a, reinterpret_cast<const float*>(block_scale),
                   reinterpret_cast<const float*>(*stream_a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*stream_b)));
            *readers += 0x30;
            *dst += 3 * Size;
        }
        *stream_a += 0xc;
    }
    return context[1];
}

}  // namespace

void XB_CDECL NAL_Packed8EntropyFloat3_DecodeChannelFrames(uint8_t* readers, uint8_t* out, float* buffer, uint32_t,
                                                           uint32_t count, int32_t stride, uint32_t,
                                                           const float* block_scale, const float* ranges, uint32_t) {
    decode_packed_float3<int8_t>(readers, out, buffer, count, stride, block_scale, ranges, 0x0083ed84, 0x0083ed88,
                                 static_cast<int8_t>(0x81), 0x7f);
}

void XB_CDECL NAL_Packed16EntropyFloat3_DecodeChannelFrames(uint8_t* readers, uint8_t* out, float* buffer, uint32_t,
                                                            uint32_t count, int32_t stride, uint32_t,
                                                            const float* block_scale, const float* ranges, uint32_t) {
    decode_packed_float3<int16_t>(readers, out, buffer, count, stride, block_scale, ranges, 0x007c240c, 0x0083f258,
                                  static_cast<int16_t>(0x8001), 0x7fff);
}

uint32_t XB_STDCALL NAL_Packed8EntropyFloat3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers,
                                                          uint32_t buffer, uint32_t arg5, uint32_t count, int32_t stride) {
    return decode_packed_float3_frames<1>(context, dst, readers, buffer, arg5, count, stride,
        [](uint8_t* r, uint8_t* o, uint32_t b, uint32_t a5, uint32_t n, int32_t s, uint32_t raw, const float* bs,
           const float* ranges, uint32_t sb) {
            NAL_Packed8EntropyFloat3_DecodeChannelFrames(r, o, reinterpret_cast<float*>(static_cast<uintptr_t>(b)), a5, n, s,
                                                        raw, bs, ranges, sb);
        });
}

uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers,
                                                           uint32_t buffer, uint32_t arg5, uint32_t count, int32_t stride) {
    return decode_packed_float3_frames<2>(context, dst, readers, buffer, arg5, count, stride,
        [](uint8_t* r, uint8_t* o, uint32_t b, uint32_t a5, uint32_t n, int32_t s, uint32_t raw, const float* bs,
           const float* ranges, uint32_t sb) {
            NAL_Packed16EntropyFloat3_DecodeChannelFrames(r, o, reinterpret_cast<float*>(static_cast<uintptr_t>(b)), a5, n,
                                                         s, raw, bs, ranges, sb);
        });
}

float* XB_THISCALL NAL_Float4_AddTo(const float* self, float* out, const float* in) {
    const float w = in[3] + self[3];
    const float z = in[2] + self[2];
    const float y = in[1] + self[1];
    const float x = in[0] + self[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
    return out;
}

namespace {

// Skips a block of fixed-size channels: stream B aligned + 4 (the block scale), stream A
// aligned and `bytes` per channel of the group.
void skip_fixed_block(const uint32_t* context, uint32_t bytes) {
    uint8_t** stream_b = context_stream(context, 3);
    uint8_t** stream_a = context_stream(context, 2);
    *stream_b = align4(*stream_b) + 4;
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) *stream_a += bytes;
}

}  // namespace

void XB_CDECL NAL_Float3_SkipFixedBlock(const uint32_t* context) { skip_fixed_block(context, 0xc); }
void XB_CDECL NAL_Float2_SkipFixedBlock(const uint32_t* context) { skip_fixed_block(context, 8); }

void XB_CDECL NAL_Float4_CopyChannels(const uint8_t* group, uint8_t** dst, uint8_t** src) {
    *dst = align4(*dst);
    *src = align4(*src);
    const uint32_t bytes = channel_count(group) << 4;
    platform_rep_movs(*dst, *src, bytes);  // the title's inline REP MOVSD/MOVSB
    *dst += bytes;
    *src += bytes;
}

namespace {

// Every active channel's block, length-prefixed ({size including the word, data}), stepped
// over in stream A (context[3], aligned to 4 first). Returns the group (context[1]).
uint32_t skip_sized_blocks(const uint32_t* context) {
    uint8_t** stream = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[3]));
    *stream = align4(*stream);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (!channel_active(context, i)) continue;
        uint8_t* block = *stream;
        *stream = block + *reinterpret_cast<const uint32_t*>(block);
    }
    return context[1];
}

}  // namespace

uint32_t XB_STDCALL NAL_CodNote_SkipBlock(const uint32_t* context, uint32_t, uint32_t, uint32_t) {
    return skip_sized_blocks(context);
}

uint32_t XB_STDCALL NAL_CodNote_DecodeFrames(const uint32_t* context, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                             uint32_t) {
    return skip_sized_blocks(context);
}

uint32_t XB_STDCALL NAL_CodNote_DecodeBlock(const uint32_t* context, uint32_t, uint32_t, uint32_t, uint32_t) {
    return skip_sized_blocks(context);
}

PLATFORM_ASM_ENTRY(NAL_SkipActiveChannelBlocks_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(NAL_SkipActiveChannelBlocks) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL NAL_SkipActiveChannelBlocks(const uint32_t* context) { return skip_sized_blocks(context); }

uint32_t XB_STDCALL NAL_Packed16_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    *cursor = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(*cursor) + 0xf) & ~uintptr_t{0xf});
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) {
        if (channel_active(context, i)) *cursor += 0x10;
    }
    return context[1];
}


namespace {

const float& ik_float(const uint8_t* record, uint32_t offset) {
    return *reinterpret_cast<const float*>(record + offset);
}
int16_t ik_short(const uint8_t* record, uint32_t offset) { return *reinterpret_cast<const int16_t*>(record + offset); }
void ik_set_short(uint8_t* record, uint32_t offset, int32_t value) {
    *reinterpret_cast<uint16_t*>(record + offset) = static_cast<uint16_t>(value);
}
// a's int16 at `offset` moved towards b's by s (truncated).
int32_t ik_lerp_short(const uint8_t* a, const uint8_t* b, uint32_t offset, float s) {
    const int32_t from = ik_short(a, offset);
    return static_cast<int32_t>(static_cast<float>(ik_short(b, offset) - from) * s) + from;
}
// Quat_Slerp by title address: its C version differs from the original in NaN payloads on
// NaN inputs (found by the offline test of NAL_Packed16EntropyIKSpin_Lerp).
const float* ik_quat_slerp(float* out, const uint8_t* a, const uint8_t* b, float s) {
    return title_function<const float* XB_CDECL(float*, const uint8_t*, const uint8_t*, float)>(0x0001c3b0)(out, a, b,
                                                                                                          s);  // Quat_Slerp
}
// x * v + y * s with the title's operand order (the left operand is the destination: a NaN
// there wins over one in the other operand).
float ik_mix(float x, float v, float y, float s) {
    float p = x;
    p *= v;
    float q = y;
    q *= s;
    p += q;
    return p;
}
void ik_slerp(uint8_t* dst, const uint8_t* a, const uint8_t* b, float s) {
    alignas(16) float q[4];
    std::memcpy(dst, ik_quat_slerp(q, a, b, s), 16);
}

}  // namespace

PLATFORM_ASM_ENTRY(NAL_Packed16EntropyIKSpin_Lerp_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_Packed16EntropyIKSpin_Lerp) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL NAL_Packed16EntropyIKSpin_Lerp(uint32_t entry, const uint8_t* a, const uint8_t* b, uint8_t* out, float t,
                                             float u) {
    // The 0x50-byte record ({q0, q1} the spin part with its int16 +0x40, {q2, position} the
    // IK part with +0x42, +0x3c the IK weight) is built where the original's frame has it
    // (push ebp; and esp,-16; sub 0x64; three pushes) and copied out whole: the words a path
    // does not write (+0x44.., and the half a weight of 0 or 1 leaves out) keep that
    // stack's old contents. Weights of exactly 0 or 1 copy or slerp halves; others blend
    // them by the weights (t on b, u = 1 - t on a).
    uint8_t* r = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(((entry - 4) & ~15u) - 0x50));
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    const float wa = ik_float(a, 0x3c);
    const float wb = ik_float(b, 0x3c);
    const auto finish = [&] { platform_rep_movs(out, r, 0x50); };
    // The IK half slerped by s (position lerped by s, a's by v), +0x42 moved by s.
    const auto ik_half = [&](float s, float v) {
        alignas(16) float q[4];
        const float* blended = ik_quat_slerp(q, a + 0x20, b + 0x20, s);
        std::memcpy(r + 0x20, blended, 16);
        const float x = ik_mix(ik_float(a, 0x30), v, ik_float(b, 0x30), s);
        const float y = ik_mix(ik_float(a, 0x34), v, ik_float(b, 0x34), s);
        const float z = ik_mix(ik_float(a, 0x38), v, ik_float(b, 0x38), s);
        std::memcpy(r + 0x30, &x, 4);
        std::memcpy(r + 0x34, &y, 4);
        std::memcpy(r + 0x38, &z, 4);
        ik_set_short(r, 0x42, ik_lerp_short(a, b, 0x42, s));
        finish();
    };
    const auto copy_spin = [&](const uint8_t* from) {
        std::memcpy(r, from, 0x20);
        ik_set_short(r, 0x40, ik_short(from, 0x40));
    };
    const auto copy_ik = [&](const uint8_t* from) {
        std::memcpy(r + 0x20, from + 0x20, 0x1c);
        ik_set_short(r, 0x42, ik_short(from, 0x42));
        finish();
    };
    if (wa == zero && wb == zero) {
        std::memcpy(r + 0x3c, &zero, 4);
        ik_slerp(r, a, b, t);
        ik_slerp(r + 0x10, a + 0x10, b + 0x10, t);
        ik_set_short(r, 0x40, ik_lerp_short(a, b, 0x40, t));
        finish();
        return;
    }
    if (wa == one && wb == one) {
        std::memcpy(r + 0x3c, &one, 4);
        ik_half(t, u);
        return;
    }
    if (wa == zero && wb == one) {
        copy_spin(a);
        std::memcpy(r + 0x3c, &t, 4);
        copy_ik(b);
        return;
    }
    if (wa == one && wb == zero) {
        copy_spin(b);
        std::memcpy(r + 0x3c, &u, 4);
        copy_ik(a);
        return;
    }
    const float weight = t * wb + u * wa;
    std::memcpy(r + 0x3c, &weight, 4);
    if (wb == one) {
        copy_spin(a);
    } else if (wa == one) {
        copy_spin(b);
    } else {
        const float ub = u * wb;
        const float ta = t * wa;
        const float s = ta + ub == zero ? zero : ta / (ta + ub);
        ik_slerp(r, a, b, s);
        ik_slerp(r + 0x10, a + 0x10, b + 0x10, s);
        ik_set_short(r, 0x40, ik_lerp_short(a, b, 0x40, s));
    }
    if (wb == zero) {
        copy_ik(a);
        return;
    }
    if (wa == zero) {
        copy_ik(b);
        return;
    }
    const float ta = t * wa;
    const float s = one - ta / (u * wb + ta);
    ik_half(s, one - s);
}

PLATFORM_ASM_ENTRY(NAL_Packed16EntropyIKSpin_Blend_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(NAL_Packed16EntropyIKSpin_Blend) "addl $36, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $0x14")

uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_Blend(uint32_t entry, int32_t count, uint8_t* out, const uint8_t* a,
                                                  const uint8_t* b, uint32_t t_bits, uint32_t ebx, uint32_t esi,
                                                  uint32_t edi) {
    // out[i] = Lerp(a[i], b[i], t) over `count` 0x50-byte records, each Lerp at the original's
    // ESP (push ecx, four pushes, three arguments) so its record lands where the original's
    // does; 1 - t kept in the push ecx slot, the count in its own argument slot.
    float t;
    std::memcpy(&t, &t_bits, 4);
    const float u = title_global<float>(0x0083eb20) - t;
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(entry - 4)), &u, 4);
    if (count <= 0) return static_cast<uint32_t>(count);
    uint32_t* slot = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(entry + 4));
    *slot = static_cast<uint32_t>(count);
    const uint32_t top = entry - 0x14;
    uint32_t u_bits;
    std::memcpy(&u_bits, &u, 4);
    uint32_t o = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
    uint32_t x = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a));
    uint32_t y = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
    (void)ebx;
    (void)esi;
    (void)edi;
    uint32_t left;
    do {
        const uint32_t args[] = {o, t_bits, u_bits};
        const TitleRegs regs{y, y, x, 0, o, x};
        title_call_at_regs(top, 0x0008cc40, &regs, 3, args);  // NAL_Packed16EntropyIKSpin_Lerp
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(top - 0x10)) = 0x0008d224;
        o += 0x50;
        x += 0x50;
        y += 0x50;
        left = *slot - 1;
        *slot = left;
    } while (left);
    return left;
}

}  // namespace game
