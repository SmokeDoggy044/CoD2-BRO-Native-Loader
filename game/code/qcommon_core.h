#pragma once

// Common engine services, low half (Q3's qcommon): the title's container helpers (its STL
// instantiations, hash maps, handle tables, bit sets), heaps, files and small math helpers.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00013df0 (list in EDX): whether a node from list+0x10 on has *key at +8. The walk stops at
// the sentinel list+0x14 only before the first step, and at a node whose next (+0) is null
// (that node is not compared).
uint8_t XB_REGS2 XB_STDCALL KeyList_Contains(uint32_t unused_eax, const uint8_t* list, const uint32_t* key);
// 0x00013e30 (v in ECX): 1 when any of x, y, z differs from 0x00922eb8 (0; NaN differs).
uint8_t XB_REGS3 XB_CDECL Vec3_IsNonZero(uint32_t unused_eax, uint32_t unused_edx, const float* v);
// 0x00014780 (table in EAX; returns it): 256 free handles - the free bit set (+0) all set, each
// slot {pointer 0, generation 1} from +0x20, +0x820 = 0.
uint8_t* XB_REGS1 XB_CDECL HandleTable256_Init(uint8_t* table);
// 0x00014850 (table in ESI): frees a handle (slot = low byte, generation = the rest) when it is
// non-zero and its generation is current: bit set, generation + 1, pointer cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL HandleTable256_Free(uint8_t* table, uint32_t handle);
extern "C" void HandleTable256_Free_entry(void);
// 0x00014e40 (out in EAX, key in ECX, map in ESI): the hash map's node for *key (buckets +0x14,
// mask +0x20, bucket limit +0x24, ordered keys at node+8, signed) or the end node (+8) into *out.
extern "C" PLATFORM_HIDDEN void XB_CDECL IntHashMap_Find(uint32_t* out, const int32_t* key, const uint8_t* map);
extern "C" void IntHashMap_Find_entry(void);
// 0x00015190 (dst in EAX, src in ECX; returns dst): dst = ~src over 256 bits (all read first).
uint32_t* XB_REGS3 XB_CDECL BitSet256_Complement(uint32_t* dst, uint32_t unused_edx, const uint32_t* src);
// 0x000152b0 (iterator in EDX): the next set bit of a 256-bit set {words, current word, word
// index, result}; result and index -1 at the end.
void XB_REGS2 XB_CDECL BitSet256_NextSet(uint32_t unused_eax, uint32_t* iterator);
// 0x00015790 (out in EAX, vector in EBX): erases [first, last) of a vector of dwords (end +8);
// *out = first.
extern "C" PLATFORM_HIDDEN void XB_CDECL std_vector_Erase4_2(uint32_t* out, uint8_t* vector, uint32_t* first,
                                                             uint32_t* last);
extern "C" void std_vector_Erase4_2_entry(void);
// 0x0001b640 (dest end in EAX, first in EDX, last in ECX): copy_backward of dwords.
uint32_t* XB_REGS3 XB_CDECL std_CopyBackward4_2(uint32_t* dest, const uint32_t* first, const uint32_t* last);
// 0x0001b8f0: whether two 32-byte hash strings are equal.
uint8_t XB_THISCALL HashStr_Equals(const uint32_t* a, const uint32_t* b);
// 0x0001c140: a 4x4 identity (rows 0-2 as 1.0 times the constant rows, row 3 copied).
void XB_CDECL Matrix_Identity(float* out);
// 0x0001ed50 (table in EAX): count 0, then 16 slots of 0x84 bytes from +4: 8 dwords 0, -1, and
// 6 each of 0, 0, -1, -1.
void XB_REGS1 XB_CDECL SoundBankTable_Init(uint8_t* table);
// 0x0001edb0 (index in EAX, array in ECX {count, data}): the 48-byte element at index, element 0
// when index >= count.
uint8_t* XB_REGS3 XB_CDECL Array48_GetOrFirst(uint32_t index, uint32_t unused_edx, const uint32_t* array);
// 0x0001efd0 (pos in EAX): erases [pos, last) of a 12-byte element array {data, ?, count}.
void XB_REGS1 XB_STDCALL DynArray_Erase12(uint32_t* pos, uint32_t* array, const uint32_t* last);

