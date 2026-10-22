#include "nal_codec.h"

#include <cstring>

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
        const uint32_t decoder = title_global<uint32_t>(0x008bb4a0 + reader->mode * 4);
        reader->zeros = static_cast<uint16_t>(
            title_function<uint32_t XB_CDECL(EntropyReader*, uint32_t*, int32_t, uint32_t)>(decoder)(reader, dst, stride, count));
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

}  // namespace game
