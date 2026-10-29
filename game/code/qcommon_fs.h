#pragma once

// qcommon, upper address half (0x00170000 and up): the script-heap containers (DynArray,
// HashMap, MSVC std::vector/std::list helpers), file system, commands, cvars, localization,
// pak zones, math helpers. Split from qcommon.cpp so two agents can work in parallel; the
// module is still qcommon (names/modules.csv).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Script-heap dynamic array: data from Scr_Alloc, capacity and size in elements.
struct DynArray {
    uint32_t* data;
    uint32_t capacity;
    uint32_t size;
};

struct DynArrayBytes {
    uint8_t* data;
    uint32_t capacity;
    uint32_t size;
};

// MSVC 7.1 std::list node of the HashMap: next, prev, key, value.
struct HashNode {
    HashNode* next;
    HashNode* prev;
    uint32_t key;
};

// Hash map: a std::list of nodes (head at +8) plus a bucket vector of list iterators
// (begin at +0x14); bucket index = (key ^ 0xdeadbeef) & mask, folded down by mask/2+1 when
// it reaches the bucket count (linear hashing).
struct HashMap {
    uint32_t unknown0[2];
    HashNode* head;          // +0x08
    uint32_t unknown0c[2];
    HashNode** buckets;      // +0x14
    uint32_t unknown18[2];
    uint32_t mask;           // +0x20
    uint32_t max_index;      // +0x24
};

// MSVC 7.1 std::vector of 4-byte elements (allocator word first).
struct Vector4 {
    uint32_t allocator;
    uint32_t* first;
    uint32_t* last;
    uint32_t* end;
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(DynArray) == 12);
static_assert(offsetof(HashMap, buckets) == 0x14);
static_assert(offsetof(HashMap, max_index) == 0x24);
static_assert(offsetof(Vector4, last) == 8);
#endif

// 0x0017ab70 (callee pops 8): count 24-byte elements from Scr_Alloc, each word set to the
// script "undefined" float (0x00922eb8).
uint32_t* XB_STDCALL DynArray_Alloc24_FillUndefined(uint32_t count, uint32_t unused);
// 0x0017ac00 (callee pops 4; callers set ECX, unused): count ints set to g_scrUndefinedInt.
uint32_t* XB_STDCALL DynArray_AllocInt_FillUndefined(uint32_t count);
// 0x0017ac40 (callee pops 4): count bytes set to g_scrUndefinedBool.
uint8_t* XB_STDCALL DynArray_AllocBool_FillUndefined(uint32_t count);
// 0x0017b630: std::fill over 4-byte words.
uint32_t* XB_CDECL std_Fill4(uint32_t* first, uint32_t* last, const uint32_t* value);
// 0x0017b650: std::copy_backward over 4-byte words; returns the new destination start.
uint32_t* XB_CDECL std_CopyBackward4(uint32_t* first, uint32_t* last, uint32_t* dest_end);
// 0x0017b9a0: resize an int array to size; grows to capacity, new slots undefined.
void XB_THISCALL DynArray_ResizeInt(DynArray* array, uint32_t size, uint32_t capacity);
// 0x0017ba30: copy-assign an int array (the old data is freed first).
void XB_THISCALL DynArray_Assign4(DynArray* array, const DynArray* source);
// 0x0017baa0: DynArray_ResizeInt for a byte (bool) array.
void XB_THISCALL DynArray_ResizeBytes(DynArrayBytes* array, uint32_t size, uint32_t capacity);
// 0x0017bbb0: std::uninitialized_copy over 4-byte words (skips a null destination).
uint32_t* XB_CDECL std_UninitializedCopy4(const uint32_t* first, const uint32_t* last, uint32_t* dest);
// 0x0017bc40 (callee pops 12): new list node with a 16-byte value.
uint32_t* XB_STDCALL std_list_BuyNode16(uint32_t next, uint32_t prev, const uint32_t* value);
// 0x0017bc80: new list head node pointing at itself.
uint32_t* XB_CDECL std_list_BuyHead();
// 0x0017bca0: resize a 4-byte array; grows to capacity, new slots left unset.
void XB_THISCALL DynArray_Resize4(DynArray* array, uint32_t size, uint32_t capacity);
// 0x0017bf40: DynArray_Resize4 for bytes.
void XB_THISCALL DynArray_ResizeBytesNoFill(DynArrayBytes* array, uint32_t size, uint32_t capacity);
// 0x0017c180: std::uninitialized_fill_n over 4-byte words.
uint32_t* XB_CDECL std_UninitializedFillN4(uint32_t* dest, uint32_t count, const uint32_t* value);
// 0x0017cb40: find; *out = the key's node or the end node. Returns out.
HashNode** XB_THISCALL HashMap_Find(HashMap* map, HashNode** out, const uint32_t* key);
// 0x0017cd50: std::vector::erase(first, last); *out = first. Returns out.
uint32_t** XB_THISCALL std_vector_Erase4(Vector4* vector, uint32_t** out, uint32_t* first, uint32_t* last);
// 0x0017bb40: DynArray_Assign4 for a byte (bool) array.
void XB_THISCALL DynArray_AssignBytes(DynArrayBytes* array, const DynArrayBytes* source);
// 0x00182090: out = (x, y, z) * scale. Returns out.
float* XB_CDECL Vec3_SetScaled(float* out, float x, float y, float z, float scale);
// 0x0019ba60: removes the first element equal to value (the rest moves down one).
// Returns the last position the scan or the move reached.
uint32_t* XB_CDECL PtrArray_RemoveValue(DynArray* array, uint32_t value);
// 0x001e4520 (callee pops 8): count 12-byte elements {undefined int, undefined int, 0}.
uint32_t* XB_STDCALL DynArray_Alloc12_FillUndefined(uint32_t count, uint32_t unused);
// 0x001e4570 (callee pops 8): count 24-byte elements: three words of 0x00922eb4, two
// undefined floats (0x00922eac), an undefined int.
uint32_t* XB_STDCALL DynArray_Alloc24_FillUndefined_2(uint32_t count, uint32_t unused);
// 0x001e5920 (callee pops 8): count 52-byte elements: undefined int, 0, 0, 0x00922eb4,
// three undefined floats (0x00922eac), six of 0x00922eb8.
uint32_t* XB_STDCALL DynArray_Alloc52_FillUndefined(uint32_t count, uint32_t unused);
// 0x001e6330: HashMap_Find with signed keys.
HashNode** XB_THISCALL HashMap_Find_2(HashMap* map, HashNode** out, const int32_t* key);
// 0x001ef700: std::exception constructor (vtable 0x00827904, what = null). Returns this.
uint32_t* XB_THISCALL std_exception_Ctor(uint32_t* self);

