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
// 0x00290920 (data, size, context id or -1 as Stream_LoadTexture): the mesh file (memory
// XPhysicalProtect 0x400, nglLoadMeshFile under its base name), its meshes flagged 0x1000000.
uint32_t XB_CDECL Stream_LoadMeshFile(const char* name, uint8_t* data, uint32_t size, uint32_t context_id);
// 0x0028fbe0 (name in EBX; streams, font text, zone id): the font loaded (nglLoadFont) and
// stored in the zone font map; its slot in the renderer font table (R_FindFont of the
// first 31 chars) set and marked. Returns that slot (8 = none), 0 when not loaded.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_RegisterFont(const char* name, uint32_t streams, char* text,
                                                                 uint32_t zone_id);
extern "C" void Stream_RegisterFont_entry(void);
// File-type load handlers (name, data, size, zone or context id): 0x00290e10 Ifl /
// 0x00290e30 Ddsmp / 0x00290e50 Dds = Stream_LoadTexture kind 3 / 1 / 0; 0x0028fc60 Fdf =
// Stream_RegisterFont; 0x0028ee00 Stb = StringTable_Load(0x0095f5d8, data, zone); 0x0028ee20
// Db / 0x0028ee50 Ib / 0x0028ee80 Bmips: the pack relocated (table at +0x18 / +0xe8 / +0x14)
// and stored (0x0095f5a8 +4 + zone * 4 / streams +0x5c + zone * 4 / streams +0x58, zone at
// +8); these return the zone.
uint32_t XB_CDECL Stream_LoadIfl(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadDdsmp(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadDds(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadFdf(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadStb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadDb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadIb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadBmips(char* name, uint8_t* data, uint32_t size, uint32_t zone);
// 0x0028d120 Aitb: pack (+0x18) relocated, stored in 0x0095f718 +4 + zone * 4; returns the
// zone. 0x0028d0e0 Lgrid: +0/+8/+0x10/+0x18 made absolute, stored in 0x0095f6a0 +4 +
// zone * 4; returns the data.
// 0x0028e930 / 0x0028e7e0: the texture (name +0x54) / mesh file (name +0) stored in the
// texture (+8) / mesh file (+0x38) map of the zone on top of the global memory context;
// returns the previous entry (texture: 0 when the name is not in the map).
uint32_t XB_STDCALL Stream_AddTextureToZone(uint32_t streams, uint32_t texture);
uint32_t XB_STDCALL Stream_AddMeshFileToZone(uint32_t streams, uint32_t file);
// 0x0028e880: meshes (name where +0 points, map +0x28). 0x0028e790 / 0x0028e690 (object in
// ESI, streams on the stack): anims (name +0x10, map +0x88) / skeletons (name +8, map +0xb8).
// These store unchecked (a missing name writes through null, as the original).
uint32_t XB_STDCALL Stream_AddMeshToZone(uint32_t streams, uint32_t mesh);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_AddAnimToZone(uint32_t anim, uint32_t streams);
extern "C" void Stream_AddAnimToZone_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_AddSkelToZone(uint32_t skel, uint32_t streams);
extern "C" void Stream_AddSkelToZone_entry(void);
// 0x00025b50 / 0x00025bf0 / 0x00025c90 / 0x00025e70 / 0x00025f10 (vtable slot 3, thiscall):
// the Stream_Add*ToZone above while a zone is active on the global context (byte +9 set);
// otherwise the wrapped manager (+4) adds it; +9 clear: returns the object.
uint32_t XB_THISCALL ZoneTextureManager_Add(const uint8_t* self, uint32_t object);
uint32_t XB_THISCALL ZoneMeshFileManager_Add(const uint8_t* self, uint32_t object);
uint32_t XB_THISCALL ZoneMeshManager_Add(const uint8_t* self, uint32_t object);
uint32_t XB_THISCALL ZoneAnimManager_Add(const uint8_t* self, uint32_t object);
uint32_t XB_THISCALL ZoneSkelManager_Add(const uint8_t* self, uint32_t object);
uint32_t XB_CDECL Stream_LoadAitb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint8_t* XB_CDECL Stream_LoadLgrid(char* name, uint8_t* data, uint32_t size, uint32_t zone);
// 0x00290730 Xmb: pack relocated, registered (0x0023cfb0, EBX = data), stored in
// 0x0095f6ac +4 + zone * 4. 0x002906f0 Lpth: +0x10..+0x1c made absolute; type 9 = the path
// nodes (g_pathNodeBlocks +0x194, Path_InitAfterLoad). 0x002907f0 Xbskel: 0x0008e130(data)
// inside the memory context (as Stream_LoadTexture).
uint32_t XB_CDECL Stream_LoadXmb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadLpth(char* name, uint8_t* data, uint32_t size, uint32_t zone);
uint32_t XB_CDECL Stream_LoadXbskel(char* name, uint8_t* data, uint32_t size, uint32_t context_id);
// 0x00290760 Xbanim: 0x0008dbb0(hashed base name, data) in the memory context, counter
// 0x008d508c + 1. 0x00291920 Zbb: Stream_ParseZbb(0x0095f640, data, zone). 0x002911c0 Bpe:
// FX_LoadEffectFromStream (EAX size, ECX data, EBX context; streams, name) in the context.
uint32_t XB_CDECL Stream_LoadXbanim(char* name, uint8_t* data, uint32_t size, uint32_t context_id);
uint32_t XB_CDECL Stream_LoadZbb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_LoadBpe(uint32_t entry, char* name, uint8_t* data, uint32_t size,
                                                            uint32_t context_id);
extern "C" void Stream_LoadBpe_entry(void);
// 0x00025330 (was FUN_00025330; src in EAX, dst in ESI, flag on the stack; returns dst):
// FixedStr_BaseName for 0x200-byte fixed strings (u16 length at +0x1fe).
extern "C" PLATFORM_HIDDEN char* XB_CDECL FixedStr512_BaseName(const char* src, char* dst, uint32_t flag);
extern "C" void FixedStr512_BaseName_entry(void);
// 0x0028eeb0 (name in EAX, out in EDI; returns out): the hashed name (HashStr_Init) of the
// file name's base name.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL Stream_HashFileName(const char* name, uint32_t* out);
extern "C" void Stream_HashFileName_entry(void);

// The streaming view position (CL_Frame, ClientSpawn, G_InitGame, Stream_ParseZbb): zone selectors (manager +4 +
// index * 4, linked through +0x3c from +0x1cc) pick a volume point from the player's cell and flag box sets;
// changes recompute every zone's load distance.
// 0x00024cb0 (point in EAX, bounds in ECX: min vec4, max vec4): min.x <= x <= max.x and y, z inside (SSE lane
// compares: NaN in x is outside, in y or z inside). 1 or 0 in EAX.
uint32_t XB_REGS3 XB_CDECL Stream_BoundsContainPoint(const float* point, uint32_t unused_edx, const float* bounds);
// 0x00024d30 (volume in ESI, point in EDX): the volume's point (+0x28 list of vec4 pointers, count +0x24) nearest
// to `point` ((dx*dx + dy*dy) + dz*dz, strictly below FLT_MAX), or null.
extern "C" PLATFORM_HIDDEN const float* XB_CDECL Stream_NearestPoint(const float* point, const uint8_t* volume);
extern "C" void Stream_NearestPoint_entry(void);
// 0x00024da0 (point in EBX, volumes on the stack, ret 4): for each volume (+0x28 list, count +0x24) whose bounds
// contain the point, its nearest point; returns the last one found (0 if none).
extern "C" PLATFORM_HIDDEN const float* XB_CDECL Stream_PickVolumePoint(const float* point, const uint8_t* volumes);
extern "C" void Stream_PickVolumePoint_entry(void);
// 0x00024df0 (box set in EDI, point on the stack, ret 4): 1 in AL if any of its boxes (+8, 32 bytes each, count +4)
// contains the point.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL Stream_PointInAnyBox(const uint8_t* set, const float* point);
extern "C" void Stream_PointInAnyBox_entry(void);
// 0x0028fd00 (selector in EBX; mask low, mask high, distance list on the stack, ret 0xc): per zone of the selector
// (+0x14 list, count +0x10) the next listed distance if its mask bit is set, else FLT_MAX (1.0 with
// g_connectpaths), set with Zone_SetDistance_r on its zone +0x24. `args` = the three stack words. Returns the count.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_SetZoneDistancesFromMask(const uint8_t* selector,
                                                                            const uint32_t* args);
extern "C" void Stream_SetZoneDistancesFromMask_entry(void);
// 0x0028fdb0 (stdcall): Pak_ResetZoneDistances, then per selector the distances of its picked point (+0x44) and of
// each set box flag, and each flagged set's linked zones (12-byte entries: zone +4, distance +8; 1.0 with
// g_connectpaths) at most at their current +0x30 (-1 = FLT_MAX) on every sub-zone.
void XB_STDCALL Stream_UpdateZoneDistances(uint8_t* manager);
// 0x002914f0 (stdcall; cell -1 = none): stores the origin (+0x1b0) and cell (+0x1c4), re-picks each selector's
// point and box flags and runs Stream_UpdateZoneDistances when anything changed or `force` (byte) is set. The
// changed flag lives in the manager argument's slot, as in the original.
void XB_STDCALL Stream_SetViewPosition(uint8_t* manager, uint32_t cell, const float* origin, uint32_t force);

// 0x0028ca10 (stream system in ECX; zone, distance, force byte on the stack, ret 0xc): unless forced, a distance
// above the zone's current +0x30 changes nothing; otherwise +0x30 = min(distance, +0x30) and every sub-zone (+0x20
// list, count +0x1c) gets it too, -1 handed on as FLT_MAX (kept in the force argument's slot, as in the original).
void XB_REGS3 XB_STDCALL Zone_SetDistance_r(uint32_t unused_eax, uint32_t unused_edx, uint32_t system, uint8_t* zone,
                                            float distance, uint32_t force);
// 0x0028ca90 (stream system in ESI; both byte on the stack, ret 4): every zone of the pak (system +0x18, list +0x14,
// count +0x10) gets +0x30 = FLT_MAX, and +0x34 too when `both` is set. Returns the zone count (0 if none).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Pak_ResetZoneDistances(const uint8_t* system, uint32_t both);
extern "C" void Pak_ResetZoneDistances_entry(void);

// 0x0028c780 / 0x0028c7d0 (ECX passed down; zone on the stack, ret 4): the zone's reference count +0x2c - 1 / + 1,
// then the same for every sub-zone (+0x20 list, count +0x1c).
void XB_REGS3 XB_STDCALL Zone_Release_r(uint32_t unused_eax, uint32_t unused_edx, uint32_t passed, uint8_t* zone);
void XB_REGS3 XB_STDCALL Zone_AddRef_r(uint32_t unused_eax, uint32_t unused_edx, uint32_t passed, uint8_t* zone);
// 0x0028c820 (zone in ESI; pak on the stack, ret 4): the first sub-zone with no pak slot (+0x28 = -1), an empty pak
// slot (pak +0x38 + 4 * slot) or a pak in state 3 (+0xdc); null if none.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Zone_FindUnloadedChild(const uint8_t* zone, const uint8_t* pak);
extern "C" void Zone_FindUnloadedChild_entry(void);
// 0x0028d480 (memory in ECX, bit words in EDI): how many of its +0x10 slots are set in either bit array (EDI +0 or
// +8, two words each).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL StreamMem_CountUsed(const uint8_t* memory, const uint32_t* bits);
extern "C" void StreamMem_CountUsed_entry(void);
// 0x002984f0 (zone table in EAX; hash on the stack, ret 4): the zone with that hash from the first of the 99 maps
// (table +4, map +8, lists +0x14, count +0x10) that has one; null if none.
uint8_t* XB_REGS1 XB_STDCALL Zone_FindByHash(const uint8_t* table, uint32_t hash);

// Stream memory: slot bit arrays at +0 and +8 (two words each), slot count +0x10, slot size +0x14, base +0x18.
// 0x0028d150 (memory in EDX; out on the stack, ret 4): out = {0, free, 0, 0} with free = 1 per slot set in both
// arrays plus 0.5 once for any slots set in only one. Returns out.
extern "C" PLATFORM_HIDDEN float* XB_CDECL StreamMem_CountFree(const uint32_t* memory, float* out);
extern "C" void StreamMem_CountFree_entry(void);
// 0x0028d390 (bit arrays in EDI; memory, out, n on the stack, ret 0xc): the n-th slot set in both arrays, else the
// n-th (counting on) set in only one; out = {address (half-slot offset when only the second array has it), size
// (halved for a half slot)}. Returns out.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL StreamMem_GetBlock(const uint32_t* bits, const uint8_t* memory,
                                                                uint32_t* out, int32_t n);
extern "C" void StreamMem_GetBlock_entry(void);

// 0x0028c870 (zone in ECX; result in XMM0): the zone's load priority, cached per frame (+0x40 = frame 0x008d9d38,
// +0x3c = value): FLT_MAX when its distance +0x34 is -1, or +0x30 is -1 and +0x34 FLT_MAX; else +0x34 (+0x30 when +0x34
// is FLT_MAX), 0 for zones in states 0, 1, 3, 7 that have a pak slot (+0x28). Callers keep XMM4 and ECX across it.
extern "C" PLATFORM_HIDDEN float XB_CDECL Zone_UpdatePriority(uint8_t* zone);
extern "C" void Zone_UpdatePriority_entry(void);

// --- Streaming module push (2026-10-28) ---

// Zone*Manager vtable slot 0 (thiscall, flags on the stack, ret 4): the deleting destructor of the managers that
// wrap the previous one (+4) while a zone loads. The texture, mesh file and mesh managers hand the previous one back
// to ngl (nglSet*Manager); the clip, clip set, anim and skel managers store it in their global (0x00909830,
// 0x00909828, 0x0090982c, 0x00909838). Then the base vtable, Mem_AlignedFree when flags bit 0. Returns the object.
uint8_t* XB_THISCALL ZoneTextureManager_Destruct(uint8_t* self, uint32_t flags);    // 0x000258d0
uint8_t* XB_THISCALL ZoneMeshFileManager_Destruct(uint8_t* self, uint32_t flags);   // 0x00025930
uint8_t* XB_THISCALL ZoneMeshManager_Destruct(uint8_t* self, uint32_t flags);       // 0x00025990
uint8_t* XB_THISCALL ZoneClipManager_Destruct(uint8_t* self, uint32_t flags);       // 0x000259f0
uint8_t* XB_THISCALL ZoneClipSetManager_Destruct(uint8_t* self, uint32_t flags);    // 0x00025a40
uint8_t* XB_THISCALL ZoneAnimManager_Destruct(uint8_t* self, uint32_t flags);       // 0x00025a90
uint8_t* XB_THISCALL ZoneSkelManager_Destruct(uint8_t* self, uint32_t flags);       // 0x00025ae0

// 0x0002bf80 (zone set in EDX; zone table, out, key, an unread word, out zone id on the stack, ret 0x14): the key
// looked up (StringMap_Find) in the string map (+8) of each zone of the set's search list (Stream_ListSearchZones,
// table +4 + id * 4); *out = the first hit's entry (+0x14 list, count +0x10, index wrapped to 0) and *out_id = its
// zone id (when out_id is set); *out = 0 when none or the set is -1. Returns out.
uint32_t* XB_REGS2 XB_STDCALL ZoneStringMap_Lookup(uint32_t unused_eax, uint32_t zone_set, const uint8_t* zones,
                                                   uint32_t* out, const char* key, uint32_t unused,
                                                   uint32_t* out_id);

// 0x0028d360 (memory in EDX, passed on to StreamMem_CountFree; 4 stack words, ret 0x10, only the second read): 0
// when `needed` is above the free slot count, else 1 (also for NaN). Dead: no call, pointer or immediate.
uint8_t XB_REGS2 XB_STDCALL StreamMem_HasSpace(uint32_t unused_eax, const uint32_t* memory, uint32_t unused0,
                                               float needed, uint32_t unused2, uint32_t unused3);

// 0x00024e40 (value in EDX, load progress record in ECX): +0 = value; 0x0095f61c = (+4 + value) / 1024 (signed)
// and 0x0095f618 = that as unsigned on the x87 over (0 - +8) + +0xc, or 0 when that range is 0.
void XB_REGS3 XB_CDECL Stream_SetLoadProgress(uint32_t unused_eax, int32_t value, uint8_t* record);

// Load handlers (cdecl: name, data, size, zone): the pack's relocation table (+0x18) applied and the data stored in
// rm_cfgstr (0x0095f5d4) / rm_dlg (0x0095f5ac) at +4 + zone * 4; returns the zone. 0x002d0d40: CM_LoadWorldTree
// with cm_worldTrees (0x0095f4fc).
uint32_t XB_CDECL Stream_LoadCfgstr(char* name, uint8_t* data, uint32_t size, uint32_t zone);  // 0x00298570
uint32_t XB_CDECL Stream_LoadDlg(char* name, uint8_t* data, uint32_t size, uint32_t zone);     // 0x00299250
void XB_CDECL Stream_LoadCg(char* name, uint8_t* data, uint32_t size, uint32_t zone);           // 0x002d0d40

// Zone images: +8 asset groups (relative to the image when stored, 24 bytes each, count +0xc), each with +0x10 and
// +0x14 pointers and +0xe sub-entries (20 bytes each at +0x14: +0 a pointer, +4 a pointer or 0).
// 0x00024b90 (image in ESI): every pointer made relative to the image again (the group list last).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_UnrelocateZone(uint8_t* image);
extern "C" void Stream_UnrelocateZone_entry(void);
// 0x00024c10 (image in EDX; skip byte on the stack, ret 4): the group list and the pointers made absolute; with
// `skip` set each group's +0x14 is cleared instead of walking its sub-entries.
uint32_t XB_REGS2 XB_STDCALL Stream_RelocateZone(uint32_t unused_eax, uint8_t* image, uint32_t skip);

// 0x0028e740 / 0x0028e6e0 (clip / clip set in ESI; stream system on the stack, ret 4): the object stored in the map
// +0x98 (clip, hashed name +8) / +0xa8 (clip set, hashed name +0x10) of the zone on top of the global context's
// stack; returns the entry's previous value. The clip set first gets its player (NAL_ClipSetPlayer_Create(set, 0, 0)).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_AddClipToZone(uint32_t clip, uint32_t streams);
extern "C" void Stream_AddClipToZone_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_AddClipSetToZone(uint32_t set, uint32_t streams);
extern "C" void Stream_AddClipSetToZone_entry(void);
// 0x00025d30 / 0x00025dd0: ZoneTextureManager_Add's pattern with Stream_AddClipToZone / Stream_AddClipSetToZone.
uint32_t XB_THISCALL ZoneClipManager_Add(const uint8_t* self, uint32_t object);
uint32_t XB_THISCALL ZoneClipSetManager_Add(const uint8_t* self, uint32_t object);

// Zone releases (stream system, zone id): each entry of one of the zone's maps (zone = system +0x5c + id * 4) released
// and cleared. 0x0028cef0 (system in EAX, id in ECX): fonts (+0x20 count, +0x24 entries; nglReleaseFont).
// 0x0028cd60 (same registers): effects (+0xd0, +0xd4; FxEffectDef_Release + Z_Free of each set one).
void XB_REGS3 XB_CDECL Stream_ReleaseFonts(const uint8_t* streams, uint32_t unused_edx, uint32_t id);
void XB_REGS3 XB_CDECL Stream_ReleaseEffects(const uint8_t* streams, uint32_t unused_edx, uint32_t id);
// 0x0028cde0 (id in EAX; system on the stack, ret 4): skeletons (+0xc0, +0xc4; NAL_Skeleton_Release) with the
// system's +0x54 byte set meanwhile.
void XB_REGS1 XB_STDCALL Stream_ReleaseZoneSkeletons(uint32_t id, uint8_t* streams);
// 0x0028cf40 (id in EAX; system on the stack, ret 4): textures (+0x10, +0x14) whose flags +0x28 bit 0 are set, with
// +0xc set meanwhile; for a texture in state 3 (+0xc) +0xc is cleared and +0xe set around its nglReleaseTexture.
void XB_REGS1 XB_STDCALL Stream_ReleaseTextures(uint32_t id, uint8_t* streams);
// 0x0028ce60 (stdcall): the mesh files (+0x40, +0x44; nglReleaseMeshFile of the set ones), then the meshes' entries
// (+0x30, +0x34) cleared, with the system's +0x24 and +0x18 bytes set meanwhile.
void XB_STDCALL Stream_ReleaseMeshFiles(uint8_t* streams, uint32_t id);

// 0x00024820 (zone set in EDX; zone table, out, prefix, prefix length, callback, an unread word on the stack, ret
// 0x18): for every zone of the set's search list (table +4 + id * 4), every asset of its list (+0x14, count +0x10)
// whose name (+0) starts with the prefix: callback(name + length + 1, asset) (cdecl). *out = 0; returns out.
uint32_t* XB_REGS2 XB_STDCALL Stream_ForEachAssetWithPrefix(uint32_t unused_eax, uint32_t zone_set,
                                                            const uint8_t* zones, uint32_t* out, const char* prefix,
                                                            uint32_t length, uint32_t callback, uint32_t unused);

// 0x0028c240 (request in ECX): the next item of the request's current group (+0x7c image, +0x80 group, +0xd4 item):
// none left -> state +0xe0 = 3 and +0xd8 = 0; else +0xd8 = the item's address (group +0x10 table, entry +0xc * 0x1c
// + 0x14, plus the item's offset +0x10).
uint32_t XB_REGS3 XB_CDECL Stream_SeekNextItem(uint32_t unused_eax, uint32_t unused_edx, uint8_t* request);

// 0x002904b0 (active byte in AL, hashed name in ECX; stream system, zone, an unread word on the stack, ret 0xc):
// Stream_FindTexture's search over the zone map +0x28.
uint32_t XB_REGS3 XB_STDCALL Stream_FindInZones(uint32_t active, uint32_t unused_edx, const uint32_t* hashed,
                                                uint32_t streams, uint32_t zone, uint32_t unused);

// 0x002aeab0 (data in ESI; table on the stack, ret 4): the pack's relocation table (+8) applied, table +4 = data, and
// each anim tree's (+4 list of 12-byte groups, count +0; per group +8 list of 0x38-byte nodes, count +4) +0x28 and
// +0x24 cleared. 0x002aeb70 (load handler): with rm_animTrees (0x0095f524).
extern "C" PLATFORM_HIDDEN void XB_CDECL Stream_RelocateAnimTrees(uint8_t* data, uint8_t* table);
extern "C" void Stream_RelocateAnimTrees_entry(void);
void XB_CDECL Stream_LoadAtrb(char* name, uint8_t* data, uint32_t size, uint32_t zone);
// 0x002caa40 (data in EAX; table, zone on the stack, ret 8): Dcg_RelocateModels (cursor 8), table +4 + zone * 4 =
// data, then every brush (models +4, 0x50 bytes, count +0; brushes +4, 0x40 bytes, count +0) gets the CM_Brush
// vtable and its flags (+8) bit 0x4000 moved to 0x1000000. 0x002caba0 (load handler): with the table 0x0095f500.
uint32_t XB_REGS1 XB_STDCALL Stream_RelocateDcg(uint8_t* data, uint8_t* table, uint32_t zone);
void XB_CDECL Stream_LoadDcg(char* name, uint8_t* data, uint32_t size, uint32_t zone);

// 0x0028c1c0 (start in EAX, memory context in ESI; size on the stack, ret 4): a block above 0x2000 bytes with fewer than
// 0xb heaps on the context (+0x8c count, +0x90 list) becomes a heap (a 0x494-byte header from stream_heapHeaderPool
// 0x0095f628, Heap_Init over the 32-aligned start .. start + size), added when below 0xc. Returns 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_AddFreeBlock(uint32_t start, uint8_t* context, uint32_t size);
extern "C" void Stream_AddFreeBlock_entry(void);
// 0x0028d860 (context in EAX, size in EDX; start on the stack, ret 4): Stream_AddFreeBlock, and a returned heap is
// linked (+0x490) after the context's last one (never: it returns 0).
uint32_t XB_REGS2 XB_STDCALL MemContext_AddFreeBlock(uint8_t* context, uint32_t size, uint32_t start);

// 0x0023cfb0 (model list in EBX: +0x14 entries, count +0x10; zone set on the stack, ret 4): each model's five part
// slots (+0x24..+0x34) get their part file (+8) from rm_xmpb (0x0095f6b0) by the part's name (+4)
// (ZoneStringMap_Lookup), and the model's +0x20 the first one found when unset.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Stream_ResolveXmbParts(uint8_t* list, uint32_t zone_set);
extern "C" void Stream_ResolveXmbParts_entry(void);

// 0x0028eb90 (managers in ESI): the zone managers object's destructor (ZoneManagers vtable 0x00838300): its seven
// embedded managers (+4 texture, +0x10 mesh file, +0x1c mesh, +0x28 clip, +0x34 clip set, +0x40 anim, +0x4c skel; the
// previous one at +4 each) get their base vtables and hand the previous managers back, then ResourceManager_Destruct
// (tail jump). 0x000251a0 (vtable slot 0, thiscall, flags, ret 4): it, then Z_Free when flags bit 0.
extern "C" PLATFORM_HIDDEN void XB_CDECL ZoneManagers_Destruct(uint8_t* self);
extern "C" void ZoneManagers_Destruct_entry(void);
uint8_t* XB_THISCALL ZoneManagers_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x000251c0 / 0x00025880 (vtable slot 0, thiscall, flags, ret 4): the zone graph (removes its "ZoneGraph" command
// first) / its base: ResourceManager_Destruct, Z_Free when flags bit 0.
uint8_t* XB_THISCALL ZoneGraph_DeletingDtor(uint8_t* self, uint32_t flags);
uint8_t* XB_THISCALL ZoneGraphBase_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x0028f620 (graph in ESI, Com_Init): registered as resource manager (list 0x009719fc, count 0x009719f8, at most
// 0x14), +4..+0x18f cleared, the ZoneGraph vtable, +0x1c4/+0x1cc = -1, +0x1c8/+0x1d0 = 0, 0x0001b9a0 added to the
// callback list 0x00988784 (count 0x00988780, at most 0x20), +0x190/+0x194 = 0 and the "ZoneGraph" command
// (0x0028c430). Returns the graph.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL ZoneGraph_Init(uint8_t* self);
extern "C" void ZoneGraph_Init_entry(void);

// 0x002c0310 (stdcall: table, zone): every simple instance chain (+0x24 list of 16-byte entries, +4 the chain, next
// +0x14; count +0x20) of table +4 + zone * 4 freed (cdSimpleInstance_FreeList, Mem_AlignedFree) and its entry
// cleared; for zone != -1 inside MemContext_Push/Pop of the zone with the loading flags 0x0095f5d0 = 1 and 0x0095f5d1
// = 0 (0x0095f5d0 restored after). Returns the saved flag in AL.
uint8_t XB_STDCALL Zone_FreeSimpleInstances(const uint8_t* table, uint32_t zone);

// 0x0028d4c0 (source bit arrays in EDX; target memory, n on the stack, ret 8): moves slot bits (+0 and +8, two words
// each; slot count = target +0x10) to the target: the n-th full slot (both bits; every one when n = -1), else (none
// moved, or n = -1) the n-th half slot counting on (one bit each, every one when n = -1).
uint32_t XB_REGS2 XB_STDCALL StreamMem_MoveSlots(uint32_t unused_eax, uint32_t* from, uint32_t* to, int32_t n);

// 0x000292a0 (bsp tree in EAX): every node (+0xc, 16 bytes each, count +8) marked 0xffff at +0 gets its child
// indices +8 and +0xc made node pointers and its leaf index +4 a leaf pointer (+0x14 list, count +0x10); indices
// past the end wrap to 0. EAX is handed back.
uint8_t* XB_REGS1 XB_CDECL Stream_RelocateBtrNodes(uint8_t* tree);
// 0x0023a1b0 (load handler): cm (0x0095f6b8) = the data, its relocation table (+0x7c) applied and the tree's nodes
// relocated; the map name (0x00988bd8, 0x10c bytes cleared first, with the error callback 0x0095e634 when a map was
// loaded, 0x0095e394) and its base name without extension (0x00988c58) set, 0x0095e608 = the name; SV_ClearWorld.
void XB_CDECL Stream_LoadBtr(char* name, uint8_t* data, uint32_t size, uint32_t zone);
// 0x0023d040 (data in EAX, zone in ESI; table on the stack, ret 4): the relocation table (+0x18) applied,
// Stream_ResolveXmbParts with the zone, table +4 + zone * 4 = data. Returns the table. Dead: no call, pointer or
// immediate.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Stream_LoadXmbInto(uint8_t* data, uint32_t zone, uint8_t* table);
extern "C" void Stream_LoadXmbInto_entry(void);
// 0x002c6030 (name in EAX, path data in EBX; node blocks on the stack, ret 4): with a zone table (blocks +0x194) the
// data's seven pointers (+0x10..+0x28) and the table's +0xc entries of +0x28 (8 bytes each) are made absolute; the
// block whose name is the file's base name (Path_FindZoneBlockIndex) gets the data (blocks +8 + index * 4) and
// Path_RelocateNodeBlock when the data's type (+0) is 2. 0x00290df0 (load handler): with g_pathNodeBlocks.
extern "C" PLATFORM_HIDDEN void XB_CDECL Stream_RelocatePathData(const char* name, uint8_t* data, uint8_t* blocks);
extern "C" void Stream_RelocatePathData_entry(void);
void XB_CDECL Stream_LoadZpth(char* name, uint8_t* data, uint32_t size, uint32_t zone);

// 0x00290f00 (stdcall: zone record): inside MemContext_Push/Pop of its zone (+0x74) every resource manager (list
// 0x009719fc, count 0x009719f8) gets vtable slot 1 (thiscall, zone), then FX_KillEffectsOnEntity and
// CodNote_FreeForZone of the zone.
void XB_STDCALL Stream_UnloadNotifyManagers(const uint8_t* record);
// 0x002c4020 (zone in EBX): every note of the list at 0x008dda50 (sentinel 0x008dda54; next at +0) whose zone
// (+0x24) matches is freed (CodNote_Free), the next one read first; the list's last node is not checked.
extern "C" PLATFORM_HIDDEN void XB_CDECL CodNote_FreeForZone(uint32_t zone);
extern "C" void CodNote_FreeForZone_entry(void);

}  // namespace game
