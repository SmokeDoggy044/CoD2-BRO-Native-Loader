#pragma once

// Our own versions of the title's C runtime (VC7.1, statically linked into default.xbe), for the own engine host
// (0.3: the host maps no original .text). Each behaves like the title's, not like the host's CRT: VC7.1's printf
// writes 3-digit exponents, %p as 8 upper-case hex digits, 1.#INF/1.#QNAN; its atol wraps on overflow.
// `--check-crt` (loader) runs each against the original on a table of inputs and logs every difference.

#include <cstdarg>
#include <cstdint>
#include <string>

#include "compiler.h"

namespace game {

// 0x001eea95 _vsprintf / 0x001ee901 _sprintf. Wide arguments (%S, %ls, %C, %lc) are 16-bit, as on the Xbox.
int32_t XB_CDECL title_vsprintf(char* out, const char* format, va_list args);
int32_t XB_CDECL title_sprintf(char* out, const char* format, ...);
// 0x001eeb81 _atol
int32_t XB_CDECL title_atol(const char* text);
// 0x001eed80 _strncpy
char* XB_CDECL title_strncpy(char* dest, const char* source, uint32_t count);

// 0x001eed48 tolower (the "C" locale: only 'A'..'Z' change; the title never sets another locale)
int32_t XB_CDECL title_tolower(int32_t c);
// 0x001ef056 atof: whitespace, then VC's _fltin grammar ([sign] digits [. digits] [e|E|d|D [sign] digits]; no hex,
// inf or nan). Returns the double (in ST0 on the Xbox; callers that store a float round it there).
double XB_CDECL title_atof(const char* text);
// atof as a caller that declared it float sees it (the double rounded on the store).
inline float title_atof_float(const char* text) {
    return static_cast<float>(title_atof(text));
}
// 0x001f07e1 sscanf, the conversions the title's callers use: %f/%e/%g (float*), %lf (double*), %d (int32_t*), %%,
// whitespace and literal characters. Returns the conversions stored, or -1 if the input ended before the first.
int32_t XB_CDECL title_sscanf(const char* text, const char* format, ...);
// 0x001ef6a0 memset (the level import table's memset2)
void* XB_CDECL title_memset(void* dest, int32_t value, uint32_t count);
// 0x00011240 `eh vector constructor iterator' / 0x00011280 `eh vector destructor iterator' (stdcall): calls the
// thiscall constructor on each element in order / the destructor from the last element back. `function` is a code
// address (original or our C's).
void XB_STDCALL title_vector_construct(void* array, uint32_t size, int32_t count, uint32_t function);
void XB_STDCALL title_vector_destruct(void* array, uint32_t size, int32_t count, uint32_t function);
// The two above under one name each, for the hook table (0x00011240 / 0x00011280).
void XB_STDCALL title_vector_constructor_iterator(void* array, uint32_t size, int32_t count, uint32_t function);
void XB_STDCALL title_vector_destructor_iterator(void* array, uint32_t size, int32_t count, uint32_t function);
inline void title_vector_construct(uint32_t array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_construct(reinterpret_cast<void*>(static_cast<uintptr_t>(array)), size, count, function);
}
inline void title_vector_construct(uint32_t* array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_construct(static_cast<void*>(array), size, count, function);
}
inline void title_vector_destruct(uint32_t array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_destruct(reinterpret_cast<void*>(static_cast<uintptr_t>(array)), size, count, function);
}

// 0x001efce2 _vsnprintf: at most `count` characters, the terminator only if it fits; -1 if the text did not fit.
int32_t XB_CDECL title_vsnprintf(char* out, uint32_t count, const char* format, va_list args);

// The CRT's per-thread data (VC7.1 _tiddata, 0x88 bytes; the fields our CRT uses).
struct TitleThreadData {
    uint32_t thread_id;    // +0x00
    uint32_t handle;       // +0x04 (-1)
    int32_t error;         // +0x08 errno
    uint32_t dos_error;    // +0x0c _doserrno
    uint32_t fpds;         // +0x10
    uint32_t rand_seed;    // +0x14 rand's state (1 until srand)
    uint32_t token;        // +0x18 strtok's position
    uint32_t rest[0x0e];   // +0x1c
    uint32_t mbcinfo;      // +0x54 the multibyte info (0x008d1ad0)
    uint32_t tail[0x0c];   // +0x58..0x88 (C++ EH: current exception, context, ...)
};
static_assert(sizeof(TitleThreadData) == 0x88);
// 0x001f3327 _getptd. Our C's CRT reaches it through the original address on purpose (tools/own_calls.py
// KEEP_ADDRESS): in loader runs that is the title's own (one rand seed / errno / strtok position shared with the
// title's CRT); with --own-main (and in the host) the address jumps here and every user shares ours.
TitleThreadData* XB_CDECL title_getptd();
TitleThreadData* title_thread_data();  // the above through 0x001f3327
// 0x001efcb3 srand / 0x001efcc0 rand (seed * 214013 + 2531011, bits 16..30) / 0x001f0ac5 _errno
void XB_CDECL title_srand(uint32_t seed);
int32_t XB_CDECL title_rand();
int32_t* XB_CDECL title_errno();

// Strings ("C" locale; the title never sets another). Comparisons return -1, 0 or 1 like VC's asm versions.
int32_t XB_CDECL title_strncmp(const char* a, const char* b, uint32_t count);  // 0x001eeaf0
char* XB_CDECL title_strtok(char* text, const char* delimiters);              // 0x001eeea4
uint32_t XB_CDECL title_strcspn(const char* text, const char* reject);        // 0x001ef010
char* XB_CDECL title_strchr(const char* text, int32_t c);                     // 0x001ef5e0
char* XB_CDECL title_strstr(const char* text, const char* part);              // 0x001efb10
int32_t XB_CDECL title_stricmp(const char* a, const char* b);                 // 0x001f1443 (__ascii_stricmp)
char* XB_CDECL title_strlwr(char* text);                                      // 0x001fa3b2
char* XB_CDECL title_strupr(char* text);                                      // 0x001fa3d5
int32_t XB_CDECL title_strtol(const char* text, char** end, int32_t base);    // 0x001f0ecf
// ctype through the title's _pctype table (0x008d1650; callers pass sign-extended chars, which index before it):
// the flag bits, not 0/1.
int32_t XB_CDECL title_isalpha(int32_t c);  // 0x001eef49 & 0x103
int32_t XB_CDECL title_isdigit(int32_t c);  // 0x001eef77 & 0x4
int32_t XB_CDECL title_isspace(int32_t c);  // 0x001eefa0 & 0x8
int32_t XB_CDECL title_isalnum(int32_t c);  // 0x001eefc9 & 0x107

// 0x001ef880 qsort (VC7.1: median of three, insertion below 9 elements, equal runs skipped - the order of equal keys
// is VC's) / 0x001f123f bsearch. `compare` is a cdecl code address (original or our C's).
void XB_CDECL title_qsort(void* base, uint32_t count, uint32_t width, uint32_t compare);
void* XB_CDECL title_bsearch(const void* key, const void* base, uint32_t count, uint32_t width, uint32_t compare);

// 0x001eed36 atexit: our own list (the title's onexit table and its walk at exit stay with the title's CRT; the game
// never exits through it). Our C calls it through the original address (KEEP_ADDRESS), so loader runs keep the
// title's table.
int32_t XB_CDECL title_atexit(uint32_t function);
// 0x001f12cb clock: milliseconds since the CRT start time (0x00926a58, set by the title's ___inittime).
int32_t XB_CDECL title_crt_clock();

// 64-bit helpers (stdcall, result in EDX:EAX): 0x001f1170 _allmul, 0x001f1580 _alldiv, 0x001f11b0 _aulldiv.
int64_t XB_STDCALL title_allmul(int64_t a, int64_t b);
int64_t XB_STDCALL title_alldiv(int64_t a, int64_t b);
uint64_t XB_STDCALL title_aulldiv(uint64_t a, uint64_t b);
// Register conventions (asm entries): 0x001f2590 _allshr (EDX:EAX >> CL, arithmetic) and 0x001f0bf4 _ftol (ST0 ->
// EDX:EAX, truncated: VC7.1's version rounds in the current mode, then corrects by the sign of the difference).
extern "C" void title_allshr_entry(void);
extern "C" void title_ftol_entry(void);

// 0x001f2399 / 0x001f289d `eh vector destructor/constructor iterator' (the SEH-framed ones; their unwind paths only
// run when a constructor or destructor throws, which none does).
void XB_STDCALL title_eh_vector_destruct(void* array, uint32_t size, int32_t count, uint32_t function);
void XB_STDCALL title_eh_vector_construct(void* array, uint32_t size, int32_t count, uint32_t function,
                                          uint32_t destructor);

// 0x001f1486 _strnicmp (__ascii_strnicmp: 'A'..'Z' folded, -1/0/1) / 0x001eebff _snprintf.
int32_t XB_CDECL title_strnicmp(const char* a, const char* b, uint32_t count);
int32_t XB_CDECL title_snprintf(char* out, uint32_t count, const char* format, ...);
// 0x001f0a40 _controlfp / 0x001f0a0e _control87: the x87 control word through VC's abstract bits (_EM_*, _RC_*,
// _PC_*, _IC_*); _controlfp never changes the denormal mask. Returns the new abstract word.
uint32_t XB_CDECL title_control87(uint32_t value, uint32_t mask);
uint32_t XB_CDECL title_controlfp(uint32_t value, uint32_t mask);
// 0x001f0fb0 _chkstk (EAX = frame size): touches each 4 KB page downwards and moves ESP.
extern "C" void title_chkstk_entry(void);

}  // namespace game

