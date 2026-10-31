#pragma once

// Common engine services (Q3's qcommon): memory.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Counts the allocation (0x009098c4) and hands it to the installed allocator
// (0x009098e8) when there is one; else flags 0x30000 = physical memory (0x20000:
// write-combined), others the CRT's aligned heap.
void* XB_CDECL Mem_AlignedAlloc(uint32_t size, uint32_t alignment, uint32_t flags);

// Case-blind name hash: per character (A-Z lowered) rotate left 5, xor the signed char.
// 0 for null or empty text.
uint32_t XB_REGS2 XB_CDECL Com_HashKeyNoCase(uint32_t unused, const char* text);
// The title's polynomial sine of angle + 3 pi / 2 into *sine and of angle into
// *cosine (the names as the callers use them; EDX unused).
void XB_REGS3 XB_CDECL FastSinCos(float* sine, uint32_t unused, float* cosine, float angle);
// 0x000442e0: copies at most max characters of src (null: none) and a NUL; *length =
// the characters copied.
void XB_CDECL Q_strncpyLen(char* dst, int32_t* length, int32_t max, const char* src);
// 0x000443e0: src[start .. min(start + count, size - 1)) up to a NUL into dst with a
// NUL, *length = the characters copied; nothing at all when start >= size.
void XB_CDECL Q_strncpySubLen(char* dst, int32_t* length, const char* src, int32_t start, int32_t count,
                              int32_t size);

// 0x0001f040 (src in EAX, dst in ESI, flag on the stack; returns dst): a fixed string (text,
// length byte at +0x7f) reduced to its base name (Path_BaseName 0x00044500 with flag) and
// copied into dst (FixedStr_Copy 0x00044370), dst's length byte set.
extern "C" PLATFORM_HIDDEN char* XB_CDECL FixedStr_BaseName(const char* src, char* dst, uint32_t flag);
extern "C" void FixedStr_BaseName_entry(void);

// 0x002a2af0 (right in EDI, up in ESI; angles, forward on the stack): Q3's AngleVectors with
// FastSinCos (x87 degree-to-radian products); the original keeps the pitch and roll sines in
// its two argument slots, so the C version gets their address.
extern "C" PLATFORM_HIDDEN void XB_CDECL AnglesToAxis(uint32_t* args, float* right, float* up);
extern "C" void AnglesToAxis_entry(void);

// 0x00295060 (a in EAX, out in EDX, b in ECX): a 4x3 matrix (rows of 3 floats, translation
// last) times a 4x4 one into a 4x4 with last column {0, 0, 0, 1}.
const float* XB_REGS3 XB_CDECL MatrixMultiply43To44(const float* a, float* out, const float* b);

