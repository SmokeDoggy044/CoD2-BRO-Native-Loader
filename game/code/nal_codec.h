#pragma once

// The NAL channel codecs (docs/nal_animation.md, "Stream formats"): the stream readers and
// the channel classes' block decoders.

#include <cstdint>

#include "compiler.h"

namespace game {

// RLE8 reader: {pointer, run left, value}; the stream is {count, value} byte pairs.
struct RleReader {
    const uint8_t* ptr;
    uint8_t run;
    uint8_t value;
};
// Entropy reader: {pointer | byte offset in the low 2 bits, bit position, mode (0xff = read
// a 5-bit header), zero run}.
struct EntropyReader {
    uint32_t ptr;
    uint8_t bit;
    uint8_t mode;
    uint16_t zeros;
    int32_t prev[2];  // NAL_EntropyReader_DecodeFloats: the last two values
};

// 0x0007c260 (reader in ECX, callee pops 0xc): `count` bytes from the RLE stream to dst,
// `stride` apart.
void XB_THISCALL NAL_RleReader_ReadBytes(RleReader* reader, uint8_t* dst, int32_t stride, uint32_t count);
// 0x0007cea0 (reader in ECX, callee pops 0xc): `count` values (words, `stride` bytes apart):
// the pending zero run first, the rest from the mode's decoder (table 0x008bb4a0), which
// returns the new zero run.
void XB_THISCALL NAL_EntropyReader_Decode(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);

// Channel class methods (cdecl; the vtable holds thiscall wrappers). A group: +0x24 first
// channel index, +0x28 channel count; context = {clip, group, &stream A, &stream B}.
// 0x0007c8e0 / 0x0007d1a0 / 0x0007e140: copy the group's state (count bytes / floats /
// float triples; the float ones align both cursors to 4 first) and advance both cursors.
uint32_t XB_CDECL NAL_Int1_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src);
uint32_t XB_CDECL NAL_Float1_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src);
uint32_t XB_CDECL NAL_Float3_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src);
// 0x0007c570: stream B advances one byte per active channel (bit set in clip +0x54).
void XB_CDECL NAL_SignalCounter_SkipStatic(const uint32_t* context);

// 0x0007db00 / 0x0007f940 (stdcall: context, &cursor): one cached frame skipped - the
// cursor aligned to 4, then 12 / 16 bytes per active channel. Returns the group.
uint32_t XB_STDCALL NAL_Float3_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
uint32_t XB_STDCALL NAL_Float4_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
// 0x00080010: the entropy classes' static data skipped: stream B aligned + 4, stream A
// aligned + 4 per channel. Returns &stream A.
uint32_t XB_CDECL NAL_Entropy_SkipStatic(const uint32_t* context);

// 0x0007d1f0 (stdcall): NAL_Float3_SkipCachedFrame for one float per active channel.
uint32_t XB_STDCALL NAL_EntropyFloat1_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
// 0x0007d650: stream A aligned + 8 per channel ({bias, scale} records). Returns &stream A.
uint32_t XB_CDECL NAL_Packed8Float1_SkipStatic(const uint32_t* context);
// 0x0007d940 (stdcall: context, &cursor, &stream, frames): a raw Float3 block skipped -
// per active channel the stream moves over `frames` triples (aligned to 4 first) and the
// cursor (aligned once) over one. Returns the group.
uint32_t XB_STDCALL NAL_Float3_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames);

// 0x0007f760: NAL_Float3_SkipBlock for Float4 (16 bytes per channel and frame).
uint32_t XB_STDCALL NAL_Float4_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames);
// 0x0007e9d0 / 0x0007f440 (cdecl: out, in, -, -, record): three int8 / int16 values to
// floats: value * record[2] + record[1].
void XB_CDECL NAL_Packed8_DequantizeFloat3(float* out, const int8_t* in, uint32_t, uint32_t, const float* record);
void XB_CDECL NAL_Packed16_DequantizeFloat3(float* out, const int16_t* in, uint32_t, uint32_t, const float* record);

// 0x0007c5f0 (stdcall: context, &readers, &stream, -): per active channel an RLE reader
// (8 bytes, the array aligned to 4) at the channel's run data ({length, bytes}).
uint32_t XB_STDCALL NAL_RLE8Int1_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t);
// 0x0007c680 (stdcall: context, &dst, &readers, -, -, count, stride): `count` frames of each
// active channel's bytes from its reader, `stride` apart, one byte column per channel.
uint32_t XB_STDCALL NAL_RLE8Int1_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t, uint32_t,
                                              uint32_t count, int32_t stride);