// 0x0001f340 (vector in ECX): the element count of a vector of 28-byte strings (begin +4, end +8;
// 0 without storage).
int32_t XB_REGS3 XB_CDECL std_vector_string_Size(uint32_t unused_eax, uint32_t unused_edx, const uint32_t* vector);
// 0x0001f740 (dst in EAX, src in ECX): copies one 0x84-byte sound bank slot (dword by dword).
void XB_REGS3 XB_CDECL SoundBankSlot_Copy(uint32_t* dst, uint32_t unused_edx, const uint32_t* src);
// 0x00020990 (dst in EAX, src in EDX): dst = ~src over 512 bits (all read first).
void XB_REGS2 XB_CDECL HandleBits512_Invert(uint32_t* dst, const uint32_t* src);
// 0x00021480 (iterator in EDX): the next set bit of a 512-bit set (BitSet256_NextSet's walk).
void XB_REGS2 XB_CDECL BitIter_Next(uint32_t unused_eax, uint32_t* iterator);

// The title's red-black trees keyed by 28-byte strings (data or SSO buffer +4, size +0x14,
// capacity +0x18): nodes {left, parent, right, key at +0xc, ..., isnil byte}; the head node at
// tree+4. Three instantiations differ only in the isnil offset: 0x29 (tree29, a set), 0x65
// (map_string), 0x2d (set_string).
// lower_bound (tree in EAX, key in ECX): the first node whose key is not less, else the head.
uint8_t* XB_REGS3 XB_CDECL std_map_string_LowerBound(const uint8_t* tree, uint32_t unused_edx, const uint8_t* key);
uint8_t* XB_REGS3 XB_CDECL std_map_string_LowerBound_2(const uint8_t* tree, uint32_t unused_edx, const uint8_t* key);
uint8_t* XB_REGS3 XB_CDECL std_tree_LowerBound29(const uint8_t* tree, uint32_t unused_edx, const uint8_t* key);
// 0x000223b0 upper_bound (tree in EAX): the first node whose key is greater, else the head.
uint8_t* XB_REGS1 XB_STDCALL std_tree_UpperBound29(const uint8_t* tree, const uint8_t* key);
// Rotations about a node (ECX) of a tree (stack).
void XB_THISCALL std_tree_Lrotate29(uint8_t* node, uint8_t* tree);
void XB_THISCALL std_tree_Rrotate29(uint8_t* node, uint8_t* tree);
void XB_THISCALL std_map_string_Lrotate(uint8_t* node, uint8_t* tree);
void XB_THISCALL std_map_string_Rrotate(uint8_t* node, uint8_t* tree);
void XB_THISCALL std_set_string_Lrotate(uint8_t* node, uint8_t* tree);
void XB_THISCALL std_set_string_Rrotate(uint8_t* node, uint8_t* tree);
// Iterator decrement / increment (iterator in EDX).
void XB_REGS2 XB_CDECL std_tree_Dec29(uint32_t unused_eax, uint8_t** iterator);
void XB_REGS2 XB_CDECL std_set_string_Dec(uint32_t unused_eax, uint8_t** iterator);
void XB_REGS2 XB_CDECL std_map_string_Dec(uint32_t unused_eax, uint8_t** iterator);
void XB_REGS2 XB_CDECL std_tree_Inc29(uint32_t unused_eax, uint8_t** iterator);
void XB_REGS2 XB_CDECL std_set_string_Inc(uint32_t unused_eax, uint8_t** iterator);
void XB_REGS2 XB_CDECL std_map_string_Inc(uint32_t unused_eax, uint8_t** iterator);
// Rightmost / leftmost node under a node (EAX).
uint8_t* XB_REGS1 XB_CDECL std_tree_Max29(uint8_t* node);
uint8_t* XB_REGS1 XB_CDECL std_tree_Min29(uint8_t* node);
uint8_t* XB_REGS1 XB_CDECL std_map_string_Max(uint8_t* node);
uint8_t* XB_REGS1 XB_CDECL std_map_string_Min(uint8_t* node);
uint8_t* XB_REGS1 XB_CDECL std_set_string_Max(uint8_t* node);
uint8_t* XB_REGS1 XB_CDECL std_set_string_Min(uint8_t* node);

