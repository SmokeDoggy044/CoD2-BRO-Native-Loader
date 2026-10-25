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

// The Float3 classes' shared methods (vtables 0x007c25b8, 0x007c2620, 0x007c2688, 0x007c26f0)
// and the quaternion classes' (0x007c28f8, 0x007c2960, 0x007c29c8, 0x007c2a30; values 16
// bytes, aligned to 16). Slot 0 type tag (0x0007be30 0x00909820, 0x0007be70 0x00909826),
// slot 1 value size (0x0007be40 12; 0x0007bec0 16, also slot 24 of the 16-aligned classes),
// slot 24 alignment (0x0007be60 4).
uint32_t XB_CDECL NAL_Float3_TypeId();
uint32_t XB_CDECL NAL_Float3_ValueSize();
uint32_t XB_CDECL NAL_Const4();
uint32_t XB_CDECL NAL_Quat_TypeId();
uint32_t XB_CDECL NAL_Const16();
// 0x0007d700 / 0x0007dad0 (slots 4 / 13, stdcall: group, &cursor[, -]): cursor aligned to 4,
// then 12 per channel; returns &cursor. 0x0007dab0 (slot 12): the same, returns the new cursor.
uint8_t** XB_STDCALL NAL_Float3_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t);
uint32_t XB_STDCALL NAL_Float3_InitState(const uint8_t* group, uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Float3_FreeState(const uint8_t* group, uint8_t** cursor);
// 0x00081c80 / 0x00081f00 (slots 4 / 13): as above with 16 per channel, aligned to 16.
// 0x00081ee0 (slot 12): returns the aligned start. 0x00082030 (cdecl, behind the slot 14
// wrapper 0x00081f30): NAL_PosOri_CopyState for 16-byte values.
uint8_t** XB_STDCALL NAL_Quat_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t);
uint32_t XB_STDCALL NAL_Quat_InitState(const uint8_t* group, uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Quat_FreeState(const uint8_t* group, uint8_t** cursor);
uint32_t XB_CDECL NAL_Quat_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src);
// 0x00081e60 (slot 10 of 0x007c2620 and 0x007c2a30, stdcall: context, pose, skip, offsets):
// NAL_Entropy_SkipStatic - these classes have no static values to apply.
uint32_t XB_STDCALL NAL_Entropy_SkipStatic4(const uint32_t* context, uint8_t*, uint32_t, const int32_t*);
// The packed entropy quaternion class (0x007c2a30): cached frames 8 bytes per active channel
// (aligned to 2). 0x00081930 (slot 15): one cached frame skipped, returns the group.
// 0x000820e0 (slot 8, stdcall: context, &cursor, &stream, -): a block skipped - per active
// channel one byte-length-prefixed stream, cursor + 8; stream B aligned + 4, stream A
// aligned + 4 per channel. Returns the group.
uint32_t XB_STDCALL NAL_Packed16Quat_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
uint32_t XB_STDCALL NAL_Packed16Quat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t);
// 0x0008c8e0 / 0x0008c950 (slots 2 / 3 of the quaternion classes): NAL_PosOri_Blend /
// BlendWeighted for quaternions alone (Quat_Slerp).
void XB_STDCALL NAL_Quat_Blend(int32_t count, float* dst, const float* a, const float* b, float t);
void XB_STDCALL NAL_Quat_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights);
// 0x0008bab0 / 0x0008bd80 (slots 2 / 3 of the Float3 classes): triples lerped (one weight /
// a weight per triple from a float cursor); MSVC unrolled both 4x.
void XB_STDCALL NAL_Float3_Blend(int32_t count, float* dst, const float* a, const float* b, float t);
void XB_STDCALL NAL_Float3_BlendWeighted(int32_t count, float* dst, const float* a, const float* b, const float** weights);
// The plain Float3 class (vtable 0x007c25b8): raw triples. 0x0007d730 (slot 5, stdcall:
// context, &readers, &stream, frames): per active channel the reader slot (array aligned to
// 4; null-checked) gets the stream (aligned to 4), which then moves over `frames` triples.
// 0x0007d7c0 (slot 6: context, &dst, &readers, -, -, count, stride): `count` triples from each
// active channel's reader, `stride` apart. 0x0007d880 (slot 7: context, &data, &stream,
// frames, stride): the same straight from the stream. 0x0007d9c0 (slot 9): NAL_EntropyFloat1_
// Evaluate for triples (no static streams). All return the group.
uint32_t XB_STDCALL NAL_Float3_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, int32_t frames);
uint32_t XB_STDCALL NAL_Float3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t, uint32_t,
                                            int32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_Float3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, int32_t frames,
                                           int32_t stride);
