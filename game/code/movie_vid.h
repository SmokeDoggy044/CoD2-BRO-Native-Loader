#pragma once

// The movie player's MPEG-2 style video decoder: motion compensation (the McPut/McAvg
// table at 0x008c0208), block output after the IDCT, coefficient and header readers.
// The pixel code is MMX: the C versions compute the same lanes and leave the MMX
// registers the original writes holding its final values (the x87 stack is compared;
// nothing here runs EMMS, the frame loop does).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Motion compensation, one per (put/avg, 16/8 wide, half-pel x/y): rows of `width` pixels
// from src to dst, both stepping by stride; Avg averages the prediction into dst (PAVGB).
// Reached through MovieVid_mcFuncs (0x008c0208: put 16/8 at +0x00/+0x10, avg at
// +0x20/+0x30, each four entries: full, X, Y, XY half-pel).
void XB_CDECL MovieVid_McAvg16(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg8(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut16(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut8(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg16X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg8X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut16X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut8X(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg16Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg8Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut16Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut8Y(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg16XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McAvg8XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut16XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
void XB_CDECL MovieVid_McPut8XY(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);

// 0x000cf8c0: stores an 8x8 block of 16-bit IDCT output (64 words) as bytes, saturated;
// returns block (EAX untouched; MovieVid_IdctPutBlock clears it through EAX).
const int16_t* XB_REGS3 XB_CDECL MovieVid_PutBlock(const int16_t* block, int32_t stride, uint8_t* dst);
// 0x000cf950: adds an 8x8 block of 16-bit residuals to the pixels at dst (saturated).
void XB_REGS3 XB_CDECL MovieVid_AddBlock(uint8_t* dst, int32_t stride, const int16_t* block);
// 0x000cfa70: adds a DC-only residual, (block[0] + 64) >> 7, to the 8x8 pixels at dst.
void XB_REGS3 XB_CDECL MovieVid_AddBlockDCOnly(uint8_t* dst, const int16_t* block, int32_t stride);
// 0x000d02c0: IDCT (0x000cfb20) of an intra block, stored at dst; clears the block.
int16_t* XB_CDECL MovieVid_IdctPutBlock(int16_t* block, uint8_t* dst, int32_t stride);
// 0x000d0330: IDCT of a residual block added to dst (a DC-only shortcut when last == 0x81
// and the block's first byte is not 0x4X); clears the block.
void XB_CDECL MovieVid_IdctAddBlock(int32_t last, int16_t* block, uint8_t* dst, int32_t stride);

// The decoder state (`dec`, ECX) is not a struct here yet; fields are named where used
// (+0x140/+0x14c/+0x150 bitstream pointer/window/count, +0x154 coefficient block, ...).

// 0x000cbf60 / 0x000cc250: an intra block's AC coefficients (B14 / B15 VLC tables), times
// quant, into the block (+0x154) in scan order (+0x2240), with MPEG-2 mismatch control.
void XB_THISCALL MovieVid_ReadIntraCoeffs(uint8_t* dec, const uint16_t* quant);
void XB_THISCALL MovieVid_ReadIntraCoeffsAlt(uint8_t* dec, const uint16_t* quant);
// 0x000cc510: a non-intra block's coefficients; returns the last scan index reached.
int32_t XB_THISCALL MovieVid_ReadNonIntraCoeffs(uint8_t* dec, const uint16_t* quant);
// 0x000ce7d0: rebuilds the 32 scaled copies of quant matrix `index` (+0x98 + 64 * index,
// to +0x160 + 0x1000 * index) when the quantiser scale type (+0x2246) changed.
void XB_THISCALL MovieVid_BuildQuantTables(uint8_t* dec, int32_t index);
// 0x000ce850: macroblock_type for the picture type, plus motion/DCT type bits.
uint32_t XB_FASTCALL MovieVid_ReadMacroblockType(uint8_t* dec);
// 0x000ceb80: coded_block_pattern.
uint32_t XB_FASTCALL MovieVid_ReadCodedBlockPattern(uint8_t* dec);
// 0x000cec20 / 0x000ced80: the intra DC differential, luma / chroma.
int32_t XB_FASTCALL MovieVid_ReadLumaDCDiff(uint8_t* dec);
int32_t XB_FASTCALL MovieVid_ReadChromaDCDiff(uint8_t* dec);
// 0x000ceef0: one intra block of component cc (0 Y, 1 Cb, 2 Cr): DC, AC, IDCT into dst.
void XB_THISCALL MovieVid_DecodeIntraBlock(uint8_t* dec, int32_t cc, uint8_t* dst, int32_t stride);

// Motion prediction (motion: ref planes [3] at +0, pmv[2][2] at +0x20, f_code[2] at +0x30),
// through a McPut/McAvg table (put 16 at [0..3], 8 at [4..7]).
using MovieVidMcFn = void XB_CDECL(uint8_t* dst, const uint8_t* src, int32_t stride, uint32_t rows);
// 0x000cc840: frame prediction with a new vector.
void XB_THISCALL MovieVid_PredictFrameMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table);
// 0x000cca70: field prediction in a frame picture (two vectors, field selects).
void XB_THISCALL MovieVid_PredictFieldMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table);
// 0x000ccef0: dual prime (always the global put/avg tables; the table argument is unused).
void XB_THISCALL MovieVid_PredictDualPrime(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table);
// 0x000cd4f0: frame prediction reusing pmv[0] (skipped macroblocks).
void XB_THISCALL MovieVid_PredictLastMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table);
// 0x000cd630: zero vector (clears pmv).
void XB_THISCALL MovieVid_PredictZeroMotion(uint8_t* dec, uint8_t* motion, MovieVidMcFn* const* table);

// 0x000cd6e0: decodes a slice's macroblocks (slice header, macroblock loop, skipped
// macroblocks). Returns 1 on a bad start, else 0 (+0x2230 = 1 once the picture is done).
uint32_t XB_FASTCALL MovieVid_DecodeMacroblocks(uint8_t* dec);
// 0x000d0af0: converts 16 rows of a decoded macroblock row (planes: Y, U, V) to 32-bit
// pixels at dst, width pixels per row.
void XB_CDECL MovieVid_ConvertMacroblockRow(uint8_t* const* planes, uint8_t* dst, int32_t width);

}  // namespace game