// 0x000289c0 (out in EAX, key in ECX, map in ESI): IntHashMap_Find with unsigned keys.
extern "C" PLATFORM_HIDDEN void XB_CDECL UintHashMap_Find(uint32_t* out, const uint32_t* key, const uint8_t* map);
extern "C" void UintHashMap_Find_entry(void);
// 0x00024a30 (bits in EAX; result in XMM0): 0.5 (0x0083eb24) for each of bits 0-63 set in the
// first 64-bit set and in the second (+8), summed in bit order.
extern "C" PLATFORM_HIDDEN float XB_CDECL LoadBits_Progress(const uint32_t* bits);
extern "C" void LoadBits_Progress_entry(void);
// 0x00026050 (table in EAX {count, 12-byte entries with the name first}): the entry from 1 on
// whose name equals name, else null.
uint8_t* XB_REGS1 XB_STDCALL NameTable12_FindFrom1(const uint32_t* table, const char* name);
// 0x000261d0 (record in EAX): clears the 32 words +0x80 .. +0xfc.
void XB_REGS1 XB_CDECL BlockTail80_Clear(uint8_t* record);
// 0x0002b950: dst's x, y, z from src (16 bytes read), dst's w kept.
void XB_THISCALL Vec3A_CopyXYZ(uint32_t* dst, const uint32_t* src);

// The heaps: 0x494-byte records from 0x008ff638 (region base +0x474, limit +0x478, top +0x47c,
// end +0x480, size +0x484, bytes in use +0x488), the current one in 0x00901ae8.
// 0x00042470: a heap over [base, end) (nothing when empty): cleared, then the region fields and
// +0x490 = tag.
void XB_CDECL Heap_Init(uint8_t* heap, uint32_t base, uint32_t end, uint32_t tag);
// 0x00042560: clears the region fields; 1 when nothing is in use, else -4.
int32_t XB_CDECL Heap_DetachRegion(uint8_t* heap);
// 0x00042830: moves the current heap's top up by size and returns the old top (-1 when it
// would reach the limit).
uint32_t XB_CDECL Heap_Sbrk(uint32_t size);
// 0x00042860: the current heap.
uint32_t XB_CDECL Heap_GetCurrent();
// 0x00042ac0: makes heap index current; returns the previous one.
uint32_t XB_CDECL Heap_SelectByIndex(int32_t index);
// 0x00042ca0: makes heap current; returns the previous one.
uint32_t XB_CDECL Heap_SetCurrent(uint32_t heap);

// 0x00044330: appends src (null: nothing) at dst + *length while *length < max, then a NUL.
void XB_CDECL Str_AppendBounded(char* dst, int32_t* length, int32_t max, const char* src);
// 0x00044370: copies min(count, max) bytes (dwords, then bytes) and a NUL; *length = that count.
void XB_CDECL FixedStr_Copy(char* dst, int32_t* length, int32_t count, const char* src, int32_t max);
// 0x00044960: whether ptr lies in one of the small heaps' ranges [+0x18, +0x1c] (list {count,
// heaps}).
uint8_t XB_THISCALL SmallHeap_Owns(const uint32_t* heaps, uint32_t ptr);
// 0x000938f0: installs the six allocator hooks (0x009098d8 .. 0x009098ec; +0x10 is the one
// Mem_AlignedAlloc calls).
void XB_CDECL Mem_SetAllocatorHooks(const uint32_t* hooks);

