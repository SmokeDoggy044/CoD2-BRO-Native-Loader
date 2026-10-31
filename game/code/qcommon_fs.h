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
// Layout (0x28 B): +0 traits byte, list {allocator, head, size} at +4, bucket vector {allocator, first, last,
// end} at +0x10, then the mask and the highest bucket index in use.
struct HashMap {
    uint8_t traits;
    uint8_t pad01[3];
    uint32_t list_allocator;
    HashNode* head;          // +0x08
    uint32_t size;           // +0x0c
    uint32_t vector_allocator;
    HashNode** buckets;      // +0x14
    HashNode** buckets_last; // +0x18
    HashNode** buckets_end;  // +0x1c
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
static_assert(sizeof(HashMap) == 0x28);
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

// --- Vector and matrix helpers (q_math) ---

// 0x00294190: copies a 3x3 axis (9 words, out in EAX, in in ECX).
void XB_REGS3 XB_CDECL AxisCopy(float* out, uint32_t unused_edx, const float* in);
// 0x00295020: out = transpose(in) for a 3x3 matrix (out in EAX, in in ECX).
void XB_REGS3 XB_CDECL MatrixTranspose(float* out, uint32_t unused_edx, const float* in);
// 0x00295b40 (index in EAX, out in ECX; ECX kept): Q3's ByteToDir over bytedirs
// (0x008d52d8, 162 entries); out of range gives the zero vector.
extern "C" PLATFORM_HIDDEN void XB_CDECL ByteToDir(int32_t index, float* out);
extern "C" void ByteToDir_entry(void);
// 0x00295b80 (dir in ESI): Q3's DirToByte - the bytedirs index with the largest dot
// product above 0 (0 for a null dir). Only AL is written.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL DirToByte(const float* dir);
extern "C" void DirToByte_entry(void);
// 0x00297800 (a in EAX, b in ECX): 2D distance on the x87 (title __CIsqrt), in ST0.
float XB_REGS3 XB_CDECL Vec2Distance(const float* a, uint32_t unused_edx, const float* b);
// 0x00297840 (a in EAX, b in ECX): 3D distance on the x87. No callers (dead).
float XB_REGS3 XB_CDECL Vec3Distance(const float* a, uint32_t unused_edx, const float* b);
// 0x00297700 (v in ESI; length left in XMM0, ECX kept): normalizes a 2D vector in place
// unless its length is 0.
extern "C" PLATFORM_HIDDEN float XB_CDECL Vec2Normalize(float* v);
extern "C" void Vec2Normalize_entry(void);
// 0x00297770 (v in ESI; length left in XMM0, ECX kept): Q3's VectorNormalize.
extern "C" PLATFORM_HIDDEN float XB_CDECL VectorNormalize(float* v);
extern "C" void VectorNormalize_entry(void);
// 0x002975c0 (in in ESI, out in EDI; length left in XMM0, ECX kept): Q3's
// VectorNormalize2 (zero vector out for length 0).
extern "C" PLATFORM_HIDDEN float XB_CDECL VectorNormalize2(const float* in, float* out);
extern "C" void VectorNormalize2_entry(void);
// 0x002956b0 MatrixMultiply (in2 in EAX, out in EDX kept, in1 on the stack): Q3's, out = in1 * in2 (3x3).
// The C is MatrixMultiply3x3: SDL3 (statically linked on Windows) has a MatrixMultiply of its own.
extern "C" PLATFORM_HIDDEN void XB_CDECL MatrixMultiply3x3(const float* in1, const float* in2, float* out);
extern "C" void MatrixMultiply_entry(void);
// 0x002954a0 (in2 in EAX, out in EDX kept, in1 on the stack): 4x3 (axis + origin)
// product, out = in1 * in2.
extern "C" PLATFORM_HIDDEN void XB_CDECL MatrixMultiply43(const float* in1, const float* in2, float* out);
extern "C" void MatrixMultiply43_entry(void);
// 0x00297500 (out in EAX, in in ECX): inverse of an orthonormal 4x3 matrix (transposed
// axis, origin = -(origin * axis^T)).
void XB_REGS3 XB_CDECL MatrixInverse43(float* out, uint32_t unused_edx, const float* in);
// 0x0029a060 (b in ESI, a in EDI, out on the stack): out[i] = AngleDelta(a[i], b[i])
// (Q_fmod); the original keeps a temporary in its argument slot.
extern "C" PLATFORM_HIDDEN void XB_CDECL AnglesNormalize180(const float* b, const float* a, float* out);
extern "C" void AnglesNormalize180_entry(void);
// 0x00299d50 (a in EAX, b in ECX, origin in ESI, plane out in EDI): Q3's PlaneFromPoints
// with (a - origin) x (b - origin); 0 (unnormalized normal, no dist) for a zero normal.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL PlaneFromPoints(const float* a, const float* b, const float* origin,
                                                            float* plane);
extern "C" void PlaneFromPoints_entry(void);
// 0x002a3180 (16-byte aligned vector in ECX, kept): normalizes all four lanes by the xyz
// length from RSQRTSS with one Newton step (DIVPS).
extern "C" PLATFORM_HIDDEN void XB_CDECL Vec3NormalizeSimd(float* v);
extern "C" void Vec3NormalizeSimd_entry(void);

// --- Localization text helpers ---

// Double-byte classifiers for Loc_PopSemicolonToken (code = lead << 8 | trail; ECX and EDX
// kept). The C versions take (EAX, ECX) and return the whole EAX: the ones that set only
// AL keep the rest of their EAX (Big5Char: ECX >> 8).
// 0x002a8b40 (code in EAX): 0xa1a0 < code < 0xa1ae.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsKsc5601Punct(uint32_t code, uint32_t ecx);
extern "C" void Loc_IsKsc5601Punct_entry(void);
// 0x002a8b80 (code in EAX): 0x8140 <= code < 0x8152.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsShiftJisPunct(uint32_t code, uint32_t ecx);
extern "C" void Loc_IsShiftJisPunct_entry(void);
// 0x002a8ba0 (lead in AL, trail in CL).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsShiftJisChar(uint32_t eax, uint32_t ecx);
extern "C" void Loc_IsShiftJisChar_entry(void);
// 0x002a8bd0 (code in EAX): 0xa140 <= code < 0xa154.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsBig5Punct(uint32_t code, uint32_t ecx);
extern "C" void Loc_IsBig5Punct_entry(void);
// 0x002a8bf0 (code in ECX).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsBig5Char(uint32_t eax, uint32_t code);
extern "C" void Loc_IsBig5Char_entry(void);
// 0x002a8c20 (lead in AL, trail in CL): 0 or 1 in EAX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsKsc5601Char(uint32_t eax, uint32_t ecx);
extern "C" void Loc_IsKsc5601Char_entry(void);
// 0x002afa40 (code in ECX): 0 or 1 in EAX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_IsGb2312Char(uint32_t eax, uint32_t code);
extern "C" void Loc_IsGb2312Char_entry(void);
// 0x002a8960 (line in EAX): cuts the line at the first "//" outside double quotes (quotes
// counted from the line start), then strips trailing whitespace from the last search start.
void XB_REGS1 XB_CDECL Loc_StripLineComment(char* line);
// 0x002a89f0 (keyword in EDI, cursor in ESI): 1 and the cursor moved past the keyword and
// following tabs/spaces when *cursor starts with it (_strnicmp), else 0.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Loc_MatchKeyword(const char* keyword, char** cursor);
extern "C" void Loc_MatchKeyword_entry(void);
// 0x002a8a80 (name in EAX): a copy in 0x0098b220 without the extension after the last '.'
// (when it follows every '\\' and '/').
char* XB_REGS1 XB_CDECL Loc_StripFileExtension(const char* name);
// 0x002a8ae0 (path in EAX): a copy in 0x0098b2a0 cut at its last '\\' or '/'.
char* XB_REGS1 XB_CDECL Com_DirName(const char* path);
// 0x002a9570 (n in EAX, s1 in EDX, s2 in ECX): Q3's Q_strncmp (-1/0/1). No callers (dead).
int32_t XB_REGS3 XB_CDECL Q_strncmp(int32_t n, const char* s1, const char* s2);
// 0x002b78b0 (v in EAX): Q3's vtos, "(%i %i %i)" in one of 8 32-byte buffers (0x0098ba68,
// index 0x0098bb68).
char* XB_REGS1 XB_CDECL vtos(const float* v);
// 0x00293d20: Q3's Com_StringContains (title _toupper when not case sensitive); keeps the
// moving str1 in its argument slot.
const char* XB_CDECL Com_StringContains(const char* str1, const char* str2, int32_t casesensitive);
// 0x0029ab20 (extension in EAX, leading '.' skipped): 1 for "cfg" (_stricmp), "menu"* or
// "dat"* (Q_stricmpn 99999).
int32_t XB_REGS1 XB_CDECL FS_IsCfgMenuOrDatFile(const char* ext);