// x87 intrinsics (argument and result in ST(0); EAX, ECX, EDX clobbered), as the title's VC7.1 CRT computes them:
// the operation runs with the control word's precision kept, rounding to nearest and every exception masked
// (sqrt: (cw & 0x300) | 0x7f) or with 0x027f (sin/cos/tan), the caller's control word restored after. When the
// caller's word is not 0x027f, masks precision exceptions and the inexact flag is set, the result goes through a
// double (the CRT's error path stores and reloads it). |x| >= 2^63 is reduced with fprem1 by 2 pi for sin/cos/tan.
// Domain errors (sqrt of a negative, an infinite angle, any NaN) give the indefinite / a quiet NaN, also through a
// double, and errno EDOM. Not ported: unmasked exceptions (the title masks all), _set_fast_math's flag 0x00926a4c
// (read; 0 in every run seen).
// 0x0033acf4 __CIsqrt / 0x0033ae60 __CIsin / 0x0033adb0 __CIcos / 0x0033af10 __CItan.
extern "C" void title_CIsqrt(void);
extern "C" void title_CIsin(void);
extern "C" void title_CIcos(void);
extern "C" void title_CItan(void);
// 0x001f0ff4 __CIacos.
extern "C" void title_CIacos(void);
// 0x0033acda __CIfmod (x in ST(1), y in ST(0); both popped, the result pushed): see title_crt.cpp for its classes.
extern "C" void title_CIfmod(void);
// 0x001f1a6c __CIpow (x in ST(1), y in ST(0)): see title_crt.cpp; bit-exact with its overflow / underflow / domain
// paths, x87 status flags and errno.
extern "C" void title_CIpow(void);

namespace game {

// Runs ours and the original on the check table; one report line per difference, returns how many.
int32_t check_title_crt(std::string& report);

}  // namespace game
