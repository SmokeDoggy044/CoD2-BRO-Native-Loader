#pragma once

// The NAL entropy readers' mode decoders (docs/nal_animation.md, "Stream formats"): table
// 0x008bb4a0, indexed by the reader's mode (0x00-0x3f; 0x20 and up for a negative scale).
// Each takes (reader, dst, stride, count), writes `count` words `stride` bytes apart -
// small values and runs of zeros from a prefix code - and returns the part of the last
// zero run that did not fit.

#include <cstdint>

#include "compiler.h"
#include "nal_codec.h"

namespace game {

using EntropyModeDecoder = uint32_t XB_CDECL (*)(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);

// The 64 entries in the title's order.
extern const EntropyModeDecoder kEntropyModeDecoders[64];

// 0x0008f440 (mode 0x00): only zeros, nothing read.
uint32_t XB_CDECL NAL_EntropyMode00(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
// 0x0008f460 (0x01): 0 or +-1.
uint32_t XB_CDECL NAL_EntropyMode01(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
// 0x0008f560-0x00092f80 (0x02-0x1d, 0x2f-0x3d): the codes are described in nal_entropy.cpp.
uint32_t XB_CDECL NAL_EntropyMode02(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode03(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode04(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode05(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode06(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode07(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode08(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode09(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode0A(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode0B(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode0C(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode0D(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode0E(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode0F(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode10(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode11(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode12(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode13(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode14(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode15(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode16(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode17(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode18(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode19(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode1A(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode1B(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode1C(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode1D(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode2F(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode30(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode31(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode32(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode33(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode34(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode35(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode36(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode37(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode38(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode39(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode3A(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode3B(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode3C(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
uint32_t XB_CDECL NAL_EntropyMode3D(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);
// 0x00093090 (0x1e, 0x1f, 0x3e, 0x3f): a copy of mode 0.
uint32_t XB_CDECL NAL_EntropyMode1E(EntropyReader* reader, uint32_t* dst, int32_t stride, uint32_t count);

}  // namespace game