// --- File handles (fsh 0x009477a0, 0x120 bytes each: +0 FILE* or unzFile, +4 unique zip,
// +8 handleSync, +0x18 zipFile) ---

// 0x00295bf0 (f in EAX): fflush of the handle's FILE.
void XB_REGS1 XB_CDECL FS_Flush(int32_t f);
// 0x00297a60 (the body from 0x00297a69 was FUN_00297a69): Q3's FS_Write - fwrite until done
// (one retry after a 0, 0 on -1), fflush with handleSync; returns len, 0 for handle 0.
// Keeps the retry flag in its f argument slot.
int32_t XB_CDECL FS_Write(const void* buffer, int32_t len, int32_t f);
// 0x00297af0: Q3's FS_Read - unzReadCurrentFile for zip handles, else fread until done (a
// second 0 returns the bytes read; -1 calls __break and goes on).
int32_t XB_CDECL FS_Read(void* buffer, int32_t len, int32_t f);
// 0x00297d70: closes the handle (unzCloseCurrentFile + unzClose for unique zip handles, or
// fclose) and clears its slot.
void XB_CDECL FS_FCloseFile(int32_t f);
// 0x00298000 (f in EAX): the zip entry's uncompressed size (unz +0x44), else ftell at the end
// (position restored).
int32_t XB_REGS1 XB_CDECL FS_filelength(int32_t f);

// 0x00293e10 (a in XMM0, b in XMM1; result in XMM0): steps the LCG at 0x008d5a70 (MSVC rand
// constants) and returns b + (a - b) * (seed >> 17) / 32768.
extern "C" PLATFORM_HIDDEN float XB_CDECL Rand_StepSeed(float a, float b);
extern "C" void Rand_StepSeed_entry(void);
// 0x002c0ce0: Q3's Netchan_Init - showpackets/showdrop cvars and the cleared loopbacks.
void XB_CDECL Netchan_Init();
// 0x002f6320: the value (node + 0xc) for key in the int hash map at 0x0095fe00, 0 when absent.
uint8_t* XB_CDECL IntHashMap_FindValue(int32_t key);
// 0x003304f0 (profile in ESI): SV_SetSkillHard/Medium/Easy for skill +0x20 = 0/1/2.
extern "C" PLATFORM_HIDDEN void XB_CDECL Profile_ApplySkill(const uint8_t* profile);
extern "C" void Profile_ApplySkill_entry(void);
// 0x0029e4d0: "toggle <cvar> [values...]": with no values 0 <-> 1 ("%i" of !value), else the
// value after the current one (the first value when not found).
void XB_CDECL Cvar_Toggle_f();

// --- Angles from vectors (the title's arcsine series, see Vec3_PitchDegrees in cm.h) ---
// 0x002a0210 (vector in ESI, angles out in EDI): {vectopitch, vectoyaw_2, 0} (no wrap to [0, 360)).
extern "C" PLATFORM_HIDDEN void XB_CDECL vectoangles_2(const float* v, float* angles);
extern "C" void vectoangles_2_entry(void);
// 0x002a0850 (vector in ESI, angles out in EDI): {Vec3_PitchDegrees, vectoyaw, 0}, both in [0, 360).
extern "C" PLATFORM_HIDDEN void XB_CDECL vectoangles(const float* v, float* angles);
extern "C" void vectoangles_entry(void);
// 0x002a0ea0 (vector in ESI; XMM0): pitch in degrees (-180, 180]: -90 / 90 straight up / down.
extern "C" PLATFORM_HIDDEN float XB_CDECL vectopitch(const float* v);
extern "C" void vectopitch_entry(void);
// 0x002a1540 (vector in ECX; XMM0): yaw atan2(y, x) in degrees (-180, 180], 0 for x = y = 0.
extern "C" PLATFORM_HIDDEN float XB_CDECL vectoyaw_2(const float* v);
extern "C" void vectoyaw_2_entry(void);
// 0x002a1830 (vector in ECX; XMM0): vectoyaw_2 wrapped to [0, 360).
extern "C" PLATFORM_HIDDEN float XB_CDECL vectoyaw(const float* v);
extern "C" void vectoyaw_entry(void);
// 0x002a4440 (plane normal in EAX, yaw in degrees; XMM0): the pitch (Vec3_PitchDegrees) of the yaw
// direction {cos, sin, 0} projected onto the plane. Proposed name (was FUN_002a4440).
extern "C" PLATFORM_HIDDEN float XB_CDECL Plane_PitchAlongYaw(const float* normal, float yaw);
extern "C" void Plane_PitchAlongYaw_entry(void);
// 0x002a4520 (axis in ESI): rows {cos, sin, 0}, {0 - sin, 0 - (0 - cos), 0}, {0, 0, 1} of yaw degrees.
extern "C" PLATFORM_HIDDEN void XB_CDECL YawToAxis(float* axis, float yaw);
extern "C" void YawToAxis_entry(void);
// 0x002a45b0 / 0x002a4610 (angles in EAX): AnglesToAxis rows {forward, 0 - right, up}.
void XB_REGS1 XB_CDECL AnglesToAxisLeft2(const float* angles, float* axis);
void XB_REGS1 XB_CDECL AnglesToAxisLeft(const float* angles, float* axis);
// 0x002db350 (angles in ECX, 4-float rows out in EBX; no callers): AnglesToAxisLeft into rows of 4.
extern "C" PLATFORM_HIDDEN void XB_CDECL AnglesToAxisVec4(const float* angles, float* out);
extern "C" void AnglesToAxisVec4_entry(void);
// 0x0029c8c0 (no callers): the first three lanes of the 4x4 matrix's rows 0-2 into a 3x3 axis.
float* XB_REGS3 XB_CDECL Mat44_GetAxis(float* out, uint32_t, const float* m);
// 0x002bfc10 (no callers): out = axis rows (m[0..8]) . (p - m[9..11]).
const float* XB_REGS3 XB_CDECL Mat43_UntransformPoint(const float* m, uint32_t, float* out, const float* p);
// 0x002a32e0 (no callers): {cos a, sin a} * r for random a in [0, 360) degrees and r in [0, 1).
float* XB_CDECL RandomPointInUnitCircle(float* x, float* y);