// Q3's event queue: 64 sysEvent_t at 0x0095e800, head 0x0095e7f4, tail 0x0095ee04.
struct SysEvent {
    int32_t time;
    int32_t type;
    int32_t value;
    int32_t value2;
    int32_t ptr_length;
    void* ptr;
};

// 0x00222f20 (out in EDI): the next queued event, else an empty one stamped with
// Sys_Milliseconds (the code at 0x00222f70 is this function's tail). Returns out.
extern "C" PLATFORM_HIDDEN SysEvent* XB_CDECL Sys_GetEvent(SysEvent* out);
extern "C" void Sys_GetEvent_entry(void);
// 0x00222fa0 (time in EAX): Q3's Sys_QueEvent; a full queue drops (and Z_Frees) the
// oldest event. Time 0 = now. Returns ptr_length (left in EAX).
int32_t XB_REGS1 XB_CDECL Sys_QueEvent(int32_t time, int32_t type, int32_t value, int32_t value2,
                                       int32_t ptr_length, void* ptr);
// 0x00223040 (path in EAX): 1 when the directory holds a file or a subdirectory other
// than ".", ".." and "CVS" (that early return leaves the find handle open).
int32_t XB_REGS1 XB_CDECL FS_DirectoryHasFiles(const char* path);
// 0x0024c0e0 (angle in XMM0, target in XMM2; result in XMM0): wraps angle to within 180
// of target, moves it toward target by delta * (rate * 0.001) * scale unless that
// overshoots (deltas up to 0.005 snap), then quantizes to 16 bits and wraps to
// (-180, 180]. Callers keep ECX and EDX.
extern "C" PLATFORM_HIDDEN float XB_CDECL AngleWrapNear(float angle, float target, float scale, int32_t rate);
extern "C" void AngleWrapNear_entry(void);
// 0x0024c1d0 (angle in XMM0, target in XMM1): like AngleWrapNear with a fixed step
// (0x008ddaa0 * scale) toward target.
extern "C" PLATFORM_HIDDEN float XB_CDECL AngleWrapNear2(float angle, float target, float scale);
extern "C" void AngleWrapNear2_entry(void);
// Bare `ret` stubs: console commands compiled out of the Xbox build, empty CRT
// initializers.
void XB_CDECL Cmd_NglStats_f();  // 0x00248db0
void XB_CDECL Cmd_Invinc_f();    // 0x0024d240
void XB_CDECL Cmd_List_f();      // 0x002aa870
void XB_CDECL Cmd_Echo_f();      // 0x002aaad0
void XB_CDECL CrtInitStub_379cc0();
void XB_CDECL CrtInitStub_379cf0();
void XB_CDECL CrtInitStub_379d00();
void XB_CDECL CrtInitStub_379d10();
void XB_CDECL CrtInitStub_379d20();
void XB_CDECL CrtInitStub_379d30();

