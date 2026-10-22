#include "nal_codec.h"

#include <cstring>

#include "nal_entropy.h"
#include "title_data.h"

namespace game {

void XB_THISCALL NAL_RleReader_ReadBytes(RleReader* reader, uint8_t* dst, int32_t stride, uint32_t count) {
    for (; count; --count) {
        if (!reader->run) {
            reader->run = *reader->ptr++;
            reader->value = *reader->ptr++;
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
    std::memcpy(*dst, *src, bytes);
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
    return context[2];
}

uint32_t XB_STDCALL NAL_EntropyFloat1_SkipCachedFrame(const uint32_t* context, uint8_t** cursor) {
    return skip_cached_frame(context, cursor, 4);
}

uint32_t XB_CDECL NAL_Packed8Float1_SkipStatic(const uint32_t* context) {
    uint8_t** stream_a = reinterpret_cast<uint8_t**>(static_cast<uintptr_t>(context[2]));
    *stream_a = align4(*stream_a);
    for (int32_t i = 0; i < static_cast<int32_t>(channel_count(context_group(context))); ++i) *stream_a += 8;
    return context[2];
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
    const float scale = record[2], bias = record[1];
    out[0] = scale * static_cast<float>(in[0]) + bias;
    out[1] = static_cast<float>(in[1]) * scale + bias;
    out[2] = static_cast<float>(in[2]) * scale + bias;
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
        const uint8_t length = *(*stream)++;
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

}  // namespace game