// --- Pak zones and memory contexts (pak = the stream pak 0x0095f638: zone vector +0x18 {+0x10 size, +0x14
// data}, slot records +0x38[99], context cache +0x1c4[99], loaded list +0x368 (sentinel +0x36c)) ---
// 0x0028e550: the memory context of zone slot id: cached in +0x1c4[id]; from the slot record's +0x78
// (ZoneRecord_GetContext), else the zone whose +0x28 is id; 0 without zones or for id -1.
uint8_t* XB_THISCALL MemContext_FindById(uint8_t* pak, int32_t id);
// 0x0028fcc0 (flag byte in AL, id in ECX): scope {+0 id, +4 old 0x0095f5d0}; for id != -1 stores AL in
// 0x0095f5d1, sets 0x0095f5d0 and pushes id on the pak's context stack (MemContext_Push). Returns scope.
uint8_t* XB_REGS3 XB_STDCALL MemContextScope_Enter(uint32_t flag, uint32_t, int32_t id, uint8_t* scope);
// 0x0028ccf0 (out in ESI): appends each loaded zone's id (+0x74) to out {count, ids[32]} (up to 32).
extern "C" PLATFORM_HIDDEN void XB_CDECL Pak_CollectLoadedZoneIds(int32_t* out);
extern "C" void Pak_CollectLoadedZoneIds_entry(void);
// 0x0028cb00 (pak in EDI): every zone whose name (+8) contains a loaded zone's name (+8) gets that
// zone's id (+0x74) in +0x28, and +0x2c = 1 when it was 0.
extern "C" PLATFORM_HIDDEN void XB_CDECL Pak_AssignZoneIdsByName(uint8_t* pak);
extern "C" void Pak_AssignZoneIdsByName_entry(void);
// 0x0028e460 (pak in EBX): of the zones not loaded (no slot, or slot state +0xdc 3) the one with the
// lowest Zone_UpdatePriority (ties: the one whose Zone_FindUnloadedChild is another zone); then that
// zone when its slot state is 0, else its unloaded child (itself without one). 0 for none.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Pak_FindNextZoneToLoad(uint8_t* pak);
extern "C" void Pak_FindNextZoneToLoad_entry(void);
// 0x0028e180 (pak in EDX): calls the progress callback +0x35c with (1 - 0x008d9d3c - 0x008d9d40) *
// done / total (0x0095f62c +0x10): per loaded zone in state 1 (+0xdc) LoadBits_Progress(+0x54), in
// state 2/3 (+0xe0) one per flagged (+0xc & 4) item whose +0x18 is the full (+0x14) or half count.
void XB_REGS2 XB_CDECL Pak_UpdateLoadProgress(uint32_t, uint8_t* pak);
// 0x00290020 (pak in ESI): clears the pak, registers PakRender and the progressbar/renderpaks/
// renderheaps cvars, allocates the 0x9000-byte block 0x0095f608 and adds callback 0x0001b9a0 to the
// 32-entry list 0x00988784 (count 0x00988780). Returns pak.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Pak_Init(uint8_t* pak);
extern "C" void Pak_Init_entry(void);
// 0x00290fe0 / 0x00291090: R_LoadCellMeshes(mesh +0x20, release 1 / 0) for each mesh (+0x30 count,
// +0x34 data) of every cell whose zone (+0x24) has id +0x28 == id, over the cell lists chained from
// +0x1cc through each list's +0x3c.
void XB_STDCALL Pak_ReleaseCellMeshesForZone(uint8_t* ctx, int32_t id);
void XB_STDCALL Pak_LoadCellMeshesForZone(uint8_t* ctx, int32_t id);
// 0x002939d0 (mode in EAX; no callers): modes 0 and 2 touch every 32nd byte of the first 4 KB.
void XB_REGS1 XB_CDECL Com_Prefetch(uint32_t mode, const uint8_t* data, int32_t size);
// 0x00293ab0: touches the source like Com_Prefetch, then copies 32-byte blocks from the last down,
// then the rest 16/8/4/2/1 forward.
void XB_CDECL Com_MemcpyPrefetch(uint8_t* dst, const uint8_t* src, int32_t count);
// 0x002999e0: 1 when a console line starts "safe" or "cvar_restart" (that line is emptied), else 0x0095f594.
uint32_t XB_CDECL Com_SafeMode();
// 0x00299890 (out in EBX): the next event from the journal file (com_journal 2) or Sys_GetEvent (written
// to the journal for com_journal 1). Returns out.
extern "C" PLATFORM_HIDDEN SysEvent* XB_CDECL Com_GetRealEvent(SysEvent* out);
extern "C" void Com_GetRealEvent_entry(void);
// 0x0029d4a0 (pointer table 0x002f115b only): 1 when fopen(homepath/gamedir/name, "rb") succeeds.
uint32_t XB_CDECL FS_FileExists(const char* name);
// 0x00250a10 ("where"): prints the client's origin (+0x23c, vtos) and copies it to 0x00947f18.
void XB_CDECL Cmd_Where_f(uint8_t* ent);

// --- Net channel, loopback, parsing, localization, profiles ---
// 0x002b13b0 (chan in EAX, msg in EBX): Q3's Netchan_Process: reads the sequence (+2 qport bytes on a
// server channel, chan +0 == 1); 0 for an old/duplicate one, else dropped (+4) = gap - 1, incoming
// sequence (+0x20) = it, 1. The showdrop/showpackets prints are gone (NET_AdrToString still runs).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Netchan_Process(uint8_t* chan, uint8_t* msg);
extern "C" void Netchan_Process_entry(void);
// 0x002b1480 (chan in EAX): sequence (+0x24, then incremented), qport 0 on a client channel (+0 == 0),
// data, copied into the other side's loopback (0x00928590 + (+0 ^ 1) * 0x93c: data, +0x930 size, +0x938
// count). Returns that loopback.
uint8_t* XB_REGS1 XB_CDECL Netchan_Transmit(uint8_t* chan, int32_t length, const uint8_t* data);
// 0x002ba530 (sock in EAX; adr by value, format, ...): "\xff\xff\xff\xff" + vsprintf into the other
// side's loopback (without the terminator). Returns the length.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL NET_OutOfBandPrint(uint32_t sock, const uint32_t* args);
extern "C" void NET_OutOfBandPrint_entry(void);
// 0x002b9080: COM_ParseExt(data_p, no line breaks), or the ungot token (0x008d4ca0 +0x84; restores
// *data_p and the line from +0x98/+0x94 when +0x88 is set, else returns the parse info itself).
char* XB_THISCALL Com_ParseOnLine(const char** data_p);
// 0x002aa480 (msg in EDI): the string at the read position into 0x0098b3c8 (empty when 256+ long
// or past the size), advancing past its terminator.
extern "C" PLATFORM_HIDDEN char* XB_CDECL MSG_ReadStringLine(uint8_t* msg);
extern "C" void MSG_ReadStringLine_entry(void);
// 0x002af930 (cursor in EAX, line out in EBX; one unused stack word): the next line (up to "\n"),
// skipping the following "\r\n" characters, trailing white space cut and the comment stripped
// (Loc_StripLineComment). 0 at the end of the text.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_ReadLine(const char** cursor, char* line);
extern "C" void Loc_ReadLine_entry(void);
// 0x002ae040 (vtable only): the 32 entries {id, ?, data} (+4, 12 bytes): the one with id gets -1 / 0.
void XB_THISCALL SpgManager_Unload(uint8_t* manager, int32_t id);
// 0x002b6fc0 (this in EAX): ResourceManager base (registered in the 20-entry list 0x009719fc, count
// 0x009719f8), SpgManager vtable, 32 empty entries, 0x00909940 = 0x002ae1b0. Returns this.
uint8_t* XB_REGS1 XB_CDECL SpgManager_Construct(uint8_t* self);
// 0x0030f3f0: once (0x0098ad11): appends "_" and the language name (0x008d5a80[g_languageIndex]) to the
// 31-character name 0x0095f828 (length byte 0x0095f847).
void XB_CDECL Loc_AppendLanguageSuffix();
// 0x0030c240 (header in EDI): 1 when it starts with the build date ("9-21-05", "9-21-05CE" when
// 0x0095e7f8 is set; 25 characters) and its +0x3c0 string equals Save_GetData's.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL Save_CheckVersion(const char* header);
extern "C" void Save_CheckVersion_entry(void);
// 0x00312e30: out[18] = the sums of the first 17 dwords of the unlocked chapters' 0x48-byte records
// (g_chaptersUnlocked, 14 when 0x008dce54 is set; none below 1); out[17] = 0.
void XB_STDCALL Profile_SumMissionStats(const int32_t* chapters, int32_t* out);
// 0x0017be80: push_back of a 24-byte element (grows by 4 through DynArray_Alloc24_FillUndefined).
void XB_THISCALL DynArray_PushBack24(DynArray* array, const uint32_t* value);
// 0x0017cbe0: std_UninitializedFillN4(dest, count, value); returns dest + count.
uint32_t* XB_STDCALL std_vector_UfillN4(uint32_t* dest, uint32_t count, const uint32_t* value);
// 0x0017cd90: a 24-byte list node {next, prev, key, DynArray copy of value + 4} from Scr_Alloc.
uint32_t* XB_STDCALL std_list_BuyNodeKeyArray(uint32_t next, uint32_t prev, const uint32_t* value);