// 0x00029aa0: out = v / |v| (16-byte aligned, 4 lanes; |v|^2 summed (x^2 + y^2) + z^2, rsqrt estimate
// and one Newton step); a zero vector scales by 0 (0x0082e6a0). Returns out.
float* XB_CDECL Vec3A_Normalize(float* out, const float* v);
// 0x000b88c0: the same with an unaligned load of v (constants at 0x007cd5f0..0x007cd670).
float* XB_CDECL Vec3A_NormalizeDup(float* out, const float* v);
// 0x0011fd80: out = (x, y, z) * s. Returns out. No callers, no stored pointer (dead).
float* XB_CDECL Vec3_Scale(float* out, float x, float y, float z, float s);
// 0x0002a230: grows sphere A (center_a aligned, *radius_a) to hold sphere B (center_b, radius_b):
// B contains A -> A = B; A contains B -> unchanged; else the smallest sphere around both.
void XB_REGS3 XB_CDECL Sphere_Merge(const float* center_b, float* radius_a, float* center_a, float radius_b);
// 0x0002ac30: out = body's rotation rows (+0, +0x10, +0x20) times v (4 lanes, unaligned store);
// v unchanged when flag 0x10 of +0x144 is set. Returns out.
float* XB_CDECL PhysBody_RotateVector(float* out, const uint8_t* body, const float* v);
// 0x000a7570: arccos(x) by odd polynomials (|x| < 0.5: pi/2 - asin series; else 2 asin of
// sqrt((1 - |x|) / 2)), on the x87.
long double XB_CDECL FastAcos(float x);
// 0x000aff30: dot(n, a - b) / dot(n, d), or 1e20 when |dot(n, d)| < 0.0001 (all aligned).
float XB_CDECL RayPlaneIntersectFrac(const float* a, const float* n, const float* b, const float* d);
// 0x000c3b40: for each set bit i of the 64-bit mask, dst[i] = the next word of src.
void XB_CDECL BitMask64_Unpack(uint32_t* dst, uint32_t mask_lo, uint32_t mask_hi, const uint32_t* src);
// 0x000c56b0: the index of `bit` among the set bits of *mask (bits below it counted), -1 when not
// all of `bit` is set.
int32_t XB_REGS1 XB_CDECL BitMask64_IndexOfBit(const uint32_t* mask, uint32_t bit_lo, uint32_t bit_hi);
// 0x000a7670: ARGB of color (aligned, 0..1) * 255 * (scale.xyz, 1), clamped to [0, 255], truncated.
uint32_t XB_CDECL apsColor_PackScaled(const float* scale, const float* color);
// 0x000b8ed0: rotation rows 0-2 about the unit axis (aligned) from its sine and cosine (each row's w
// = its x). Returns out + 8.
float* XB_CDECL Mat34_FromAxisSinCos(float* out, const float* axis, float sine, float cosine);
// 0x000b9080: rotation by |v| * scale about v (aligned); identity when |v| < 1e-5.
void XB_CDECL Mat34_FromRotationVector(float* out, const float* v, float scale);
// 0x000b9180: the rotation turning a towards b (aligned) about a x b; identity when |a x b|^2 < 1e-5.
void XB_CDECL Mat34_RotationBetweenVectors(float* out, const float* a, const float* b);
// 0x00029ff0: out = the 4x4 rotation (rows 0-2, row 3 = 0x0082e290) turning a towards b (aligned):
// cos I + sin [n]x + (n - n cos) n^T with n = a x b normalized, cos = a.b, sin = |a x b|.
void XB_REGS3 XB_CDECL Quat_RotationBetween(const float* a, uint32_t, const float* b, float* out);
// 0x00029e30: out = the inverse of the rigid 4x4 m: rows 0-2 transposed, row 3 = -(R^T t)
// (out == m: through a local copy).
void XB_CDECL Mat44_InverseRigid_2(float* out, const float* m);

