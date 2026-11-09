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

// Runs ours and the original on the check table; one report line per difference, returns how many.
int32_t check_title_crt(std::string& report);

}  // namespace game