uint32_t XB_STDCALL NAL_Float3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                        const int32_t* offsets);
// The entropy Float3 class (vtable 0x007c2620; stream B one block scale, stream A one
// channel scale per channel). 0x0007db70 (slot 5, stdcall: context, &readers, &stream, -): per
// active channel three entropy readers (NAL_EntropyFloat3_InitReaders, 0x30 bytes).
// 0x0007dc40 (slot 6: context, &dst, &readers, -, -, count, stride): x, y, z of each active
// channel from its readers (NAL_EntropyReader_DecodeFloats, scale block * channel).
// 0x0007df70 (slot 8: context, &cursor, &stream, -): per active channel three
// byte-length-prefixed streams skipped, cursor + 12. 0x0007e030 (slot 9): NAL_Float3_Evaluate
// plus the static streams. All return the group.
uint32_t XB_STDCALL NAL_EntropyFloat3_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t);
uint32_t XB_STDCALL NAL_EntropyFloat3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                   uint32_t, uint32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_EntropyFloat3_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t);
uint32_t XB_STDCALL NAL_EntropyFloat3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                               const int32_t* offsets);
// The quaternion entropy reader (0x40 bytes per channel in the reader array): the current
// value, one entropy reader for each of x, y, z (8 bytes each: no prediction state) and the
// first deltas.
struct QuatReader {
    struct Reader {
        uint32_t ptr;
        uint8_t bit;
        uint8_t mode;
        uint16_t zeros;
    };
    float value[4];
    Reader readers[3];
    int32_t deltas[3];
};
// 0x000843c0 (reader in ECX, callee pops 0x10: frames, scale, stream, bit): reads the headers
// of the x, y, z streams - an optional 11-bit length (+ 0x100 bits; x and y only), the 5-bit
// residual mode (+ 0x20 for a negative scale), the first value (2-bit width index + folded
// sign) and the first delta (the same for negative scales, else a 12/7/5-bit code). A stream
// without a length is decoded once (frames - 2 residuals) to find the next one. value = the
// first values * |scale| / 4 with w = sqrt(|1 - |xyz|^2|); x's mode gets 0x80. Returns q.
QuatReader* XB_THISCALL NAL_QuatReader_Init(QuatReader* q, uint32_t count, float scale, uint32_t ptr, uint8_t bit);
// 0x000866e0 (out in ECX, callee pops 4): q * 32767 truncated to four int16. Returns out.
int16_t* XB_THISCALL Quat_ToShort4(int16_t* out, const float* q);
// 0x00086280 (reader in ECX, callee pops 0x14: out, stride, frames, scale, delta buffer):
// `frames` quaternions as Quat_ToShort4 words, `stride` apart. The frames the reader still holds
// come first (top bits of x's mode: the first value, then one step with the first deltas);
// then the x, y, z residuals go to the buffer (12 bytes per frame), each is added to the
// deltas, and the value is multiplied by the step quaternion (deltas * |scale|, w from the
// unit length).
void XB_THISCALL NAL_QuatReader_DecodeFrames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale,
                                             int32_t* deltas);