// Save device state (0x000d0c40..0x000d1640): last result 0x00920aa4, path prefix 0x00920aa8, port
// 0x008c02d0, buffer/size 0x00920ae8/ec, listener 0x00920af0, op state 0x00920af4, removed listener
// 0x00920af8, drive names 0x00920860 (64 bytes per port 0..8).
uint32_t XB_CDECL Save_GetLastResult();                   // 0x000d0c40
void XB_CDECL Save_SetLastResult(uint32_t result);        // 0x000d0c50
void XB_CDECL Save_SetPathPrefix(const char* prefix);     // 0x000d0c60: strcpy to 0x00920aa8
uint32_t XB_CDECL Save_GetData();                         // 0x000d0c80: the address 0x00920aa8
void XB_CDECL Save_SetPort(int32_t port);                 // 0x000d0c90
int32_t XB_CDECL Save_GetPort();                          // 0x000d0ca0
void XB_CDECL Save_SetListener(uint32_t listener);        // 0x000d0cb0
void XB_CDECL Save_SetDeviceRemovedListener(uint32_t listener);  // 0x000d0cc0
uint32_t XB_CDECL Save_GetBuffer();                       // 0x000d0cf0
uint32_t XB_CDECL Save_GetBufferSize();                   // 0x000d0d00
uint32_t XB_CDECL Save_GetOpState();                      // 0x000d0e40
int32_t XB_CDECL Save_LastPort();                         // 0x000d13d0: 8
// Save request: up to 8 files {name[64] at +0, word +0x200, word +0x220}, cursor +0x240, count
// +0x244, file name +0x248.
// 0x000d0d10: copies the file name and empties the list.
void XB_THISCALL SaveRequest_SetFileName(uint8_t* request, const char* name);
// 0x000d0d50: appends a file (false when 8 are listed).
uint8_t XB_THISCALL SaveRequest_AddFile(uint8_t* request, const char* name, uint32_t a, uint32_t b);
// 0x000d0dd0: the next listed file (false after the last): name copied out, both words.
uint8_t XB_THISCALL SaveRequest_NextFile(uint8_t* request, char* name, uint32_t* a, uint32_t* b);
// 0x000d0e30: request + 0x248.
char* XB_THISCALL SaveRequest_GetFileName(uint8_t* request);
// 0x000d11d0: SaveRequest_SetFileName; returns the request.
uint8_t* XB_THISCALL SaveRequest_Init(uint8_t* request, const char* name);
// 0x000d0e60: copies the 64-byte drive names of ports Save_FirstPort..Save_LastPort.
void XB_CDECL Save_SetDriveNames(const char* names);
// 0x000d12c0: closes the save file (0x00920eac) and the enumeration handle (0x00920da0), clears the
// op callback (0x00920eb0).
void XB_CDECL Save_CloseFileHandles();
// 0x000d1300: Save_CloseFileHandles, then resets the overlapped block (0x00920d90..9c), 0x00920da8,
// 0x008c02d8 and the drive letter 0x008c02d4 ('?').
void XB_CDECL Save_CloseDevice();
// 0x000d1360: Xbox error code -> save result (0 -> 0, 0x55 -> -17, 0x70 -> -3, 0x48f -> -10, else -1).
int32_t XB_CDECL Save_TranslateError(int32_t error);
// 0x000d1410: 0x00920afc = value, default drive names (0x007cfa40), port 8, result 0; returns the result.
uint32_t XB_CDECL Save_Init(uint32_t value);
// 0x000d1610: (bytes + 0x3fff) >> 14 (16 KB blocks); the high half goes to ECX (no caller reads it).
uint32_t XB_CDECL BytesToBlocks(uint32_t bytes_lo, uint32_t bytes_hi);
// 0x000d1630: memory-unit port 0..7 -> controller port/slot {0, 1, 2, 3}[port / 2], port % 2; -18 when
// out of range. Port 8 reads past the table (the return address); callers handle 8 before.
int32_t XB_CDECL Save_PortToDevice(int32_t port, uint32_t* controller, uint32_t* slot);