// 0x000441c0: Q3's Q_stricmpn (A-Z lowered; the difference of the first differing pair).
int32_t XB_CDECL Q_stricmpn(const char* s1, const char* s2, int32_t n);
// 0x002b00f0 (s2 in EAX, s1 in ECX): -1 when either is null, else Q_stricmpn(s1, s2, 99999).
int32_t XB_REGS3 XB_CDECL Q_stricmp(const char* s2, uint32_t unused, const char* s1);
// 0x002b0050 (n in EAX, s1 in EDX, s2 in ECX): a-z raised; -1 on any difference.
int32_t XB_REGS3 XB_CDECL Q_stricmpn2(int32_t n, const char* s1, const char* s2);
// 0x002b00a0 (size in EAX, dest in EDX): Q3's Q_strcat (__break 0 when dest is full); EAX = dest + its length.
char* XB_REGS2 XB_CDECL Q_strcat(int32_t size, char* dest, const char* src);
// 0x002a9540 (text in EDI; returns it): lowers in place through the CRT's tolower.
extern "C" PLATFORM_HIDDEN char* XB_CDECL Q_strlwr(char* text);
extern "C" void Q_strlwr_entry(void);
// 0x002a8a40 (path in EDX): the part after the last '/' or '\\' copied into 0x0098b1a0.
char* XB_REGS2 XB_CDECL COM_SkipPath(uint32_t unused, const char* path);
// 0x00295e00 (name in EDX): the text after the last '.' not followed by a '/' or '\\', else "".
const char* XB_REGS2 XB_CDECL COM_GetExtension(uint32_t unused, const char* name);
// 0x002a98b0 (path in EAX; max size, extension on the stack): appends the extension unless the
// file name already has one (through a 0x80-byte copy and Com_sprintf "%s%s").
void XB_REGS1 XB_CDECL COM_DefaultExtension(char* path, int32_t max_size, const char* extension);
// 0x002a93d0: formats into 0x0092f148 (1 KB, __break 1 on overflow), then into the next free
// part of the 0x400-byte ring 0x00927f60 (offset 0x0098ad2c); returns that copy.
extern "C" PLATFORM_HIDDEN char* XB_CDECL va(const uint32_t* args);
extern "C" void va_entry(void);
// 0x002a9460 (dest in EDI, size in ESI; format and arguments on the stack): __vsnprintf, last byte
// NUL; returns __vsnprintf's result.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Com_sprintf(char* dest, int32_t size, const uint32_t* args);
extern "C" void Com_sprintf_entry(void);
// 0x002a9890 (bit in EDX): bit of the dword array on the stack, 0 or 1.
int32_t XB_REGS2 XB_CDECL Com_BitCheck(uint32_t unused, int32_t bit, const uint32_t* array);
// 0x002aaaa0: cmd_argv[arg], "" past cmd_argc (unsigned compare).
const char* XB_CDECL Cmd_Argv(uint32_t arg);
extern "C" void Cmd_Argv_entry(void);  // keeps ECX/EDX like the original
// 0x002aaac0.
int32_t XB_CDECL Cmd_Argc();
// 0x002aaa10 (first in EAX): the arguments from `first` on, space separated, in 0x00927b30.
char* XB_REGS1 XB_CDECL Cmd_ArgsFrom(int32_t first);
// 0x002b1e50 (buffer in EDI, size in ESI; returns buffer): Cmd_ArgsFrom(1) copied with a NUL.
extern "C" PLATFORM_HIDDEN char* XB_CDECL Cmd_ArgsBuffer(char* buffer, int32_t size);
extern "C" void Cmd_ArgsBuffer_entry(void);
// 0x002974a0 (result in XMM0): Q3's AngleDelta - fmod(a - b, 360) on the x87 (title __CIfmod),
// then into (-180, 180]; the original keeps the fmod result in its first argument slot.
extern "C" PLATFORM_HIDDEN float XB_CDECL Q_fmod(float a, float b);
extern "C" void Q_fmod_entry(void);

// 0x00293be0: count dwords of value at dest (eight at a time from the end, then 4/2/1); returns value.
uint32_t XB_CDECL Com_Memset32(uint32_t* dest, uint32_t value, uint32_t count);
// 0x002971a0: Q3's Com_Memset (Com_Memset32 for 8+ bytes; the pattern is (fill << 8 | fill) doubled, so
// fill's upper bits leak in). EAX = the pattern, or count when no tail bytes follow the dwords.
uint32_t XB_CDECL Com_Memset(uint8_t* dest, uint32_t fill, uint32_t count);
// 0x00296ec0 (vmCvar in EBX): copies a cvar's string/value/integer into the vmCvar when its
// modification count changed (__break 1 for a bad handle or a string over 128 bytes).
extern "C" PLATFORM_HIDDEN void XB_CDECL Cvar_Update(uint8_t* vm_cvar);
extern "C" void Cvar_Update_entry(void);
// 0x002a9cf0: pushes a parse-info record (0x11c bytes at 0x008d3ae0, depth 0x00927f54, current
// 0x008d4ca0; __break 0 at depth 15) named after the argument.
void XB_CDECL Com_BeginParseSession(const char* name);
// 0x002938a0 (command line in EAX): splits it into com_consoleLines at '+' and '\n' (at most 32).
void XB_REGS1 XB_CDECL Com_ParseCommandLine(char* command_line);

// msg_t: +4 data, +0xc cursize, +0x10 readcount.
// 0x002aa4f0 (msg in EDI): the string at the read position copied into 0x0098b4c8 and skipped;
// an empty string (position unchanged) when it is 0x100+ long or reaches cursize.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL MSG_ReadString(uint8_t* msg);
extern "C" void MSG_ReadString_entry(void);
// 0x002aa560 (msg in EAX; result in XMM0): four bytes, -1 when that passes cursize.
extern "C" PLATFORM_HIDDEN float XB_CDECL MSG_ReadFloat(uint8_t* msg);
extern "C" void MSG_ReadFloat_entry(void);
// 0x002aa5b0 (msg in EAX): four bytes, -1 when that passes cursize.
int32_t XB_REGS1 XB_CDECL MSG_ReadLong(uint8_t* msg);
// 0x002aa6e0 (msg in EAX): four bytes at cursize, no overflow check. EAX is kept: CL_WritePacket writes the next
// long without reloading it.
uint8_t* XB_REGS1 XB_CDECL MSG_WriteLong(uint8_t* msg, int32_t value);
// 0x002aa260 (a in EAX, b in EDX): netadr_t order - type, then the port (+0x12) and the IP (+4,
// 4 bytes) or IPX (+8, 10 bytes) bytes; loopback (2) and other types are equal.
int32_t XB_REGS2 XB_CDECL NET_CompareAdr(const uint8_t* a, const uint8_t* b);
// 0x002aa1c0 (sock in EAX, msg in EBX; from on the stack): the loopback's one queued packet
// (0x93c-byte records at 0x00928590) into msg, from = a loopback address; 0 when none.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL NET_GetLoopPacket(int32_t sock, uint8_t* msg, uint32_t* from);
extern "C" void NET_GetLoopPacket_entry(void);