// --- qcommon push 4 (high half) ---

// 0x001e47e0 / 0x001e48f0 / 0x001e5e30 (thiscall, ret 4): push_back of a 12 / 24 / 52-byte element; a full array
// grows to size + 4 (DynArray_Alloc*_FillUndefined, elements copied, old block Scr_Free'd).
void XB_THISCALL DynArray_PushBack12_2(DynArray* array, const uint32_t* value);
void XB_THISCALL DynArray_PushBack24_2(DynArray* array, const uint32_t* value);
void XB_THISCALL DynArray_Push52(DynArray* array, const uint32_t* value);
// 0x0028cbd0 (fli in EAX, pak in ECX): pak +0x18 = fli, its relocation table (+0x18 offset) made absolute and
// relocated; the fli name (+0x1c) picks g_languageIndex (once: "de" 1, "eu" 3 / 4 / 2 by XGetLanguage 5 / 6 / other,
// else 0) and two region flags ("ck"/"ce" 0x0095e7f8, "ck"/"uk" 0x0095ee00); then Loc_AppendLanguageSuffix and
// Pak_AssignZoneIdsByName.
void XB_REGS3 XB_CDECL Pak_ParseFli(uint8_t* fli, uint32_t unused_edx, uint8_t* pak);
// 0x0028e040 (pak in EAX): the StreamMem_CountUsed sum (0x0095f62c, node +0x54) over the loaded zones whose
// Zone_UpdatePriority is not 0. Dead (no reference). The code at 0x0028e0b0 is a second dead function.
uint32_t XB_REGS1 XB_CDECL Pak_CountActiveSlots(uint8_t* pak);
// 0x0028e3e0 (pak in EAX): the loaded zone with the highest positive Zone_UpdatePriority (0 for zones whose
// +0x2c is above 1); null if none. Node +0x78 caches MemContext_FindById(node +0x74).
uint8_t* XB_REGS1 XB_CDECL Pak_FindZoneToUnload(uint8_t* pak);
// 0x00297280 (filter in EAX): Q3's Com_Filter (*, ?, [set] wildcards; _toupper unless casesensitive).
int32_t XB_REGS1 XB_CDECL Com_Filter(const char* filter, const char* name, int32_t casesensitive);
// 0x00299cc0 (name in EAX, filter in EDX): Q3's Com_FilterPath - both copied (127 chars) with '\\' and ':' as '/'.
int32_t XB_REGS2 XB_CDECL Com_FilterPath(const char* name, const char* filter, int32_t casesensitive);
// 0x0029c620: Com_GetRealEvent until an empty event, each one Com_PushEvent'ed (no reference: dead).
void XB_CDECL Com_PushAllRealEvents();
// 0x0029c6b0: Q3's Com_Milliseconds - real events pushed until an empty one; returns its time.
int32_t XB_CDECL Com_Milliseconds();
// 0x0029c700 (out in ESI): Q3's Com_GetEvent - the next pushed event, else Com_GetRealEvent. Returns out.
extern "C" PLATFORM_HIDDEN SysEvent* XB_CDECL Com_GetEvent(SysEvent* out);
extern "C" void Com_GetEvent_entry(void);
// 0x002a26a0 / 0x002a27f0 (angles out in EAX, rows in ECX: stride 16 / 12): Q3's AxisToAngles - vectoangles of
// row 0, row 1 rotated by -yaw about z and -pitch about y (FastSinCos, x87 degree-to-radian), roll from its pitch.
void XB_REGS3 XB_CDECL MatrixToAngles(float* angles, uint32_t unused_edx, const float* matrix);
void XB_REGS3 XB_CDECL AxisToAngles(float* angles, uint32_t unused_edx, const float* axis);
// 0x002a2c40 (vector in ESI; out on the stack): vectoangles into a local, copied to out.
extern "C" PLATFORM_HIDDEN void XB_CDECL vectoangles_ToOut(const float* v, float* out);
extern "C" void vectoangles_ToOut_entry(void);
// 0x002a7da0: once (0x0095f560): permutation 0x0093e758 (shuffled with _rand) and gradients 0x0093eff0
// ((rand % 512 - 256) / 256), the first 258 entries of both repeated after 256.
void XB_CDECL Noise_Init();
// 0x00297990 (list in EAX): Q3's search path list freed (pack: unzClose, buffer, pack; dir; the node).
void XB_REGS1 XB_CDECL FS_FreeSearchPaths(uint8_t* list);
// 0x0023c400 (box in EDI: mins +0, maxs +0x10; 3x3 axis in ESI; bounds on the stack): the 8 corners rotated by the
// axis, each added with 0x00029380 (aligned 4-lane point, lane 3 never set).
extern "C" PLATFORM_HIDDEN void XB_CDECL Bounds_AddPoints8(const float* axis, const float* box, float* bounds);
extern "C" void Bounds_AddPoints8_entry(void);
// 0x0029ea20 (maxs in EAX, mins in ECX, point on the stack): MINPS / MAXPS of aligned 4-lane vectors. Dead.
void XB_REGS3 XB_CDECL Bounds_AddPointSplit(float* maxs, uint32_t unused_edx, float* mins, const float* point);
// 0x002d8a10 (this in ESI): the world tree registry (cm_worldTrees 0x0095f4fc): ResourceManager part, vtable
// 0x00832788, debug draw 0x0001d0d0 on the 32-entry list 0x00988784, +4..+0x19c zeroed, command "cg"
// (0x002afb90: debug flag +4 bit 0). Returns this.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CM_WorldTrees_Construct(uint8_t* self);
extern "C" void CM_WorldTrees_Construct_entry(void);


// --- batch 2 ---

// 0x002c5bb0: the cl_language* cvars (0x0093f7fc cl_language, 0x0092efe8 ..savailable, 0x00929b4c ..translate,
// 0x0093f804 ..warnings, 0x00927b2c ..warningsaserrors; cl_language asked twice); 0x0093e750 = language 8..12.
void XB_CDECL Loc_InitCvars();
// 0x00312130 (dst in EAX, src in ECX): a profile's saved fields copied (name 16 bytes, +0x10..+0xe4, +0x39c,
// +0x39f). Returns dst.
uint8_t* XB_REGS3 XB_CDECL Profile_Copy(uint8_t* dst, uint32_t unused_edx, uint8_t* src);
// 0x003124f0 (thiscall): vtable 0x008287dc, Z_Free of the four buffers +0x11c0 and of +0x11bc.
void XB_THISCALL ProfileSave_Dtor(uint8_t* self);
// 0x00378c10: static initializer - 0x00909880 = the 4x4 identity (Mat44_FromRows).
void XB_CDECL StaticInit_Mat44();
// 0x00379df0: static initializer - 0x008bb768 = 0x007c4d90.
void XB_CDECL StaticInit_Vtable_8bb768();
// 0x0028f5c0 (name in EAX; pak on the stack, ret 4): the value of name in the string maps of the pak's zone chain
// (id +0x1cc, zone +4 + 4 * id, next id +0x3c); 0 if none.
uint32_t XB_REGS1 XB_STDCALL Pak_FindZoneByName(const char* name, uint8_t* pak);
// 0x0028fad0 (context in EAX, name in ECX): the value of name in the string map of the context's zone (+0x18).
uint32_t XB_REGS3 XB_CDECL Pak_FindZone(uint8_t* context, uint32_t unused_edx, const char* name);
// 0x0028bfc0 (thiscall, ret 4): mem freed in the context heap (+0x90, count +0x8c; range +0x474..+0x478) that holds
// it, else in a child context (+0xc4, count +0xc0); 1 if freed (or null), else 0.
uint8_t XB_THISCALL MemContext_Free_r(uint8_t* context, uint8_t* mem);
// 0x0028c0f0 (thiscall, ret 8): Heap_AllocAligned(align, size) in the first context heap with room (used +0x488,
// capacity +0x484), else in a child context; size -1 = 0x400. Null if none.
uint8_t* XB_THISCALL MemContext_Alloc_r(uint8_t* context, uint32_t align, uint32_t size);
// 0x0028c560 (slot in EAX, pak in ECX, align in EDI, size in ESI): MemContext_Alloc_r in the pak's context for
// that slot (+0x38, slots 0..0x62), else (or slot -1) Heap_AllocAligned.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL MemContext_AllocOrHeap(int32_t slot, uint8_t* pak, uint32_t align,
                                                                 uint32_t size);