// 0x00234a00: FxPools_Clear, then the 13 large-block pools (apsAddPool count x size,
// 64 bytes to 16 KB). Boot only.
void XB_CDECL Mem_InitLargeBlockPools();
// Cheat console commands (entity argument; command table pointers in .data). Each
// prints GAME_CHEATSNOTENABLED without 0x00954f34 and GAME_MUSTBEALIVECOMMAND unless
// health (+0x350) > 0, then toggles and prints the localized state.
// 0x0024d200: with two arguments, copies argv[1] (0x0092df94) and atol's it (unused).
void XB_CDECL Cmd_SetSpawnPt_f();
// 0x00251450: client (+0x23c) +0x588.
void XB_CDECL Cmd_UFO_f(uint8_t* ent);
// 0x002514f0: client +0x584; 0x0095f679 = on.
void XB_CDECL Cmd_Noclip_f(uint8_t* ent);
// 0x002515a0: entity flags (+0x2a4) bit 2; 0x0095f67a = on.
void XB_CDECL Cmd_Notarget_f(uint8_t* ent);
// 0x00251640: entity flags bit 1; 0x0095f678 = on.
void XB_CDECL Cmd_God_f(uint8_t* ent);
// 0x002789f0 (angles in ESI): angles[i] = Q_fmod(angles[i], 0x00972c10[i]) for i = 0, 1.
extern "C" PLATFORM_HIDDEN void XB_CDECL Angles2Normalize180(float* angles);
extern "C" void Angles2Normalize180_entry(void);
// 0x0028bd30 (pak in EAX, callee pops 4): calls the progress callback (+0x35c) with
// (1 - weight - base) + weight * fraction (weight 0x008d9d40, base 0x008d9d3c).
void XB_REGS1 XB_STDCALL Pak_ReportProgress(uint8_t* pak, float fraction);
// 0x0028bdb0 (pak in ECX): clears the load state and both 100-entry zone tables.
void XB_THISCALL Pak_Clear(uint8_t* pak);
// 0x0028c700 (pak in ECX): the last entry of the zone stack (+0x378, count +0x374), -1 when
// empty.
int32_t XB_THISCALL Pak_TopZoneId(const uint8_t* pak);
// Q3's byte-order helpers (the Xbox is little-endian: the NoSwap ones are used).
float XB_CDECL FloatNoSwap(float value);          // 0x002a96b0 (unhookable: 5 bytes)
float XB_CDECL FloatSwap(float value);            // 0x002a96c0
uint64_t XB_CDECL Long64NoSwap(uint64_t value);   // 0x002a9700
uint64_t XB_CDECL Long64Swap(uint64_t value);     // 0x002a9710
int32_t XB_CDECL LongNoSwap(int32_t value);       // 0x002a9770 (unhookable: 5 bytes)
int32_t XB_CDECL LongSwap(int32_t value);         // 0x002a9780
// 0x002a97b0: loads AX only; the rest of EAX is the caller's.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ShortNoSwap(uint32_t eax, uint32_t value);
extern "C" void ShortNoSwap_entry(void);
uint32_t XB_CDECL ShortSwap(uint32_t value);      // 0x002a97c0

// 0x00297470 (angle in XMM0, result in XMM0; was FUN_00297470): quantized to 16 bits and
// wrapped to (-180, 180].
extern "C" PLATFORM_HIDDEN float XB_CDECL AngleNormalize180Quantized(float angle);
extern "C" void AngleNormalize180Quantized_entry(void);
// 0x0029a020 (a in XMM0, b on the stack; result in XMM0; was FUN_0029a020): a - b quantized
// and wrapped like AngleNormalize180Quantized.
extern "C" PLATFORM_HIDDEN float XB_CDECL AngleDeltaQuantized(float a, float b);
extern "C" void AngleDeltaQuantized_entry(void);

}  // namespace game