// 0x00084970 (reader in ECX, callee pops 0x10: out, stride, frames, scale): NAL_QuatReader_DecodeFrames
// with float quaternions out (16 bytes, `stride` apart); the residuals are decoded into the output
// itself and each product is renormalized (rsqrt + one Newton step).
void XB_THISCALL NAL_QuatReader_DecodeFloatFrames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale);
// The plain quaternion class 0x007c28f8: the plain Float3 methods with 16-byte values (cache and
// output aligned to 16). 0x0007f550 (slot 5) BeginBlock, 0x00081990 (6) DecodeFrames, 0x00081a50
// (7) DecodeBlock, 0x00081b10 (8) SkipBlock (stream + frames * 16 per active channel), 0x00081b90
// (9) Evaluate. All return the group.
uint32_t XB_STDCALL NAL_Quat_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, int32_t frames);
uint32_t XB_STDCALL NAL_Quat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t, uint32_t,
                                          int32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_Quat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, int32_t frames, int32_t stride);
uint32_t XB_STDCALL NAL_Quat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, int32_t frames);
uint32_t XB_STDCALL NAL_Quat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                      const int32_t* offsets);
// 0x00081cb0 (slot 8 of 0x007c2960: context, &cursor, &stream, -): per active channel its
// byte-length-prefixed stream skipped, cursor (aligned to 16) + 0x10. 0x00081d40 (slot 9: context,
// pose, &cursor, defaults, offsets): the cached quaternion or the default at pose + offset (>= 0).
// Both return the group.
uint32_t XB_STDCALL NAL_EntropyQuat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t);
uint32_t XB_STDCALL NAL_EntropyQuat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                             const int32_t* offsets);
// 0x000848a0 / 0x00084e10 (slots 6 / 7 of the float entropy quaternion class 0x007c2960): as
// 0x000861b0 / 0x00086730 with NAL_QuatReader_DecodeFloatFrames (16-byte values, dst/data
// aligned to 16, no delta buffer). Both return the group.
uint32_t XB_STDCALL NAL_EntropyQuat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                      uint32_t, uint32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_EntropyQuat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                     int32_t stride);
// 0x00086010 (out in ECX, callee pops 4): q negated unless w >= 0, then x, y, z * 127 truncated to
// three bytes. Returns out. 0x00085ba0: NAL_QuatReader_DecodeFrames writing those three bytes.
uint8_t* XB_THISCALL Quat_ToByte3(uint8_t* out, const float* q);
void XB_THISCALL NAL_QuatReader_DecodeByteFrames(QuatReader* q, uint8_t* out, int32_t stride, uint32_t count, float scale,
                                                 int32_t* deltas);
// The byte quaternion class 0x007c29c8 (3 bytes cached per channel, no alignment): 0x00085060
// (in in ECX, callee pops 4) Byte3_ToQuat: x, y, z = bytes / 127, w from the unit length; returns
// out. Slots 6 0x00085ad0 / 7 0x000860a0: as the Packed16 ones with NAL_QuatReader_DecodeByteFrames
// (dst / data not aligned, + 3). Slot 8 0x00081fb0 SkipBlock, 9 0x00084f30 Evaluate (Byte3_ToQuat
// or the default), 15 0x00082080 SkipCachedFrame. All slots return the group.
float* XB_THISCALL Byte3_ToQuat(const int8_t* in, float* out);
uint32_t XB_STDCALL NAL_Packed8Quat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, int32_t* deltas,
                                                 uint32_t, uint32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_Packed8Quat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                int32_t stride);
uint32_t XB_STDCALL NAL_Packed8Quat_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t);
uint32_t XB_STDCALL NAL_Packed8Quat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                             const int32_t* offsets);
uint32_t XB_STDCALL NAL_Packed8Quat_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
// 0x000861b0 (slot 6 of 0x007c2a30: context, &dst, &readers, delta buffer, -, count, stride):
// per active channel NAL_QuatReader_DecodeFrames (scale channel * block), dst aligned to 2 and
// + 8. 0x00086730 (slot 7: context, &data, &stream, frames, stride): the same from a stack
// QuatReader over each byte-length-prefixed stream (buffer 0x00906820). Both return the group.
uint32_t XB_STDCALL NAL_Packed16Quat_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, int32_t* deltas,
                                                 uint32_t, uint32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_Packed16Quat_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                int32_t stride);
