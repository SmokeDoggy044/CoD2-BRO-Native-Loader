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
int32_t title_vsprintf(char* out, const char* format, va_list args);
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
inline void title_vector_construct(uint32_t array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_construct(reinterpret_cast<void*>(static_cast<uintptr_t>(array)), size, count, function);
}
inline void title_vector_construct(uint32_t* array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_construct(static_cast<void*>(array), size, count, function);
}
inline void title_vector_destruct(uint32_t array, uint32_t size, int32_t count, uint32_t function) {
    title_vector_destruct(reinterpret_cast<void*>(static_cast<uintptr_t>(array)), size, count, function);
}

// Runs ours and the original on the check table; one report line per difference, returns how many.
int32_t check_title_crt(std::string& report);

}  // namespace game