extern "C" void MemContext_AllocOrHeap_entry(void);
// 0x0029d5f0 (key in ESI): the localized text of key (Loc_HashKey, Zone_FindByHash in rm_locStrings, entry +8);
// key itself if not found, "NO STRING" for an empty entry. The code at 0x0029d620 is another function.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL Loc_GetString(const char* key);
extern "C" void Loc_GetString_entry(void);
// 0x0028bc50 (thiscall, ret 4): the zone's children (+0x20, count +0x1c) first, then its id (+0x28) appended to
// the pak's list (+0x378, count +0x374, at most 0x80).
void XB_THISCALL Pak_PushZoneIds_r(uint8_t* pak, uint8_t* zone);
// 0x00248dd0 (dst in EAX, src in ECX): src lowercased (tolower) without control characters, ESC skipping two. Dead.
void XB_REGS3 XB_CDECL Q_CleanStrLower(char* dst, uint32_t unused_edx, const char* src);
// 0x002a2510 (from address in ESI; msg on the stack): SV_PacketEvent(msg in EAX, the 5-word address by value),
// Sys_Milliseconds before and after while com_speeds is set (results unused). The code at 0x002a2560 is another
// (dead) function.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Com_RunAndTimeServerPacket(uint32_t entry, uint32_t msg,
                                                                     const uint32_t* from, uint32_t ebx, uint32_t edi);
extern "C" void Com_RunAndTimeServerPacket_entry(void);
// 0x002bdc80 (translation in EDX, out in ECX; quaternion on the stack, ret 4): a 4x4 rotation from the quaternion
// scaled by 2 / |q|^2 (each row's 4th word repeats its first diagonal-term pair), row 3 = translation or {0,0,0,1}.
void XB_REGS3 XB_STDCALL Quat_ToMat44(uint32_t unused_eax, const float* translation, float* out, const float* q);
// 0x002ae350 (matrix in EAX, translation out in EDX or null; quaternion out on the stack, ret 4): the matrix's
// rotation as a quaternion (x87 sqrt of the trace or the largest diagonal term); 1 if |q| is within 0.0001 of 1.
uint32_t XB_REGS2 XB_STDCALL Mat44_ToQuatTranslation(const float* m, float* translation, float* q);


// --- batch 3 ---

// 0x002a8410: the 512-node ring at 0x00965cfa (0x30-byte nodes, prev / next index words +0 / +2), node 0's
// floats +6 / +0xa and words +0xe / +0x10 zeroed, the name string 0x00965ce8 = "end".
void XB_CDECL Com_InitNodeRing();
// 0x0029c570: 0x0095f590 cleared, the ring's name string cleared (ScriptString_Clear unless empty),
// Com_InitNodeRing, 0x0095f590 = 1.
void XB_CDECL Com_ResetNodeRing();
// 0x0029c5b0: Q3's Com_Shutdown - the ring flag and name cleared, logfile and com_journalFile closed.
void XB_CDECL Com_Shutdown();
// 0x0029ea50 (path in EDI, dir in EBX): FS_AddGameDirectory(path, dir, 1, i) for i = 13..0, then (path, dir, 0, 0).
extern "C" PLATFORM_HIDDEN void XB_CDECL FS_AddGameDirectories(uint32_t path, uint32_t dir);
extern "C" void FS_AddGameDirectories_entry(void);
// 0x002ca9e0: cl_language asked again, 0x0093e750 = language 8..12, cl_languagesavailable forced to "1", then
// Loc_ClearStrings (tail call).
void XB_CDECL Loc_Init();
// 0x00300b30: {value, name as a 32-byte fixed string} inserted into the name map 0x0095fe00 (HashMap_Insert).
void XB_CDECL NameMap_Insert(uint32_t value, const char* name);
// 0x002ca7e0 (key in EAX, mode in EDI; out, context on the stack): out = Loc_GetString(key) when
// cl_languagetranslate is set and key has 2+ characters, else key; returns 1 (0 for a failed lookup in mode 1).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Loc_TranslateString(const char* key, char* out, const char* context,
                                                                int32_t mode);
extern "C" void Loc_TranslateString_entry(void);
// 0x002fdef0 (name in EAX): the name map entry of ScrIface_HashStringNoCase(name) erased; 1 if found. Dead.
uint8_t XB_REGS1 XB_CDECL IntHashMap_EraseByString(const char* name);
// 0x0029e8d0: "freeze <seconds>": spins on Com_Milliseconds until the time has passed (debug command).
void XB_CDECL Com_Freeze_f();
// 0x002c90c0 (rotation vector in EDX, aligned; quaternion out in ECX): angle = |v| (RSQRTSS + one Newton step),
// axis = v / angle; {axis sin(angle/2), cos(angle/2)} normalized on the x87; identity for a zero vector.
void XB_REGS3 XB_CDECL Quat_FromRotationVector(uint32_t unused_eax, const float* v, float* out);


// --- qcommon push 5 (high half) ---

// 0x0029aac0: Q3's FS_Printf - _vsprintf into 4 KB, FS_Write of its length.
void XB_CDECL FS_Printf(int32_t f, const char* format, ...);
// 0x0029a8b0 (offset in ECX; f, origin on the stack): Q3's FS_Seek. Zip handles: forward by reading in
// 64 KB steps, backward by reopening the entry (+0x14 info position) first; origin 0 cur, 1 end, 2 set,
// others -1. FILE handles: fseek (other origins 0).
int32_t XB_REGS3 XB_CDECL FS_Seek(uint32_t unused_eax, uint32_t unused_edx, int32_t offset, int32_t f, int32_t origin);
// 0x00297df0 (to in EAX, from in ECX): Q3's FS_CopyFile (journal files skipped; Z_Malloc'd copy, FS_CreatePath).
void XB_REGS3 XB_CDECL FS_CopyFile(char* to, uint32_t unused_edx, const char* from);
// 0x0029cb60 (dir in ECX; path, localized, language on the stack): Q3's FS_AddGameDirectory. Localized
// ones are "dir_<language name>" (table 0x008d4ca8, 8 B each), added only when the folder has files and
// linked after the last unlocalized node; others set fs_gamedir and go first. Skips a path+dir already on
// the list.
void XB_REGS3 XB_CDECL FS_AddGameDirectory(uint32_t unused_eax, uint32_t unused_edx, const char* dir,
                                           const char* path, int32_t localized, int32_t language);