// 0x00295d90 (name in ECX; path, depth out on the stack): Q3's FS_ReturnPath - the directory part of
// name and its depth; EAX = the length of that part.
int32_t XB_REGS3 XB_CDECL FS_ReturnPath(uint32_t unused_eax, uint32_t unused_edx, const char* name, char* path,
                                        int32_t* depth);
// 0x00295eb0 (path in ESI): 1 for paths with ".." or "::"; else 0 (the separator loop rewrites '\\' as
// itself).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL FS_CreatePath(char* path);
extern "C" void FS_CreatePath_entry(void);
// 0x00295f30 (EAX): a free fsh handle among 1-2 (EAX 0); with EAX set none is searched: __break 1, -1.
int32_t XB_REGS1 XB_CDECL FS_HandleForFile(uint32_t flag);
// 0x00297d10 (s1 in EAX, s2 in EDX): case- and separator-blind ('\\', ':' = '/'), 0 or -1.
int32_t XB_REGS2 XB_CDECL FS_FilenameCompare(const char* s1, const char* s2);
// 0x00295f90 (name in EAX; hash size on the stack): Q3's FS_HashFileName (tolower, stops at '.').
uint32_t XB_REGS1 XB_CDECL FS_HashFileName(const char* name, int32_t hash_size);

// 0x002aa8b0 (text in EAX; max tokens on the stack): splits text into cmd_argv/cmd_argc (0x0092df90 /
// 0x00934248, strings in cmd_tokenized 0x0092e9e8): blanks and // or /* */ comments skipped, "quoted"
// tokens; the max-th token takes the rest of the line (0: no limit); at most 0x200 tokens.
void XB_REGS1 XB_CDECL Cmd_TokenizeString(const char* text, uint32_t max_tokens);
// 0x002a9240 (info in EAX, key in EBX): Q3's Info_RemoveKey (__break 1 for info strings of 0x400+).
extern "C" PLATFORM_HIDDEN void XB_CDECL Info_RemoveKey(char* info, const char* key);
extern "C" void Info_RemoveKey_entry(void);

// 0x0029e6e0: Cvar_Set2(name, value, 1) (forced); returns its result.
uint32_t XB_CDECL Cvar_Set(const char* name, const char* value);
// 0x002ba5c0: Q3's Cmd_AddCommand - a 0xc-byte record {next, name copy, function} on cmd_functions
// (0x00929b04) unless the name is there already (no message).
void XB_CDECL Cmd_AddCommand(const char* name, uint32_t function);
// 0x002b1dc0: unlinks and frees the command named so (name copy, then the record).
void XB_CDECL Cmd_RemoveCommand(const char* name);

// 0x000939b0: uncounts the allocation (0x009098c4) and hands it to the installed free function
// (0x009098ec) when there is one; else XPhysicalFree for physical addresses (sign bit), the CRT's
// _aligned_free otherwise.
void XB_CDECL Mem_AlignedFree(void* pointer);
// 0x00094140: stdcall wrapper of Mem_AlignedFree (a callback table entry).
void XB_STDCALL Mem_AlignedFreeCall(void* pointer);
// 0x00042c20: Z_Malloc (0x00042cb0).
void* XB_CDECL Z_MallocDebug(uint32_t size);
// 0x00297a30: __break 0 for null, then one load fewer (fs_loadStack 0x00940a0c) and Z_Free.
void XB_CDECL FS_FreeFile(void* buffer);
// 0x0029fcc0: "reset <cvar>": Cvar_Set2(argv[1], null, 0) with exactly one argument.
void XB_CDECL Cvar_Reset_f();
// 0x0029fd60: every cheat cvar (flag 0x200) whose string differs from its reset string is reset (forced).
void XB_CDECL Cvar_SetCheatState();
// 0x002a1fb0: FS_ListFilteredFiles(path, extension, null, count).
uint32_t XB_CDECL FS_ListFiles(const char* path, const char* extension, int32_t* count);

