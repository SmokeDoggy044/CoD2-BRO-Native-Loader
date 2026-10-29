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

}  // namespace game