// 0x0029cde0: Q3's FS_FOpenFileRead. No file: 1 when a pack holds it or the folder file opens. Else a
// handle (FS_HandleForFile(quiet)), folder files only (a pack hit skips the search path; ".bsp" names
// remember the source in 0x00940200); fs_restrict/0x0093fcc8 limit unlocalized paths to cfg/menu/dat;
// fs_copyfiles copies cd-path files to the base path. Returns the length, -1 (*file = 0) when not found.
int32_t XB_CDECL FS_FOpenFileRead(const char* filename, int32_t* file, int32_t unique, int32_t quiet);
// 0x0029d1f0 (filename in EBX): Q3's FS_FOpenFileAppend ("ab" under fs_homepath/fs_gamedir); 0 on failure.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL FS_FOpenFileAppend(const char* filename);
extern "C" void FS_FOpenFileAppend_entry(void);
// 0x0029d3c0 (filename in EBX): Q3's FS_FOpenFileWrite ("wb"); 0 on failure.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL FS_FOpenFileWrite(const char* filename);
extern "C" void FS_FOpenFileWrite_entry(void);
// 0x002a1b30: Q3's FS_FOpenFileByMode (0 read, 1 write, 2 append, 3 append + sync; else __break and
// 0x1b39); records the position, size and sync in the handle (handle 0's sync too).
int32_t XB_CDECL FS_FOpenFileByMode(const char* qpath, int32_t* f, int32_t mode);
// 0x002a1fd0: Q3's FS_ReadFile; ".cfg" names come from "config\" and go through the journal
// (com_journal 1 records, 2 plays back from com_journalDataFile).
int32_t XB_CDECL FS_ReadFile(const char* qpath, void** buffer);
// 0x002c6140 (text in EBX): Q3's Cmd_ExecuteString - command list (found commands moved to the front),
// Cvar_Command, cgame (when 0x00947704 is set) and game VM, else forwarded to the server.
extern "C" PLATFORM_HIDDEN void XB_CDECL Cmd_ExecuteString(const char* text);
extern "C" void Cmd_ExecuteString_entry(void);
// 0x002ba750: the second command buffer (0x00929c10, size 0x00929c18) through
// Cmd_ExecuteRegisteredCommand, unless a server runs with 0x0092f548 set.
void XB_CDECL Cbuf_ExecuteBuffer();
// 0x002c62b0: Q3's Cbuf_Execute (cmd_wait), then Cbuf_ExecuteBuffer.
// Takes the entry ESP (the original's line buffer address stays in 0x00927f50).
extern "C" PLATFORM_HIDDEN void XB_CDECL Cbuf_Execute(uint32_t entry_esp);
extern "C" void Cbuf_Execute_entry(void);
// 0x002c63a0: Q3's Cbuf_ExecuteText (0 now, 1 insert, 2 append); keeps the text's length in its
// exec_when argument slot.
// Takes the entry ESP (exec_when, text above it); returns the tail-jump target (Cbuf_Execute) or 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Cbuf_ExecuteText(uint32_t entry_esp);
extern "C" void Cbuf_ExecuteText_entry(void);
// 0x002a9c10 (was FUN_002a9c10): __break 1 "File %s, line %i: %s" with the parse info at 0x008d4ca0.
void XB_CDECL Com_ParseError(const char* format, ...);
// 0x002c5780 (was FUN_002c5780; text in EAX): text without leading blanks and one leading quote, trailing
// blanks and one trailing quote, in a function-static std::string (0x0098bcbc); its c_str().
const char* XB_REGS1 XB_CDECL Loc_TrimQuotedValue(const char* text);
// 0x002c5ac0 (was FUN_002c5ac0; state in EBX, keep_lists on the stack): clears the string file state's map
// (+0x78), unless keep_lists its string vector (+0x8c) and set (+0x98), its count and both strings.
extern "C" PLATFORM_HIDDEN void* XB_CDECL Loc_ResetStringFileState(uint8_t* state, uint32_t keep_lists);
extern "C" void Loc_ResetStringFileState_entry(void);
// 0x0031ac60 (was FUN_0031ac60): 1 when the save device has the blocks for a 0x400-byte save (or
// Save_GetFreeSpace says -10).
uint8_t XB_CDECL Save_HasEnoughFreeSpace();

// 0x002a1c50: Q3's FS_Startup - the fs_ cvars ("fs_ignoreLozalized" sic), then the cd path's and the base
// path's game folders (14 localized + 1 each), the home path's (FS_AddGameDirectories), fs_basegame's and
// fs_game's folders for the empty game name; fs_game's modified flag cleared.
void XB_CDECL FS_Startup(const char* game_name);
// 0x0029a5c0: Q3's FS_Shutdown - handles 1-2 with a size closed, search paths and packs freed (only those
// not kept at 0x0095f580 / 0x0095f584 unless close_files), the five file commands removed (the last one a
// tail jump through its argument slot).
void XB_CDECL FS_Shutdown(int32_t close_files);
// 0x002a3370: Q3's FS_Restart - default.cfg must load (else the last valid paths and a recursive restart, or
// __break); "exec bro.cfg" unless the game is unchanged or safe mode; the paths remembered.
void XB_CDECL FS_Restart(int32_t checksum_feed);
// 0x002a3490: Q3's FS_InitFilesystem (startup variables, Loc_InitCvars, FS_Startup, default.cfg check).
void XB_CDECL FS_InitFilesystem();
// 0x002a40f0: Q3's Com_EventLoop - key/char/mouse/axis/console/packet events, then the loopback packets
// (client, then server while it runs); returns the time of the empty event.
int32_t XB_CDECL Com_EventLoop();
// 0x002a70c0: Q3's Com_Frame with the pause screen (0x008d50dd/0x008d50dc: sounds paused, resume on button
// 0xe), the frame timing (com_maxfps), Scr_RunThreads, SV_Frame, CL_Frame and frame advance.
void XB_CDECL Com_Frame();

// 0x0029fe70: Q3's Com_InitJournaling (journal 1 writes, 2 reads journal.dat / journaldata.dat; com_journal
// reset to 0 when either is missing).
void XB_CDECL Com_InitJournaling();
// 0x002a6dc0: Q3's Com_Quit_f without Sys_Quit (unless 0x00947b08): CL/SV/Com/FS shutdown.
void XB_CDECL Com_Quit_f();
// 0x002cae20: Q3's Cmd_Exec_f; "style_*" files also set the control style 0x008dce7c (h 0, e 1, f 2, g 3,
// others 4).
void XB_CDECL Cmd_Exec_f();
// 0x0029e3d0: Q3's Cvar_Set_f (the arguments from 2 joined with spaces, length counted as strlen(arg + 1)).
void XB_CDECL Cvar_Set_f();
// 0x0029ea80: Q3's FS_ListFilteredFiles for folders only (packs skipped; fs_restrict lists "svg" only);
// keeps the stripped path and "" extension in its argument slots.
char** XB_CDECL FS_ListFilteredFiles(const char* path, const char* extension, const char* filter, int32_t* numfiles);