// 0x002937a0 (msec in ECX): com_fixedtime (cvar at 0x0093fcc0) or msec times com_timescale (0x0093fce4),
// clamped to 1..100 (1 only while the timescale is set); 0 while the 0x0093fcd8 cvar is set.
int32_t XB_REGS3 XB_CDECL Com_ModifyMsec(uint32_t unused_eax, uint32_t unused_edx, int32_t msec);
// 0x00299800 (event in EDI): Q3's Com_PushEvent - 64-entry ring at 0x00940400 (0x18-byte events); on
// overflow the oldest event's data pointer is freed (warning flag 0x0098ad44, no message here).
extern "C" PLATFORM_HIDDEN void XB_CDECL Com_PushEvent(const uint32_t* event);
extern "C" void Com_PushEvent_entry(void);
// 0x002aac40: "wait [frames]": cmd_wait = atol(argv[1]) with an argument, else 1.
void XB_CDECL Cmd_Wait_f();

// 0x002aa2f0 (netadr_t by value, 0x14 bytes): "loopback", "%i.%i.%i.%i:%i" or the IPX form into
// 0x0098b388 (Com_sprintf, 0x40); the port through the _BigShort pointer (0x0092f138), sign-extended.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL NET_AdrToString(const uint8_t* adr);
extern "C" void NET_AdrToString_entry(void);
// 0x00299630 (flag bits in EDI): Info_SetValueForKey (0x002aff40; info on the stack, key ECX, value
// EAX) of every cvar with one of the bits into 0x0093f818; returns that buffer.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL Cvar_InfoString(uint32_t bits);
extern "C" void Cvar_InfoString_entry(void);
// 0x002995f0 (bits in EDI): the same with Info_SetValueForKey_Big (0x002afdb0; key EAX; info, value on
// the stack) into 0x0093fd78.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL Cvar_InfoString_Big(uint32_t bits);
extern "C" void Cvar_InfoString_Big_entry(void);
// 0x0029c4f0: `name "value"\n` per name (Cvar_FindVar; "" when unknown) through _snprintf; 0 when one
// does not fit, else 1.
int32_t XB_CDECL Cvar_WriteNamedValues(const char* const* names, int32_t count, char* buffer, int32_t size);
// 0x002b1b60 (text in EAX, msg in EBX): the string and its NUL at cursize (no overflow check); null or
// 0x100+ characters: only a NUL.
extern "C" PLATFORM_HIDDEN void XB_CDECL MSG_WriteString(const char* text, uint8_t* msg);
extern "C" void MSG_WriteString_entry(void);

// 0x00299710: the cvar named so (generateHashValue 0x00296f60 bucket of hashTable 0x009472f8, chained at
// +0x28, Q_stricmpn), else null.
uint8_t* XB_CDECL Cvar_FindVar(const char* name);
// 0x0029fdd0: Cvar_Set2(name, "%i" when the value is integral, else "%f", forced).
void XB_CDECL Cvar_SetValue(const char* name, float value);
// 0x0029fe40 (value in EAX): Cvar_Set2 on the vmCvar's cvar (forced), then Cvar_Update.
void XB_REGS1 XB_CDECL Cvar_SetByVmIndex(const char* value, uint8_t* vm_cvar);
// 0x002b8f50 (info in ECX, key in EDI): Q3's Info_ValueForKey - the value in one of two alternating
// 0x400-byte buffers (0x00927320, index 0x0098ad28), "" when absent; __break 1 for 0x400+ info strings.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL Info_ValueForKey(const char* info, const char* key);
extern "C" void Info_ValueForKey_entry(void);
// 0x002aff40 (value in EAX, key in ECX; info on the stack): value without '\\', ';', '"' (at most 0x3ff
// characters read), __break 1 for such a key, Info_RemoveKey, then "\\key\\value" appended unchecked.
void XB_REGS3 XB_CDECL Info_SetValueForKey(const char* value, uint32_t unused, const char* key, char* info);
// 0x002a9140 (info in ECX, key in EBX): Info_RemoveKey with its own message and buffers.
extern "C" PLATFORM_HIDDEN void XB_CDECL Info_RemoveKey_Big(char* info, const char* key);
extern "C" void Info_RemoveKey_Big_entry(void);