// File system (0x000c9230..0x000cb590): devices {count 0x008bfe7c, list 0x008bfe80}, open files = handle
// pool 0x009207e8 over 36-byte records at 0x00920820 (+0 size, +4 device bit, +8 the aliased handle when
// +0x20 == 1) with driver data at 0x00920824 + 0x00920828 * index; requests = pool 0x00920798 over
// 0x44-byte records at 0x00920810 (+0x18 start time, +0x2c state, +0x34 bytes done).
// 0x000c9230: releases the file mutex (0x00920784) when 0x008bfd80 == 1.
void XB_CDECL File_UnlockMutex();
// 0x000c9250 (bit in ECX): the first device whose mask (*device[+8]) has 1 << bit, else -1.
int32_t XB_REGS3 XB_CDECL File_FindDeviceForBit(uint32_t, uint32_t, uint32_t bit);
// 0x000c9320 (handle in ESI): the open file's record (through its alias when +0x20 == 1), else 0.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL File_GetRecord(int32_t handle);
extern "C" void File_GetRecord_entry(void);
// 0x000c9390: the open file's device bit (record +4), else -1.
int32_t XB_CDECL File_GetDeviceBit(int32_t handle);
// 0x000c93d0 (handle in ESI): the open file's driver data (aliased like File_GetRecord), else 0.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL File_GetDriverData(int32_t handle);
extern "C" void File_GetDriverData_entry(void);
// 0x000c9490 (state in EAX; ECX/EDX kept for the callers): request state 0..7 -> status
// {4, 4, 0, 1, 1, 2, 3, 3}, else -1.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL File_RequestStateToStatus(uint32_t state);
extern "C" void File_RequestStateToStatus_entry(void);
// 0x000c9910: each device's shutdown callback (vtable +4, slot +8), last device first.
void XB_CDECL File_ShutdownDevices();
// 0x000c9940: out = the device info words (+0x14: [+8], [0], [+4]) of the device for `bit`; returns
// out, or 0 when there is none.
uint32_t* XB_CDECL File_GetDeviceInfoForBit(uint32_t bit, uint32_t* out);
// 0x000c9d10: the open file's size, else -1.
int32_t XB_CDECL File_GetSize(int32_t handle);
// 0x000c9db0: the request's status (File_RequestStateToStatus), else -1.
int32_t XB_CDECL File_GetRequestStatus(int32_t handle);
// 0x000c9ea0: out = {bytes done, Snd_UpdateTime() - start}; out unwritten for a dead handle. Returns out.
uint32_t* XB_CDECL File_GetRequestProgress(int32_t handle, uint32_t* out);
// Handle pool {data +0, free list +4, used list +0x10 (next, prev, -1), stride +0x1c, count +0x20,
// index mask +0x24}; each element starts {next, prev, handle} (handle bit 31 = free).
// 0x000cb390: all `count` elements (stride >= 12, count < 2^20) on the free list; false when invalid.
uint8_t XB_CDECL HandlePool_Init(uint8_t* pool, uint8_t* data, int32_t count, uint32_t stride);
// 0x000cb510: moves the first free element to the used list with the next generation of its handle;
// -1 when none is free.
int32_t XB_CDECL HandlePool_Alloc(uint8_t* pool);
// 0x000cb580: moves a live handle's element back to the free list (bit 31 set).
void XB_CDECL HandlePool_Free(uint8_t* pool, int32_t handle);