// 0x0017bd20: resize of a DynArray of 24-byte elements; new ones (and a grown block from
// DynArray_Alloc24_FillUndefined) hold the undefined float 0x00922eb8.
void XB_THISCALL DynArray_Resize24(DynArray* array, uint32_t size, uint32_t capacity);
// 0x0017be40: element `index` (24 bytes), the array grown to index + 1 (capacity index + 4) first.
uint32_t* XB_THISCALL DynArray_At24(DynArray* array, uint32_t index);
// 0x0017bfb0: resize of a DynArray of 8-byte elements {0, g_scrUndefinedBool}.
void XB_THISCALL DynArray_Resize8(DynArray* array, uint32_t size, uint32_t capacity);
// 0x0017c050: element `index` (8 bytes), grown like DynArray_At24.
uint32_t* XB_THISCALL DynArray_At(DynArray* array, uint32_t index);
// 0x0017cea0: std::list::erase of a node (the value's script block and the node freed) unless it is the head;
// *out = the next node.
uint8_t** XB_THISCALL std_list_Erase(uint8_t* list, uint8_t** out, uint8_t* node);
// 0x0017cef0 and six more instances (0x0017d140, 0x0017d390, 0x0017d680, 0x0017d970, 0x0017de40,
// 0x001e6510): MSVC 7.1 vector<dword>::_Insert_n (growth by half, std_vector_Xlen past 0x3fffffff).
void XB_THISCALL std_vector_InsertN4(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
// 0x0017dbc0 and six more instances (0x0017dc30, 0x0017dca0, 0x0017dd10, 0x0017ddd0, 0x0017e370, 0x001e6760):
// vector<dword>::resize(size, value).
void XB_THISCALL std_vector_Resize4(Vector4* vector, uint32_t size, uint32_t value);
// 0x0017dd80: vector<dword>::_Buy - an empty vector, then room for count (0 for none).
uint8_t XB_THISCALL std_vector_Buy4(Vector4* vector, uint32_t count);

// The other std_vector_InsertN4 / std_vector_Resize4 instances (addresses at those two).
void XB_THISCALL std_vector_InsertN4_2(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_Resize4_2(Vector4* vector, uint32_t size, uint32_t value);
void XB_THISCALL std_vector_InsertN4_3(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_Resize4_3(Vector4* vector, uint32_t size, uint32_t value);
void XB_THISCALL std_vector_InsertN4_4(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_Resize4_4(Vector4* vector, uint32_t size, uint32_t value);
void XB_THISCALL std_vector_InsertN4_5(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_Resize4_5(Vector4* vector, uint32_t size, uint32_t value);
void XB_THISCALL std_vector_InsertN4_6(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_Resize4_6(Vector4* vector, uint32_t size, uint32_t value);
void XB_THISCALL std_vector_InsertN4_7(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_Resize4_7(Vector4* vector, uint32_t size, uint32_t value);

// --- Script-heap hash map instances (MSVC 7.1 stdext::hash_map) ---
// Each template instance below has the same body; they differ only in their callees (node size: the head
// node and node copy helpers, some in the PROLOGUE/BUCHHOLZ sections).

// 0x0017e090/0x0017e110/0x0017e190 (callee pops 8): vector(count, value): Buy4, then the fill.
void XB_THISCALL std_vector_ConstructN4(Vector4* vector, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_ConstructN4_2(Vector4* vector, uint32_t count, const uint32_t* value);
void XB_THISCALL std_vector_ConstructN4_3(Vector4* vector, uint32_t count, const uint32_t* value);
// 0x0017e3e0/0x0017f590/0x0017fed0 (callee pops 8): empty map: traits byte copied, a new list head,
// 9 buckets all pointing at the head, mask 1, max index 1.
HashMap* XB_THISCALL HashMap_Ctor(HashMap* map, const uint8_t* traits, uint32_t allocator);
HashMap* XB_THISCALL HashMap_Ctor_2(HashMap* map, const uint8_t* traits, uint32_t allocator);
HashMap* XB_THISCALL HashMap_Ctor_3(HashMap* map, const uint8_t* traits, uint32_t allocator);
// 0x0017e460/0x0017e570/0x0017f750/0x001800f0/0x00180200 (callee pops 4): operator=: buckets resized
// to the source's count (filled with the head), mask copied, list cleared and the source's nodes
// inserted, then each bucket set to the copy's node at the source bucket's position.
void XB_THISCALL HashMap_CopyFrom(HashMap* map, const HashMap* source);
void XB_THISCALL HashMap_CopyFrom_2(HashMap* map, const HashMap* source);
void XB_THISCALL HashMap_CopyFrom_3(HashMap* map, const HashMap* source);
void XB_THISCALL HashMap_CopyFrom_4(HashMap* map, const HashMap* source);
void XB_THISCALL HashMap_CopyFrom_5(HashMap* map, const HashMap* source);
// 0x0017eb20/0x0017eb90/0x0017f9c0/0x00180310/0x00180820 (callee pops 4): copy constructor.
HashMap* XB_THISCALL HashMap_CopyCtor(HashMap* map, const HashMap* source);
HashMap* XB_THISCALL HashMap_CopyCtor_2(HashMap* map, const HashMap* source);
HashMap* XB_THISCALL HashMap_CopyCtor_3(HashMap* map, const HashMap* source);
HashMap* XB_THISCALL HashMap_CopyCtor_4(HashMap* map, const HashMap* source);
HashMap* XB_THISCALL HashMap_CopyCtor_5(HashMap* map, const HashMap* source);
// 0x0017ce30: vector _Xlen: throws length_error("vector<T> too long") (never returns).
void XB_CDECL std_vector_Xlen();
// 0x0017d5e0/0x0017d8d0 (callee pops 4): list size += count, length_error past 0x05d1745d/0x0fffffff.
uint32_t XB_THISCALL std_list_IncSize(uint8_t* list, uint32_t count);
uint32_t XB_THISCALL std_list_IncSize_2(uint8_t* list, uint32_t count);
// 0x0017e210/0x0017e2c0/0x0017f4e0/0x0017f860/0x0017f910 (callee pops 0x10): list insert(where, first,
// last); the fourth word (the source container) is unused.
void XB_THISCALL std_list_InsertRange(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t unused);
void XB_THISCALL std_list_InsertRange_2(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t unused);
void XB_THISCALL std_list_InsertRange_3(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t unused);
void XB_THISCALL std_list_InsertRange_4(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t unused);
void XB_THISCALL std_list_InsertRange_5(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t unused);
// 0x0017ec00/0x0017f440/0x0017f490 (callee pops 8): std_list_Erase with the value destructor at
// 0x0045f600/0x004610d0/0x004614d0.
uint8_t** XB_THISCALL std_list_Erase_2(uint8_t* list, uint8_t** out, uint8_t* node);
uint8_t** XB_THISCALL std_list_Erase_3(uint8_t* list, uint8_t** out, uint8_t* node);
uint8_t** XB_THISCALL std_list_Erase_4(uint8_t* list, uint8_t** out, uint8_t* node);
// 0x0017f320/0x0017f3b0/0x00180a10/0x00180aa0 (callee pops 0xc): 0x34-byte node {next, prev, key, HashMap
// copy of value + 4}.
uint8_t* XB_STDCALL std_list_BuyNodeHashMap(uint8_t* next, uint8_t* prev, const uint32_t* value);
uint8_t* XB_STDCALL std_list_BuyNodeHashMap_2(uint8_t* next, uint8_t* prev, const uint32_t* value);
uint8_t* XB_STDCALL std_list_BuyNodeHashMap_3(uint8_t* next, uint8_t* prev, const uint32_t* value);
uint8_t* XB_STDCALL std_list_BuyNodeHashMap_4(uint8_t* next, uint8_t* prev, const uint32_t* value);
// 0x0017f610/0x0017f6b0 (callee pops 0xc): 0x18-byte node {next, prev, key, DynArray of hash maps assigned
// from value + 4}.
uint8_t* XB_STDCALL std_list_BuyNodeKeyArray_2(uint8_t* next, uint8_t* prev, const uint32_t* value);
uint8_t* XB_STDCALL std_list_BuyNodeKeyArray_3(uint8_t* next, uint8_t* prev, const uint32_t* value);
// 0x0017e680/0x0017e8d0/0x0017fa30/0x0017fc80/0x00180380/0x001805d0/0x00180b30/0x00180d80/0x001e6a00 (callee
// pops 8): hash_map::insert(value) with one linear-hashing split step first; out = {node, inserted byte}.
// _11 compares signed keys.
uint8_t* XB_THISCALL HashMap_Insert_2(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_3(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_4(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_5(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_6(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_7(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_8(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_9(HashMap* map, uint8_t* out, const uint32_t* value);
uint8_t* XB_THISCALL HashMap_Insert_11(HashMap* map, uint8_t* out, const uint32_t* value);
// 0x0017ed70/0x0017edf0/0x001e6c50/0x0017ff50/0x00180020/0x00180fd0/0x001810a0/0x00180890/0x00180950 (callee
// pops 4): operator[](key): the value's address (node + 0xc), a default value inserted when missing
// (Index: three undefined floats; _2/_3: an int array; _4.._7: a hash map; _8/_9: an array of hash maps).
uint8_t* XB_THISCALL HashMap_Index(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_2(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_3(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_4(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_5(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_6(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_7(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_8(HashMap* map, const uint32_t* key);
uint8_t* XB_THISCALL HashMap_Index_9(HashMap* map, const uint32_t* key);
// 0x00376750: static init: constructs the localized-string tables at 0x0095fe70 (0x0001d930, object in
// ESI) and registers AtExit_LocStrings.
void XB_CDECL StaticInit_LocStrings();
// 0x00379950: atexit: destroys the localized-string tables (0x0001d9d0).
void XB_CDECL AtExit_LocStrings();

// --- qcommon push 7 (high half) ---

// 0x0017ec50 (callee pops 4): count hash maps (0x28 B) from Scr_Alloc, each constructed by the
// PROLOGUE-section HashMap_Ctor 0x0045f8b0 with traits = the original's argument slot, which then holds the
// element's own address (the traits byte is the address's low byte). _2 (0x0017ece0): ctor 0x0045fc30.
uint8_t* XB_STDCALL DynArray_AllocHashMap(uint32_t count);
uint8_t* XB_STDCALL DynArray_AllocHashMap_2(uint32_t count);
// 0x0017eec0: resize an array of hash maps; new slots constructed (traits as in DynArray_AllocHashMap),
// dropped ones destroyed; past the capacity a new block of `capacity` maps gets min(size, old size) maps
// assigned (HashMap_CopyFrom) and the old maps are destroyed and freed. _2 (0x0017f0d0): other instances.
void XB_THISCALL DynArray_ResizeHashMap(DynArray* array, uint32_t size, uint32_t capacity);
void XB_THISCALL DynArray_ResizeHashMap_2(DynArray* array, uint32_t size, uint32_t capacity);
// 0x0017f020: copy-assign an array of hash maps (old maps destroyed and freed first). _2: 0x0017f230.
void XB_THISCALL DynArray_AssignHashMap(DynArray* array, const DynArray* source);
void XB_THISCALL DynArray_AssignHashMap_2(DynArray* array, const DynArray* source);
// 0x0017f2e0: element `index` of an array of hash maps, growing it to index + 1 (capacity index + 4).
uint8_t* XB_THISCALL DynArray_AtHashMap(DynArray* array, uint32_t index);
// 0x001a95e0: shuffles a 4-byte array: each element i swaps with element Scr_RandomInt(size) (every access
// through the array's At, which grows it: DynArray_Resize 0x00015a90). The original keeps the byte offset
// of i in its own argument slot. _2 (0x001a96d0) / ShuffleInt (0x001a98e0): resize 0x0045cad0 (ShuffleInt
// copies through XMM0); _3 (0x001a99d0): resize 0x0045c4a0. Shuffle12 (0x001a97c0): 12-byte elements
// (resize 0x0045cde0). ShuffleString (0x001a9ac0): script strings (copy/assign/release; resize 0x0045c2e0).
void XB_CDECL DynArray_Shuffle(DynArray* array);
void XB_CDECL DynArray_Shuffle_2(DynArray* array);
void XB_CDECL DynArray_Shuffle12(DynArray* array);
void XB_CDECL DynArray_ShuffleInt(DynArray* array);
void XB_CDECL DynArray_Shuffle_3(DynArray* array);
void XB_CDECL DynArray_ShuffleString(DynArray* array);

// 28-byte script record: {word, script string, word, script string, three words}; constructor 0x0045bcd0,
// destructor 0x00458fd0 (releases both strings).
// 0x001e4880: erase(where): later records assigned down one, the last one destroyed, size - 1.
void XB_THISCALL DynArray_Erase28(DynArray* array, uint8_t* where);
// 0x001e58a0 (callee pops 8): count constructed records from Scr_Alloc.
uint8_t* XB_STDCALL DynArray_Alloc28(uint32_t count, uint32_t unused);
// 0x001e59f0: resize; new records constructed, dropped ones' strings released; past the capacity a new
// block (0x0045c120, capacity records) gets the first min(size, old size) assigned, the old block's strings
// are released and it is freed.
void XB_THISCALL DynArray_Reserve28(DynArray* array, uint32_t size, uint32_t capacity);
// 0x001e5bc0: push_back(value): a full array moves to a new block of size + 4 records first.
void XB_THISCALL DynArray_PushBack28(DynArray* array, const uint8_t* value);
// 0x001e5ce0: resize a 52-byte array: new slots get DynArray_Alloc52_FillUndefined's values; past the
// capacity a new block (0x0045c1a0, capacity elements) gets the old elements copied, the old block freed.
void XB_THISCALL DynArray_Resize52(DynArray* array, uint32_t size, uint32_t capacity);

// 0x001e63d0 (callee pops 12): list node {next, prev, key, DynArray assigned (0x0045c270) from value + 4}.
uint8_t* XB_STDCALL std_list_BuyNodeKeyArray_4(uint8_t* next, uint8_t* prev, const uint32_t* value);
// 0x001e6470: list size += count; past 0x0fffffff MSVC's "list<T> too long" length_error. Returns the size.
uint32_t XB_THISCALL std_list_IncSize_3(uint8_t* list, uint32_t count);
// 0x001e67d0: list::insert(where, first, last) for HashMap_CopyFrom_6 (PROLOGUE BuyNode 0x0045caa0,
// IncSize 0x0045ead0).
void XB_THISCALL std_list_InsertRange_6(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t);
// 0x001e6880 / 0x001e6990: HashMap_CopyFrom / HashMap_CopyCtor, sixth instance (PROLOGUE helpers).
void XB_THISCALL HashMap_CopyFrom_6(HashMap* map, const HashMap* source);
HashMap* XB_THISCALL HashMap_CopyCtor_6(HashMap* map, const HashMap* source);
// 0x001ee881 / 0x001ee8c1: MSVC std::_String_base::_Xran / _Xlen: throw out_of_range("invalid string
// position") / length_error("string too long") (never return).
void XB_CDECL std_string_Xran();
void XB_CDECL std_string_Xlen();
// 0x00223800 (basedir in EAX): Q3's Sys_ListFilteredFiles without the recursion: FindFirstFile over
// "basedir\subdirs\*" (or "basedir\*" for an empty subdirs), every entry but the "." / ".." / "CVS"
// directories named "subdirs\name" (or "name" for a null subdirs) and kept when Com_FilterPath matches
// (CopyString into list[*numfiles]); stops at 0xfff names.
void XB_REGS1 XB_CDECL Sys_ListFilteredFiles(const char* basedir, const char* subdirs, const char* filter,
                                            char** list, int32_t* numfiles);
// 0x00223bf0 (filter in EDX, extension in ECX): Q3's Sys_ListFiles: with a filter, Sys_ListFilteredFiles
// from `directory`; else "directory\*extension" (extension "/" = directories), files or (wantsubs)
// directories, skipping "." / ".." / "CVS", at most 0xfff. Returns a null-terminated Z_Malloc copy of the
// list, null when empty.
char** XB_REGS3 XB_CDECL Sys_ListFiles(uint32_t, const char* filter, const char* extension, const char* directory,
                                       int32_t* numfiles, int32_t wantsubs);
// 0x00236c80 (large pools flag in BL, effect pool size in EAX): stores the flag (0x0095f6dc), the effect
// pool sizes (0x400000 with large pools, else EAX), FX_InitParticleLibrary, the large-block pools or four
// 400-entry small pools (16..128 bytes), then the 0x40000-byte zone heap 0x0095f6d8. Boot only (Sys_Main).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Mem_InitSmallBlockPools(uint32_t effect_pool_size, uint32_t large);
extern "C" void Mem_InitSmallBlockPools_entry(void);
// Game console commands (cdecl, ent = the issuing client's gentity; 0x00954f34 = cheats enabled).
// 0x0024ea60: "menuresponse <menu> <response>": notify "menuresponse" on ent with (menu name, response); a
// menu index 0 becomes config string 0x272; wrong argument counts send ("", "bad").
void XB_CDECL Cmd_MenuResponse_f(uint8_t* ent);
// 0x00258d90: "jumptonode <n>" (cheats): teleports to path node n (origin +0x44, yaw +0x50).
void XB_CDECL Cmd_JumpToNode_f(uint8_t* ent);
// 0x00258eb0: "setviewpos x y z yaw" (cheats): TeleportPlayer, SetClientViewAngle, SV_LinkEntity.
void XB_CDECL Cmd_SetViewpos_f(uint8_t* ent);
// 0x002694e0: drop the current weapon (Drop_Weapon, event 0x97) when 0x00955184 and cheats are set.
void XB_CDECL Cmd_DropWeapon_f(uint8_t* ent);
// 0x00269520: "kill": unless 0x0095bf9c, health 0, flag bit 0 cleared, player_die (mod 0x15, damage 100000).
void XB_CDECL Cmd_Kill_f(uint8_t* ent);
// 0x0026d480: Q3's Cmd_Give_f, BRO form (cheats, alive): every weapon (BG_GivePlayerWeapon with 0x00986018
// set), health + amount (or max health 0x00950cec), ammo for the current weapon (amount) or 998 for every
// owned weapon; "allammo <n>" adds n to every owned weapon.
void XB_CDECL Cmd_Give_f(uint8_t* ent);
// 0x00271300 (ent in ESI): the use button: a mounted turret's use, the cursor-hint target's use/touch
// (doors via TryDoor, trigger_use, type 0xe/0x2/0xa entities, script brushmodels) with "trigger"/"touch"
// notifies. Returns 1 when something was used.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Cmd_Activate_f(uint8_t* ent, uint32_t entry_ebx, uint32_t entry_edi);
extern "C" void Cmd_Activate_f_entry(void);

}  // namespace game