// 0x00297ef0 (game in EAX - null/"" = fs_gamedir -, base in ECX; qpath, ospath, quiet on the stack):
// "base/game/qpath" (no game: "base/qpath") with '/' turned into '\\' after the base; too long
// (0x80): "" when quiet, else __break 0 and built anyway.
void XB_REGS3 XB_CDECL FS_BuildOSPath(const char* game, uint32_t unused, const char* base, const char* qpath,
                                      char* ospath, uint32_t quiet);
// 0x00297950 (list in EAX): frees each pack node's +0x8c buffer and the node (next at +0x90).
void XB_REGS1 XB_CDECL FS_FreePackList(uint8_t* pack);
// 0x002979f0: frees each string of the null-terminated list, then the list.
void XB_CDECL FS_FreeFileList(char** list);
// 0x0029a810 (name in ESI; list, count on the stack): adds a Z_Malloc copy unless the list is full
// (0xfff) or has the name (Q_stricmpn); returns the new count.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL FS_AddFileToList(const char* name, char** list, int32_t count);
extern "C" void FS_AddFileToList_entry(void);
// 0x002b1eb0 (text in EAX, kept in 0x00927f50): tokenizes it and runs the matching command of the list at
// 0x009284e4 (moved to its front).
void XB_REGS1 XB_CDECL Cmd_ExecuteRegisteredCommand(const char* text);

// 0x002970c0: every non-empty com_consoleLines entry appended to the command buffer (cmd_text 0x00935328:
// data, maxsize, cursize) with a '\n' when they fit; 1 when one of them is not a "set" command.
int32_t XB_CDECL Com_AddStartupCommands();
// 0x00299670: frees every cvar's strings and name through the hash chains and clears the cvar globals.
void XB_CDECL Cvar_Shutdown();
// 0x002b0140: the little-endian byte-swap function pointers (_BigShort ... _LittleFloat); returns 1.
uint32_t XB_CDECL Swap_Init();

// 0x002afdb0 (key in EAX; info, value on the stack): Info_SetValueForKey for big info strings (its own
// oversize message, Info_RemoveKey_Big; __break 1 when the result passes 0x400, appended anyway).
void XB_REGS1 XB_CDECL Info_SetValueForKey_Big(const char* key, char* info, const char* value);
// 0x002a23b0: the cvar console commands.
void XB_CDECL Cvar_Init();
// 0x002d1460: cmdlist, exec, vstr, echo, wait.
void XB_CDECL Cmd_Init();
// 0x002ba680: Cmd_AddCommand(name, 0x002aaae0), then the same kind of record on the server command list
// (0x009284e4) unless it has the name.
void XB_CDECL Cmd_AddServerCommand(const char* name, uint32_t function);

// 0x00296f60 (name in EAX): the cvar hash - sum of tolower(c) * (i + 119), low byte; __break 1 for null.
int32_t XB_REGS1 XB_CDECL generateHashValue(const char* name);
// 0x00299c80 (text in EBX): a zeroed Z_Malloc copy.
extern "C" PLATFORM_HIDDEN char* XB_CDECL CopyString(const char* text);
extern "C" void CopyString_entry(void);
// 0x002a9b30 (data in EAX, has-new-lines out in ESI): skips characters <= ' ' (signed), counting '\n' in
// the current parse info's +0x80; null at the end of the text.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL SkipWhitespace(const char* data, int32_t* has_new_lines);
extern "C" void SkipWhitespace_entry(void);
// 0x002aab40 (text in EAX): text and '\n' inserted at the front of the command buffer when they fit.
void XB_REGS1 XB_CDECL Cbuf_InsertText(const char* text);
// 0x002aabb0 (text in EAX): appended to the command buffer when it fits.
void XB_REGS1 XB_CDECL Cbuf_AddText(const char* text);

// 0x0029fce0 / 0x0029fd20: "seta"/"sets name value": Cvar_Set_f (0x0029e3d0) with exactly two arguments,
// then the cvar's flags get 1 (archive) / 4.
void XB_CDECL Cvar_SetA_f();
void XB_CDECL Cvar_SetS_f();
// 0x0029fc30: user-created cvars (flag 0x80) unlinked with their strings freed (the record stays in the
// hash chain); the rest reset (forced) unless flagged 0x450.
void XB_CDECL Cvar_Restart_f();
// 0x0029e930: Q3's Com_StartupVariable - "set" console lines (all, or the one naming `match`) applied and
// flagged user-created.
void XB_CDECL Com_StartupVariable(const char* match);
// 0x002c0d30: "vstr name": the cvar's string and a '\n' inserted into the command buffer.
void XB_CDECL Cmd_Vstr_f();

