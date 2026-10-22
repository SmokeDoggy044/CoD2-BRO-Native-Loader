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

// A relocation table in a loaded pack: entries[0] = bit count, entries[1..count-1] =
// offsets of relocation chains (Pack_RelocateChain).
struct PackRelocTable {
    uint32_t count;
    uint32_t entries; // an offset from the pack base until relocated
};

// 0x00055b40: one chain word. Sign bit set = the last word: delta = the word (bit 31
// kept only with bit 30). Else delta = the low (31 - bits) bits, sign-extended, and
// next = the remaining bits, the byte distance to the next word.
void XB_CDECL Pack_DecodeRelocWord(uint32_t word, uint32_t bits, uint32_t* delta, uint32_t* next);
// 0x00055ab0: each word of a chain becomes delta + its own address. EAX = first word,
// EBX = bits: entered through Pack_RelocateChain_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL Pack_RelocateChain(uint32_t* word, uint32_t bits);
extern "C" void Pack_RelocateChain_entry(void);
// 0x00055b00: relocates the table's entries pointer, then each chain; returns count.
uint32_t XB_THISCALL Pack_RelocateTable(PackRelocTable* self, uint32_t base);

// 0x00024ed0..0x00025150 (ECX = hash or 0, EAX = name hashed with HashStr_Hash when ECX is
// 0, EDI = zone): the zone map at its offset (UintMap +0, count +8, entries +0xc, 8 bytes
// each): the entry's value address (+4); 0 when missing; index past the count -> entry 0.
#define ZONE_MAP_LOOKUP_DECL(name)                                                                \
    extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL name(uint32_t hash, const char* key, const uint8_t* zone); \
    extern "C" void name##_entry(void);
ZONE_MAP_LOOKUP_DECL(ZoneTextureMap_Lookup)   // 0x00024ed0, +0x08
ZONE_MAP_LOOKUP_DECL(ZoneFontMap_Lookup)      // 0x00024f20, +0x18
ZONE_MAP_LOOKUP_DECL(ZoneMap28_Lookup)        // 0x00024f70, +0x28 (was FUN_00024f70)
ZONE_MAP_LOOKUP_DECL(ZoneMeshFileMap_Lookup)  // 0x00024fc0, +0x38 (was FUN_00024fc0)
ZONE_MAP_LOOKUP_DECL(ZoneAnimMap_Lookup)      // 0x00025010, +0x88
ZONE_MAP_LOOKUP_DECL(ZoneMap98_Lookup)        // 0x00025060, +0x98 (was FUN_00025060)
ZONE_MAP_LOOKUP_DECL(ZoneMapA8_Lookup)        // 0x000250b0, +0xa8 (was FUN_000250b0)
ZONE_MAP_LOOKUP_DECL(ZoneSkelMap_Lookup)      // 0x00025100, +0xb8
ZONE_MAP_LOOKUP_DECL(ZoneEffectMap_Lookup)    // 0x00025150, +0xc8
#undef ZONE_MAP_LOOKUP_DECL

// Memory contexts keep a stack of zone ids (+0x374 count, +0x378 ids, at most 0x80; -1
// marks a frame). 0x0028f6c0 (context ESI, id EDI): a frame mark, then the id (or, when
// not listed and the context has packs (+0x18), the pack's ids: MemContext_FindById +
// Pak_PushZoneIds_r). 0x0028c690 (context EDX): drops the top frame, returns its last id.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL MemContext_Push(uint8_t* context, uint32_t id);
extern "C" void MemContext_Push_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL MemContext_Pop(uint8_t* context);
extern "C" void MemContext_Pop_entry(void);
// 0x0028ff70 (context EAX, out on the stack): the context's +0x28/+0x2c ids pushed on the
// global context (0x0095f638), then every distinct id on its stack, top first, appended
// to out {count, ids[0x20]}; both frames popped.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL MemContext_ListActive(uint8_t* context, uint32_t* out);
extern "C" void MemContext_ListActive_entry(void);
// 0x0028fb10 (zone EDX, out ESI): without a global context just the zone; else the zones
// Stream_CollectSearchZones_r finds, then the global context's +0x28/+0x2c ids not seen.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_ListSearchZones(uint32_t zone, uint32_t* out);
extern "C" void Stream_ListSearchZones_entry(void);
// 0x00290620 (stdcall streams, zone, name (text at +4), active, unused): the texture
// value from the first zone whose texture map has the file's hashed name: the zones of
// the active memory contexts when `active` (byte), else the zone's search list; 0 while
// 0x0095f634 is set. 0x00290400 (hashed name in EBX): the same for mesh files (0x0095f635).
uint32_t XB_STDCALL Stream_FindTexture(uint32_t streams, uint32_t zone, const char* name, uint32_t active,
                                       uint32_t unused);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_FindMeshFile(const uint32_t* hashed, uint32_t streams,
                                                                 uint32_t zone, uint32_t active, uint32_t unused);
extern "C" void Stream_FindMeshFile_entry(void);
// 0x002902a0 / 0x00290350 (was FUN_00290350): the same for anims / the +0xa8 map (0x0095f636).
// 0x00290150 (hashed in EBX): effects, no guard; the loaded zones (Pak_CollectLoadedZoneIds)
// first, then the active or searched ones. 0x002901f0 (active flag in EAX; streams, zone, hashed,
// ?): skeletons; the loaded zones instead when the list has at most two.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_FindAnim(const uint32_t* hashed, uint32_t streams, uint32_t zone,
                                                             uint32_t active, uint32_t unused);
extern "C" void Stream_FindAnim_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_FindMapA8(const uint32_t* hashed, uint32_t streams, uint32_t zone,
                                                              uint32_t active, uint32_t unused);
extern "C" void Stream_FindMapA8_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_FindEffect(const uint32_t* hashed, uint32_t streams, uint32_t zone,
                                                               uint32_t active, uint32_t unused);
extern "C" void Stream_FindEffect_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_FindSkel(uint32_t active, uint32_t streams, uint32_t zone,
                                                             const uint32_t* hashed, uint32_t unused);
extern "C" void Stream_FindSkel_entry(void);
// 0x00290860 (name in EBX; data, size, memory context id or -1, kind): the texture file
// loaded under its base name (nglLoadTexture), "no_" names loaded as "po_"; with a context
// id, inside that context (0x0095f5d0 set meanwhile; returns MemContext_Pop then).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_LoadTexture(char* name, uint8_t* data, uint32_t size,
                                                                uint32_t context_id, uint32_t kind);
extern "C" void Stream_LoadTexture_entry(void);
// 0x00025330 (was FUN_00025330; src in EAX, dst in ESI, flag on the stack; returns dst):
// FixedStr_BaseName for 0x200-byte fixed strings (u16 length at +0x1fe).
extern "C" PLATFORM_HIDDEN char* XB_CDECL FixedStr512_BaseName(const char* src, char* dst, uint32_t flag);
extern "C" void FixedStr512_BaseName_entry(void);
// 0x0028eeb0 (name in EAX, out in EDI; returns out): the hashed name (HashStr_Init) of the
// file name's base name.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL Stream_HashFileName(const char* name, uint32_t* out);
extern "C" void Stream_HashFileName_entry(void);

}  // namespace game