// 0x000822b0 (in in ECX, callee pops 4): four int16 * 1/32767 (0x007c2410) to out. Returns out.
float* XB_THISCALL Short4_ToQuat(const int16_t* in, float* out);
// 0x00082170 (slot 9 of 0x007c2a30: context, pose, &cursor, defaults, offsets): per channel
// with an offset >= 0 the quaternion at pose + offset - the cursor's int16 words (Short4_ToQuat)
// for an active channel, else the default; the cursor (aligned to 2) moves 8 per active
// channel. Returns the group.
uint32_t XB_STDCALL NAL_Packed16Quat_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor, const uint8_t* defaults,
                                             const int32_t* offsets);
// 0x0008d6b0 (reader in ECX, callee pops 0x1c: out, stride, count, scale, factor, bias, float
// buffer): NAL_EntropyReader_DecodeFloats into the buffer (stride 4), then each
// (value + bias) * factor to int16 at out, `stride` apart, clamped to -32767..32767.
void XB_THISCALL NAL_EntropyReader_DecodeShorts(EntropyReader* reader, int16_t* out, int32_t stride, uint32_t count, float scale,
                                                float factor, float bias, float* buffer);
// 0x0008d510 (cdecl: readers, out, delta buffer, -, count, stride, -, &block scale, ranges,
// &flag): one channel of the int16 position + orientation class 0x007c2c38 (slot 6, 0x00083500).
// Flag set: a quaternion (readers +0, out +8) and three position words (entropy readers
// +0x80/+0x90/+0xa0, out +0x10..: scale block * ranges[0], (v + ranges[1]) * ranges[2]); flag
// clear: two quaternions (readers +0/+0x40, out +0/+8). Both: the word at +0x16 from the reader
// after them, scale ranges[3] * block, factor 5215.1, bias 9.6e-5. Quaternion scale ranges[4] * block.
void XB_CDECL NAL_Packed16EntropyIKSpin_DecodeChannel(uint8_t* readers, uint8_t* out, int32_t* deltas, uint32_t, uint32_t count,
                                             int32_t stride, uint32_t, const float* block_scale, const float* ranges,
                                             const uint8_t* has_position);
// 0x00083500 (slot 6 of 0x007c2c38: context, &dst, &readers, delta buffer, -, count, stride):
// per active channel NAL_Packed16EntropyIKSpin_DecodeChannel with its five floats from stream A (0x14 per
// channel) and its flag byte from stream B (after the block scale); readers 0xc0 and dst 0x1a
// (aligned to 2) per active channel. Returns the group.
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, int32_t* deltas,
                                                  uint32_t unused, uint32_t count, int32_t stride);
