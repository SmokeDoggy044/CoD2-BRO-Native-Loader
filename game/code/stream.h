#pragma once

// Byte streams (a vtable with Read at slot 1 and a failed/EOF test at slot 2) and the
// typed readers on them; the effect files (FX_LoadEffectFile) are read this way.
// Values are byte-swapped when 0x00902269 is set (big-endian data).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// {vtable, data, size, position, flag}
struct MemStream {
    uint32_t vtable;
    const uint8_t* data;  // +0x04
    int32_t size;         // +0x08
    int32_t position;     // +0x0c
    uint8_t flag;         // +0x10
};

// 0x000554b0: a stream over `size` bytes at `data` (vtable 0x007bad98).
MemStream* XB_THISCALL MemStream_Init(MemStream* stream, const uint8_t* data, int32_t size);
// 0x00055590 (vtable slot 1): copies `count` bytes unless the stream failed (slot 2)
// or they run past the end; nothing is written then.
void XB_THISCALL MemStream_Read(MemStream* stream, uint8_t* out, int32_t count);
// 0x000557c0, 0x00055800: four bytes. When the read writes nothing the result is the
// original's uninitialized local, which holds the stream pointer (its PUSH ECX).
uint32_t XB_THISCALL Stream_ReadInt(uint32_t* stream);
uint32_t XB_THISCALL Stream_ReadUInt(uint32_t* stream);
// 0x00055840: the same bytes returned on the x87 stack.
float XB_THISCALL Stream_ReadFloat(uint32_t* stream);
// 0x00055880: a length word (into *length), then that many bytes one at a time, kept
// up to `capacity` and null-terminated when there is room.
void XB_THISCALL Stream_ReadString(uint32_t* stream, char* out, int32_t* length, int32_t capacity);

// 0x000554e0: frees the data when the stream owns it (+0x10), then the vtable becomes
// the base stream's (0x007bad80).
void XB_THISCALL MemStream_Destroy(MemStream* stream);
// 0x000555f0 (vtable slot 0): MemStream_Destroy, then Z_Free of the stream with flag 1.
MemStream* XB_THISCALL MemStream_DeletingDestroy(MemStream* stream, uint32_t flags);

}  // namespace game