// Parse info (current record in 0x008d4ca0): +0 token[0x80], +0x80 lines, +0x88 space-delimited tokens,
// +0x8c comma-separated tokens, +0x90 negative numbers, +0x94/+0x98 backup line/text.
// 0x002a9a60 (data_p in EDI, allow line breaks in EDX): the comma-separated token - up to ',' or '\n'
// (skipped, a '\n' only without line breaks allowed), '\r' dropped, "quoted" parts with "" for '"'.
// Returns the token; *data_p = null at the end of the text. Unnamed in Ghidra.
extern "C" PLATFORM_HIDDEN char* XB_CDECL Com_ParseCommaToken(const char** data_p, uint32_t allow_line_breaks);
extern "C" void Com_ParseCommaToken_entry(void);
// 0x002b0250: Q3's COM_ParseExt with the punctuation table (0x008d3aa0) and the parse-info modes above.
char* XB_CDECL COM_ParseExt(const char** data_p, uint32_t allow_line_breaks);

// 0x002b1570 (from in EAX, msg in ESI, to in EDI): the byte-aligned usercmd delta - a time flag byte
// (then a byte delta or MSG_ReadLong), a change flag byte (0: everything copied from `from`), three
// 16-bit angles, six bytes (+0x14..+0x16, +4..+6; 0xff past cursize) and five MSG_ReadFloat values.
// The flag bytes are read without the cursize check.
extern "C" PLATFORM_HIDDEN void XB_CDECL MSG_ReadDeltaUsercmd(const uint8_t* from, uint8_t* msg, uint8_t* to);
extern "C" void MSG_ReadDeltaUsercmd_entry(void);

// 0x002b1780 (from in EAX, to in EDX, msg in ECX): MSG_ReadDeltaUsercmd's writer - time flag + byte delta
// (under 0x100) or the full time, then 0 when nothing else changed (floats by ==), else 1 and every field
// (angles as their low 16 bits). No overflow checks.
void XB_REGS3 XB_CDECL MSG_WriteDeltaUsercmd(const uint8_t* from, const uint8_t* to, uint8_t* msg);

// cvar_t: +0 name, +4 string, +8 resetString, +0xc latchedString, +0x10 flags, +0x14 modified,
// +0x18 modificationCount, +0x1c value, +0x20 integer, +0x24 next (sorted list), +0x28 hashNext.
// 0x0029c030: Q3's Cvar_Set2 - the value's '\\' turned into '/' in place (its length kept in the
// name's argument slot), name check (__break 0), unknown cvars made through Cvar_Get (user-created
// unless forced); ROM (0x40), INIT (0x10), cheat (0x200, cheats off) and latched (0x20) rules unforced.
uint8_t* XB_CDECL Cvar_Set2(const char* name, char* value, uint32_t force);
// 0x0029c2d0: Q3's Cvar_Get - new cvars in cvar_indexes (at most 0x262), kept sorted by name
// (stricmp) and hashed; existing ones take the flags, a new reset string for user-created/0x1000 ones,
// apply a latched value, and are forced to the value for ROM (0x40) or cheats off.
uint8_t* XB_CDECL Cvar_Get(const char* name, const char* value, uint32_t flags);

// Handle pools (0x000cb440..): items of `stride` (+0x1c) bytes at +0, capacity +0x20, index mask +0x24, list
// head node +0x10; each item starts with its next node and holds its handle at +8.
// 0x000cb5e0: the first handle (head node +8).
int32_t XB_CDECL HandlePool_First(const uint8_t* pool);
// 0x000cb600: the handle after `handle` (its item's next node +8), -1 for a stale or invalid handle.
int32_t XB_CDECL HandlePool_Next(const uint8_t* pool, int32_t handle);
// 0x000cb440: the item index of a live handle (handle & mask), -1 for a stale or invalid one.
int32_t XB_CDECL HandlePool_Resolve(const uint8_t* pool, int32_t handle);

// 0x00093c00 (thiscall): a map's constructor - its first two words cleared; returns it. Run by
// the static initializers of the shader caches (0x00902ac0, 0x00902ad4) and the NAL class
// maps (0x00903fe0, 0x00903ff4), before the title's main.
uint8_t* XB_THISCALL HashStrMap_Ctor(uint8_t* map);


}  // namespace game