// 0x00086920 (cdecl: readers, &stream, frames, -, &block scale, ranges, &flag; writes the length
// byte into its last argument slot, so the C version takes the argument block): the streams of
// one NAL_Packed16EntropyIKSpin_DecodeChannel channel - flag set: a QuatReader (+0) and four entropy
// readers (+0x80..+0xb0); flag clear: two QuatReaders (+0, +0x40) and one entropy reader
// (+0x80). Each stream is byte-length-prefixed; a reader whose address is 0 is skipped.
extern "C" void NAL_Packed16EntropyIKSpin_BeginChannel_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_Packed16EntropyIKSpin_BeginChannel(uint32_t* args);
// 0x00086850 (slot 5 of 0x007c2c38, stdcall: context, &readers, &stream, frames): per active
// channel NAL_Packed16EntropyIKSpin_BeginChannel (readers aligned to 16, 0xc0 apart; flag byte from stream
// B). Returns the group.
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count);
// The int16 position + orientation class 0x007c2c38 (0x50-byte values, 0x1a bytes cached per
// channel). 0x000834d0 (slot 4) Fixup, 0x00083ce0 (12) InitState, 0x00083b60 (13) FreeState,
// 0x00083bb0 (14, body of 0x00083b90) CopyState: as the PosOri ones with 0x50 bytes per channel.
// 0x00083c00 (15) SkipCachedFrame: cursor aligned to 2, + 0x1a per active channel. 0x00083a30 (10,
// stdcall) and 0x00083ae0 (11, cdecl body of 0x00083ab0) only move the static stream pointers.
uint8_t** XB_STDCALL NAL_Packed16EntropyIKSpin_Fixup(const uint8_t* group, uint8_t** cursor, uint32_t);
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_InitState(const uint8_t* group, uint8_t** cursor);
uint8_t** XB_STDCALL NAL_Packed16EntropyIKSpin_FreeState(const uint8_t* group, uint8_t** cursor);
uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_CopyState(const uint8_t* group, uint8_t** dst, uint8_t** src);
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_ApplyStatic(const uint32_t* context, uint8_t*, uint32_t, const int32_t*);
uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_ApplyWraps(const uint32_t* context, uint8_t*, uint8_t*, int32_t, uint32_t, const int32_t*);
// 0x00083600 / 0x00083690 (slots 7 / 8 of 0x007c2c38, stdcall: context, &data, &stream, frames[,
// stride]): no decoding - data (aligned to 2) + 0x1a and one flag byte of stream B per active
// channel, stream A + 0x14 per channel. Return the group.
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t**, uint32_t, int32_t);
uint32_t XB_STDCALL NAL_Packed16EntropyIKSpin_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t**, uint32_t);
// 0x00083860 (cdecl: dst, int16 words, -, &block scale, ranges, &flag): a 0x50-byte pose value
// built in a stack record and copied whole. Flag set: +0x20 quaternion (words 4-7 / 32767), +0x30
// position ((word * 1/ranges[2]) - ranges[1]), +0x3c 1.0, +0x42 word 11; flag clear: +0 and +0x10
// quaternions (words 0-3, 4-7), +0x3c 0, +0x40 word 11. The rest of the record is whatever the
// stack held: the asm entry moves the C frame below a gap and the record is written at the
// original's address (entry ESP passed first).
extern "C" void NAL_Packed16EntropyIKSpin_Dequantize_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_Packed16EntropyIKSpin_Dequantize(uint32_t entry, uint8_t* dst, const int16_t* src, uint32_t,
                                                                     const float*, const float* ranges,
                                                                     const uint8_t* has_position);
// 0x00083720 (slot 9 of 0x007c2c38, stdcall: context, pose, &cursor, defaults, offsets): per
// channel with an offset >= 0 the 0x50-byte value - NAL_Packed16EntropyIKSpin_Dequantize of the cursor's
// 0x1a bytes for an active channel, else the default. Returns the group. Asm entry with a gap, so
// the dequantizer's record lands where the original's does.
extern "C" void NAL_Packed16EntropyIKSpin_Evaluate_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL NAL_Packed16EntropyIKSpin_Evaluate(uint32_t entry, const uint32_t* context, uint8_t* pose,
                                                                       uint8_t** cursor, const uint8_t* defaults,
                                                                       const int32_t* offsets);
// Slots 5-7 of the entropy trajectory class 0x007c2bd0 (stream A: 8 bytes per channel = position
// scale, quaternion scale; stream B: block scale, then aligned to 16 one 0x20-byte static pair per
// active channel; readers 0x70 per channel: a QuatReader and three 16-byte entropy readers at
// +0x40/+0x50/+0x60; values 0x20 = quaternion, position). 0x000856e0 (cdecl: readers, &stream,
// frames, -, &block scale, ranges, -): the x, y, z streams, then the quaternion stream (null
// readers skipped). 0x00085600 (slot 5) calls it per active channel. 0x000857b0 (slot 6: context,
// &dst, &readers, -, -, count, stride): positions by NAL_EntropyReader_DecodeFloats (scale block *
// ranges[0]), quaternions by NAL_QuatReader_DecodeFloatFrames (ranges[1] * block). 0x00085480
// (cdecl: out, &stream, frames, stride, -, &block scale, ranges, -): the same from stack readers
// over the streams; 0x00085930 (slot 7) calls it per active channel. All slots return the group.
void XB_CDECL NAL_EntropyTrajPosOri_BeginChannel(uint8_t* readers, uint8_t** stream, uint32_t count, uint32_t,
                                                 const float* block_scale, const float* ranges, uint32_t);
uint32_t XB_STDCALL NAL_EntropyTrajPosOri_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream,
                                                     uint32_t count);