// Heaps (dlmalloc 2.7 without mmap/trim; layout at Heap_EnsureInit in the .cpp). Current heap 0x00901ae8.
// 0x00042ea0 (heap in EAX; gap function; returns it): empty bins (127 self-linked pairs from +0x34),
// top = +0x34, max_fast 0x48 | 1 | (old & 2), trim/mmap thresholds 0x20000, page size 0x2000.
uint8_t* XB_REGS1 XB_CDECL Heap_InitBins(uint8_t* heap);
// 0x00042f00: Heap_InitBins when max_fast is 0, else merges every fast-bin chunk with its free
// neighbours into the unsorted bin (or the top) - dlmalloc's malloc_consolidate.
void XB_CDECL Heap_EnsureInit(uint8_t* heap);
// 0x00043260: frees a chunk of the current heap: fast bin up to max_fast, nothing for bit-1 chunks, else
// merged into the unsorted bin or the top; Heap_EnsureInit when 0x10000+ bytes got free with fast chunks.
void XB_CDECL Heap_FreeBlock(uint8_t* mem);
// 0x00042680: in the system heap, Sys_DebugBreak for a block inside a pool (0x008ff5b0 {start, size}
// x 0x008ff630) when 0x00901aec is set; then +0x488 -= chunk size and Heap_FreeBlock (tail jump).
void XB_CDECL Heap_Free(uint8_t* mem);
// 0x00042710: Heap_Free with `heap` current.
void XB_CDECL Heap_FreeIn(uint32_t heap, uint8_t* mem);
// 0x00042870: frees mem in the first heap of the +0x490 chain whose region holds it (stops at null or the
// system heap); 1 when freed.
uint8_t XB_CDECL Heap_FreeFromOwner(uint8_t* heap, uint8_t* mem);
// 0x000428d0: frees mem in its owner chain (0x008ff4a8 x 0x008ff5a8), else the first of the 8 heaps with
// a size whose region holds it, else the current heap if its region holds it.
void XB_CDECL Z_Free(uint8_t* mem);
// 0x00042e30 (heap in ECX): Heap_FreeIn.
void XB_THISCALL Heap_FreeIn2(uint32_t heap, uint8_t* mem);
// 0x00042af0: Heap_MallocAligned in the current heap (default heaps made first), counted in +0x488/peak
// +0x48c; on failure the next heap (+0x490), then from the system heap each owner heap; Sys_DebugBreak
// (0x00901aec set) when all fail.
uint8_t* XB_CDECL Heap_AllocAligned(uint32_t align, uint32_t size);
// 0x00042c30: Heap_AllocAligned(16, size).
uint8_t* XB_CDECL Heap_Alloc16(uint32_t size);
// 0x00042c40: the same body (stored as a pointer at 0x000ca133).
uint8_t* XB_CDECL Heap_Alloc16_2(uint32_t size);
// 0x00042cb0: Heap_AllocAligned's twin over Heap_Malloc; size 0 returns null.
uint8_t* XB_CDECL Z_Malloc(uint32_t size);
// 0x00042da0 (heap in ECX; returns it): Heap_Init over a new Z_Malloc block of `size`.
uint8_t* XB_THISCALL Heap_CreateFromZone(uint8_t* heap, uint32_t size);
// 0x00042de0 (heap in ECX): Heap_AllocAligned in `heap` unless used + size exceeds its capacity (null).
uint8_t* XB_THISCALL Heap_AllocFromHeap(uint8_t* heap, uint32_t size, uint32_t align);
// 0x00044040..0x000440e0: pool bounds, Mem_AllocPhysicalPools first while the value is 0. Physical
// 0x00901af0/f4, debug 0x00901afc/0x00901b00 (-1 = none), write-combined 0x00901b08/0c.
uint32_t XB_CDECL Mem_GetPhysicalPoolEnd();
uint32_t XB_CDECL Mem_GetPhysicalPoolStart();
uint32_t XB_CDECL Mem_GetDebugPoolEnd();
uint32_t XB_CDECL Mem_GetDebugPoolStart();
uint32_t XB_CDECL Mem_GetWCPoolEnd();
uint32_t XB_CDECL Mem_GetWCPoolStart();
// 0x00042740: once (0x00901ad8 = 0): clears the 8 heaps, heap 0 over the physical pool (32-byte
// aligned), heap 2 over the write-combined one, heap 1 over the debug pool (chained from heap 0);
// heap 0 current. Returns 1.
uint32_t XB_CDECL Heap_InitDefaultHeaps();