// 0x0007c7a0 (stdcall: context, &dst, &stream, -): an RLE block skipped: per active channel
// its run data ({length, bytes}, aligned to 4) and one dst byte.
uint32_t XB_STDCALL NAL_RLE8Int1_SkipBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, uint32_t);
// 0x0007eb40: three entropy readers (0x10 apart; a null check only on each one's address)
// set up at the stream's three length-prefixed component streams. Returns stream.
uint8_t** XB_CDECL NAL_EntropyFloat3_InitReaders(uint8_t* readers, uint8_t** stream);

// 0x0007d480 (stdcall: context, &dst, &stream, bytes) / 0x0007d3e0 (stdcall: context, &dst,
// &stream, frames, stride): a Packed8Float1 block skipped / decoded: per active channel
// its byte column (`frames` bytes, `stride` apart in dst) from the stream (aligned to 4
// first), dst one byte on; stream A ({bias, scale} records) 8 bytes per channel.
uint32_t XB_STDCALL NAL_Packed8Float1_SkipBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, int32_t bytes);
uint32_t XB_STDCALL NAL_Packed8Float1_DecodeBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, int32_t frames,
                                                  int32_t stride);

// 0x0007c2b0 (stdcall: context, &dst, &stream, count, stride) / 0x0007c360 (stdcall:
// context, &dst, &stream, -): a signal-counter block decoded (each active channel's RLE run
// data, {length, bytes}, read into its byte column) / skipped; dst and stream B one on per
// active channel.
uint32_t XB_STDCALL NAL_SignalCounter_DecodeBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, uint32_t count,
                                                  int32_t stride);
uint32_t XB_STDCALL NAL_SignalCounter_SkipBlock(const uint32_t* context, uint8_t** dst, uint8_t** stream, uint32_t);

// 0x0007d0b0 (stdcall: context, pose, &cursor, defaults, offsets): each channel with a pose
// offset (offsets[channel] >= 0) gets its cached float (active channels) or the default
// at the same offset; the cursor moves 4 per active channel, stream A 4 per channel.
uint32_t XB_STDCALL NAL_EntropyFloat1_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                               const int32_t* offsets);

// 0x0007cb10 (reader in ECX, callee pops 0x10: dst, stride, count, scale): `count` floats
// of a second-order predicted stream: the first call reads a header (5-bit residual mode,
// + 0x20 for a negative scale; the first value; the first delta), then residuals
// (NAL_EntropyReader_Decode) give v[n] = 2 v[n-1] - v[n-2] + r; out = v * |scale|.
void XB_THISCALL NAL_EntropyReader_DecodeFloats(EntropyReader* reader, float* dst, int32_t stride, uint32_t count, float scale);

// 0x0007de50 (cdecl, 8 words): one Float3 channel's block - x, y, z (dst + 0/4/8, `stride`
// apart) each from a length-prefixed entropy stream, scale *block_scale * *channel_scale.
void XB_CDECL NAL_EntropyFloat3_DecodeChannelBlock(float* dst, uint8_t** stream, uint32_t count, int32_t stride, uint32_t,
                                                   const float* block_scale, const float* channel_scale, uint32_t);
// 0x0007dd80 (stdcall: context, &data, &stream, frames, frame size): channel slot 7 of the
// entropy Float3 class: each active channel decoded into the next 12 bytes of the cache
// entry; stream B holds the block's scale, stream A one scale per channel.
uint32_t XB_STDCALL NAL_EntropyFloat3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                  int32_t frame_size);

