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
// 0x002956b0 (in2 in EAX, out in EDX kept, in1 on the stack): Q3's MatrixMultiply,
// out = in1 * in2 (3x3).
extern "C" PLATFORM_HIDDEN void XB_CDECL MatrixMultiply(const float* in1, const float* in2, float* out);
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
void XB_REGS3 XB_CDECL Bounds_AddPointSimd(float* maxs, uint32_t unused_edx, float* mins, const float* point);
// 0x002d8a10 (this in ESI): the world tree registry (cm_worldTrees 0x0095f4fc): ResourceManager part, vtable
// 0x00832788, debug draw 0x0001d0d0 on the 32-entry list 0x00988784, +4..+0x19c zeroed, command "cg"
// (0x002afb90: debug flag +4 bit 0). Returns this.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CM_WorldTrees_Construct(uint8_t* self);
extern "C" void CM_WorldTrees_Construct_entry(void);

}  // namespace game