uint32_t XB_STDCALL NAL_EntropyTrajPosOri_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                       uint32_t, uint32_t count, int32_t stride);
void XB_CDECL NAL_EntropyTrajPosOri_DecodeChannelBlock(uint8_t* out, uint8_t** stream, uint32_t frames, int32_t stride, uint32_t,
                                                       const float* block_scale, const float* ranges, uint32_t);
uint32_t XB_STDCALL NAL_EntropyTrajPosOri_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                      int32_t stride);
// 0x0007cf20 (slot 7 of the entropy Float1 class: context, &data, &stream, frames, stride): per
// active channel a stack entropy reader over its byte-length-prefixed stream, DecodeFloats (scale
// channel * block) into data (+ 4). Returns the group.
uint32_t XB_STDCALL NAL_EntropyFloat1_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                  int32_t stride);
// The packed entropy Float3 classes (8 = vtable 0x007c2688, int8; 16 = 0x007c26f0, int16): stream A
// 12 bytes per channel ({scale, bias, factor}), stream B the block scale. 0x0007ea70 (slot 5, both):
// NAL_EntropyFloat3_InitReaders per active channel (0x30 apart). Slot 7 (0x0007e480 / 0x0007eed0)
// calls the channel block (0x0007e550 / 0x0007efa0, cdecl: out, &stream, frames, stride, -, &block
// scale, ranges, -): each component decoded into the buffer 0x00906820 and stored as
// (v + bias) * factor clamped to -127..127 / -32767..32767; the channel block keeps its temporaries in
// its argument slots (asm entry passes the argument block). Slot 9 (0x0007e8a0 / 0x0007f310):
// NAL_Packed8/16_DequantizeFloat3 or the default. 0x0007f4c0: Packed16 SkipCachedFrame (+ 6).
uint32_t XB_STDCALL NAL_PackedEntropyFloat3_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t);
extern "C" void NAL_Packed16EntropyFloat3_DecodeChannelBlock_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_Packed16EntropyFloat3_DecodeChannelBlock(uint32_t* args);
extern "C" void NAL_Packed8EntropyFloat3_DecodeChannelBlock_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_Packed8EntropyFloat3_DecodeChannelBlock(uint32_t* args);
uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                          int32_t stride);
uint32_t XB_STDCALL NAL_Packed8EntropyFloat3_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                         int32_t stride);
uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                                       const uint8_t* defaults, const int32_t* offsets);
uint32_t XB_STDCALL NAL_Packed8EntropyFloat3_Evaluate(const uint32_t* context, uint8_t* pose, uint8_t** cursor,
                                                      const uint8_t* defaults, const int32_t* offsets);
uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);
// The entropy Float4 classes (stream A 4 bytes per channel; the packed one 0xc): 0x00080d60 (cdecl:
// readers, &stream, ...): four entropy readers (0x10 apart, null addresses skipped) over the
// byte-length-prefixed streams; keeps length bytes in its &stream slot (argument block entry);
// returns &stream. Slot 5 0x0007f9a0 / 0x00080070 (packed) call it per active channel (0x40
// apart). Slot 7 0x0007fbe0 -> 0x0007fcb0 (cdecl: out, &stream, frames, stride, -, &block scale,
// ranges, -): four components by DecodeFloats from stack readers (scale block * ranges[0]); data
// + 0x10. Slot 8 0x0007fe20: NAL_SkipPrefixedStreams4 per active channel, cursor + 0x10.
extern "C" void NAL_EntropyFloat4_InitReaders_entry(void);
extern "C" PLATFORM_HIDDEN uint8_t** XB_CDECL NAL_EntropyFloat4_InitReaders(uint32_t* args);
uint32_t XB_STDCALL NAL_EntropyFloat4_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count);
uint32_t XB_STDCALL NAL_PackedEntropyFloat4_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream,
                                                       uint32_t count);
extern "C" void NAL_EntropyFloat4_DecodeChannelBlock_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_EntropyFloat4_DecodeChannelBlock(uint32_t* args);
uint32_t XB_STDCALL NAL_EntropyFloat4_DecodeBlock(const uint32_t* context, uint8_t** data, uint8_t** stream, uint32_t frames,
                                                  int32_t stride);