// The position + orientation classes (32-byte values: quaternion, position, an unused w).
// Shared by all four vtables (0x007c2a98, 0x007c2b00, 0x007c2b68, 0x007c2bd0):
// 0x0007be80 / 0x0007be90: slot 0 (type tag 0x00909824) and slot 1 (value size 0x20).
uint32_t XB_CDECL NAL_PosOri_TypeId();
uint32_t XB_CDECL NAL_PosOri_ValueSize();
// 0x0008c9e0 (stdcall: count, dst, a, b, t): positions a * (1 - t) + b * t, quaternions
// Quat_Slerp(a, b, t); 0x0008cb10 takes each pair's weight from *weights (advanced by 4).
void XB_STDCALL NAL_PosOri_Blend(int32_t count, float* dst, const float* a, const float* b, float t);
void XB_STDCALL NAL_PosOri_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights);
// 0x000829c0 (stdcall: group, &cursor, -): the cursor aligned to 16, then 0x20 per channel.
uint8_t** XB_STDCALL NAL_PosOri_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t);
// 0x00082920 (stdcall: group, &cursor): state space - the cursor aligned to 16 (returned)
// and moved over 0x20 per channel; 0x00082e80 the same as a loop, returns &cursor.
uint32_t XB_STDCALL NAL_PosOri_InitState(const uint8_t* group, uint8_t** cursor);
uint8_t** XB_STDCALL NAL_PosOri_FreeState(const uint8_t* group, uint8_t** cursor);
// 0x00082960 (behind the slot 14 wrapper 0x00082940): NAL_Float3_CopyState with 0x20-byte
// values and both cursors aligned to 16.
uint32_t XB_CDECL NAL_PosOri_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src);
// 0x000832f0 (stdcall): one cached frame, 0x20 per active channel (cursor aligned to 16).
uint32_t XB_STDCALL NAL_PosOri_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
// Small cursor helpers in many vtables (stdcall, &cursor; return &cursor): 0x000834b0
// align to 4, 0x0007d250 + 8, 0x0007db60 + 4, 0x00082fe0 align to 16, 0x000829b0 + 0x20.
uint8_t** XB_STDCALL NAL_Cursor_Align4(uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Cursor_Skip8(uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Cursor_Skip4(uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Cursor_Align16(uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Cursor_Skip32(uint8_t** cursor);
// 0x000809d0 (cdecl, only the first word read): four length-prefixed byte streams skipped.
// Returns stream.
uint8_t** XB_CDECL NAL_SkipPrefixedStreams4(uint8_t** stream);
// The entropy trajectory class (vtable 0x007c2bd0). Static data: stream A 8 bytes per
// channel, stream B one word, then (aligned to 16) a 32-byte pair per active channel - the
// clip's whole-loop root motion.
// 0x00083000 (stdcall: context, &cursor, &stream, -): slot 8, a block skipped - per active
// channel its four component streams, cursor + 0x20.
uint32_t XB_STDCALL NAL_EntropyTrajPosOri_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t);
// 0x000830e0 (stdcall: context, pose, &cursor, defaults, offsets): slot 9,
// NAL_EntropyFloat1_Evaluate for 32-byte values (cursor aligned to 16; stream B + 0x20
// per active channel).
uint32_t XB_STDCALL NAL_EntropyTrajPosOri_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                                   const uint8_t* defaults, const int32_t* offsets);
// 0x00083200 (stdcall: context, pose, skip, offsets): slot 10, each active channel with a
// pose offset gets its static pair (unless skip).
uint32_t XB_STDCALL NAL_EntropyTrajPosOri_ApplyStatic(const uint32_t* context, uint8_t* pose, bool skip, const int32_t* offsets);
// 0x00083380 (behind the slot 11 wrapper 0x00083350; cdecl: context, dst pose, current
// pose, wraps, skip, offsets): NAL_Trajectory_AccumulateRoot per active channel with a pose
// offset.
uint32_t XB_CDECL NAL_EntropyTrajPosOri_ApplyWraps(const uint32_t* context, uint8_t* dst, uint8_t* current, int32_t wraps,
                                                   bool skip, const int32_t* offsets);
// 0x0008d3f0 (cdecl, 8 words; words 4-6 unused): unless skip, dst = dst, then the loop's
// root motion `wraps` times (inverted for negative counts), then the inverse of current.
// The original's result w is a never-written temporary (0 in a fresh test); ours writes 0.
void XB_CDECL NAL_Trajectory_AccumulateRoot(float* dst, const float* current, int32_t wraps, uint32_t, uint32_t, uint32_t,
                                            const float* loop, bool skip);

// 0x0008d2d0: an identical copy of NAL_Trajectory_AccumulateRoot, called by 0x00082ee0.
void XB_CDECL NAL_Trajectory_AccumulateRoot2(float* dst, const float* current, int32_t wraps, uint32_t, uint32_t, uint32_t,
                                             const float* loop, bool skip);
// 0x00082ee0 (behind the slot 11 wrapper 0x00082ec0 of vtable 0x007c2b68; cdecl):
// NAL_EntropyTrajPosOri_ApplyWraps for the plain trajectory class - only stream B (aligned
// to 16, 0x20 per active channel) is used.
uint32_t XB_CDECL NAL_TrajPosOri_ApplyWraps(const uint32_t* context, uint8_t* dst, uint8_t* current, int32_t wraps, bool skip,
                                            const int32_t* offsets);

}  // namespace game