// 0x00043350 (heap in EAX): dlmalloc's sysmalloc over Heap_Sbrk (no mmap): grows the top by whole pages
// (+0x45c), merging with the old top when contiguous, else fencing the old top (5, 5) and freeing it;
// sbrked +0x464 (peak +0x468), total peak +0x470; nb bytes from the new top or errno ENOMEM and null.
uint8_t* XB_REGS1 XB_CDECL Heap_SysAlloc(uint8_t* heap, uint32_t nb);
// 0x00043500: dlmalloc 2.7 malloc in the current heap: fast bin, small bin, unsorted scan (sorting into
// bins, binmap +0x434), best large fit, binmap search, top, Heap_EnsureInit + retry, Heap_SysAlloc.
uint8_t* XB_CDECL Heap_Malloc(uint32_t bytes);
// 0x00043b70: memalign: Heap_Malloc for align <= 8, else align raised to a power of two >= 16; the lead
// and tail of an over-sized block go back through Heap_FreeBlock.
uint8_t* XB_CDECL Heap_MallocAligned(uint32_t align, uint32_t bytes);
// 0x00043020 (dead: only Heap_MallInfo calls it): mallinfo of the current heap into out[10]; returns out.
uint32_t* XB_CDECL Heap_GetMallInfo(uint32_t* out);
// 0x00043150 (dead: no callers): Heap_GetMallInfo into a local.
void XB_CDECL Heap_MallInfo();
// Small-block heaps: count +0, pools +4 (8), size limits +0x28 (8 int16, count +0x24), +0x38 no pools,
// +0x39 pools off, +0x3a no fallback. Pool (0x28 bytes): block size +0, blocks +4, free +8, tag +0xc,
// align +0x10, peak used +0x14, block range [+0x18, +0x1c], free list +0x24 (links at block + 4).
// 0x00044620 (pool in ECX; returns it): `count` blocks of `block_size` (Heap_AllocAligned), each tagged
// {tag (16 bit), 0, 0} and pushed on the free list.
uint8_t* XB_THISCALL SmallHeap_InitPool(uint8_t* pool, uint32_t block_size, uint32_t count, uint32_t tag,
                                        uint32_t align);
// 0x00044770 (heap in ECX; returns it): one Z_Malloc'd pool per config entry {size, align, count} after the
// count (config[0]); the first 8 are kept, with their sizes as limits.
uint8_t* XB_THISCALL SmallHeap_Init(uint8_t* heap, const uint32_t* config, uint32_t no_pools, uint32_t no_fallback);
// 0x00044810 (heap in ECX; dead: no callers, no pointer): Z_Free of each pool's blocks and the pool.
void XB_THISCALL SmallHeap_Destroy(uint8_t* heap);
// 0x00044850 (heap in ECX): a free block of the first pool whose limit (int16) fits the size's low 16 bits,
// counting the peak; else (no_pool, +0x39) Heap_AllocAligned(16, size) unless +0x3a (null).
uint8_t* XB_THISCALL SmallHeap_Alloc(uint8_t* heap, uint32_t size, uint32_t no_pool);
// 0x00044910 (heap in ECX): back on the free list of the pool whose block range holds mem, else Z_Free.
void XB_THISCALL SmallHeap_Free(uint8_t* heap, uint8_t* mem);

}  // namespace game