uint32_t XB_STDCALL NAL_EntropyFloat4_SkipBlock(const uint32_t* context, uint8_t** cursor, uint8_t** stream, uint32_t);
// 0x000842e0 (slot 5 of the entropy quaternion classes 0x007c2960, 0x007c29c8, 0x007c2a30;
// stdcall: context, &readers, &stream, frames): per active channel a QuatReader (array aligned
// to 16, 0x40 apart; null-checked) over its byte-length-prefixed stream, scale = channel scale
// (stream A) * block scale (stream B). Returns the group.
uint32_t XB_STDCALL NAL_EntropyQuat_BeginBlock(const uint32_t* context, uint8_t** readers, uint8_t** stream, uint32_t count);

// 0x0007ca40 / 0x0007fa70 (stdcall: context, &dst, &readers, -, -, count, stride): the
// one-/four-component entropy float classes' DecodeFrames - per active channel the readers
// decode into dst (scale channel * block for Float1, block * channel for Float4); readers move
// 0x10 per component, dst 4 per component; stream A moves 4 per channel. Return the group.
uint32_t XB_STDCALL NAL_EntropyFloat1_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                   uint32_t, uint32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_EntropyFloat4_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers, uint32_t,
                                                   uint32_t, uint32_t count, int32_t stride);
// 0x0007e290 / 0x0007ecd0 (cdecl, 10 words: readers, out, float buffer, -, count, stride, -,
// &block scale, ranges, -): one packed entropy Float3 channel as int8 (+-127) / int16 (+-32767):
// per component the buffer decoded with scale block * ranges[0], then (v + ranges[1]) * ranges[2]
// clamped and truncated. The original keeps temporaries in its argument slots.
void XB_CDECL NAL_Packed8EntropyFloat3_DecodeChannelFrames(uint8_t* readers, uint8_t* out, float* buffer, uint32_t,
                                                           uint32_t count, int32_t stride, uint32_t,
                                                           const float* block_scale, const float* ranges, uint32_t);
void XB_CDECL NAL_Packed16EntropyFloat3_DecodeChannelFrames(uint8_t* readers, uint8_t* out, float* buffer, uint32_t,
                                                            uint32_t count, int32_t stride, uint32_t,
                                                            const float* block_scale, const float* ranges, uint32_t);
// 0x0007e1a0 / 0x0007ebd0 (stdcall, as above; arg 4 = the float buffer): their DecodeFrames.
// dst aligned to 2 for int16; readers +0x30 and dst +3/+6 per active channel, stream A +12 per
// channel.
uint32_t XB_STDCALL NAL_Packed8EntropyFloat3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers,
                                                          uint32_t buffer, uint32_t arg5, uint32_t count, int32_t stride);
uint32_t XB_STDCALL NAL_Packed16EntropyFloat3_DecodeFrames(const uint32_t* context, uint8_t** dst, uint8_t** readers,
                                                           uint32_t buffer, uint32_t arg5, uint32_t count, int32_t stride);

// 0x00080be0 (thiscall, callee pops 8): out = in + self (four floats). Returns out.
float* XB_THISCALL NAL_Float4_AddTo(const float* self, float* out, const float* in);
// 0x00080c60 / 0x000828d0: a block of 12 / 8-byte channels skipped in the streams.
void XB_CDECL NAL_Float3_SkipFixedBlock(const uint32_t* context);
void XB_CDECL NAL_Float2_SkipFixedBlock(const uint32_t* context);
// 0x00080cb0: the group's channels (16 bytes each) copied from *src to *dst (both aligned to 4
// first, both advanced).
void XB_CDECL NAL_Float4_CopyChannels(const uint8_t* group, uint8_t** dst, uint8_t** src);
// 0x00081f50 (stdcall): one cached frame of 16-byte channels skipped (cursor aligned to 16).
uint32_t XB_STDCALL NAL_Packed16_SkipCachedFrame(const uint32_t* context, uint8_t** cursor);

}  // namespace game
