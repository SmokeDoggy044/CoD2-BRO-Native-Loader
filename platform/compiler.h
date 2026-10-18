#pragma once

// Compiler-specific spellings live here and nowhere else, so game code stays portable.

// Xbox calling conventions. They only exist on 32-bit x86; elsewhere they expand to
// nothing, which is what the final (route C) build wants anyway.
#if defined(_MSC_VER) && defined(_M_IX86)
#define XB_STDCALL __stdcall
#define XB_FASTCALL __fastcall
#define XB_CDECL __cdecl
#define XB_THISCALL __thiscall
#define XB_REGS3
#define XB_REGS1
#define XB_REGS2
#elif defined(__i386__)
#define XB_STDCALL __attribute__((stdcall))
#define XB_FASTCALL __attribute__((fastcall))
#define XB_CDECL __attribute__((cdecl))
// `this` in ECX, the rest on the stack, callee pops; also on free functions.
#define XB_THISCALL __attribute__((thiscall))
// The title's whole-program-optimized (LTCG) functions may take their first three
// arguments in EAX, EDX, ECX, caller pops: GCC's regparm(3).
#define XB_REGS3 __attribute__((regparm(3)))
// The first one or two of them in EAX (, EDX); XB_REGS1 combines with XB_STDCALL.
#define XB_REGS1 __attribute__((regparm(1)))
#define XB_REGS2 __attribute__((regparm(2)))
#else
#define XB_STDCALL
#define XB_FASTCALL
#define XB_CDECL
#define XB_THISCALL
#define XB_REGS3
#define XB_REGS1
#define XB_REGS2
#endif

#if defined(__MINGW32__)
// MinGW builds use its C99 printf (__USE_MINGW_ANSI_STDIO), not msvcrt's.
#define PLATFORM_PRINTF(fmt_index, args_index) __attribute__((format(gnu_printf, fmt_index, args_index)))
#define PLATFORM_NOINLINE __attribute__((noinline))
#elif defined(__GNUC__) || defined(__clang__)
#define PLATFORM_PRINTF(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#define PLATFORM_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define PLATFORM_PRINTF(fmt_index, args_index)
#define PLATFORM_NOINLINE __declspec(noinline)
#else
#define PLATFORM_PRINTF(fmt_index, args_index)
#define PLATFORM_NOINLINE
#endif

#if defined(__GNUC__) || defined(__clang__)
// Puts a function into the named code section (loader/compare_linux.cpp keeps that
// section executable while everything else of ours is not).
#define PLATFORM_SECTION(name) __attribute__((section(name), noinline))
#endif

// The x87 FSIN/FCOS the title uses, rounded by the caller's precision control and
// stored as float like the title does. Other builds use the C library (route C).
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
inline float platform_x87_sin(float x) {
    float result;
    __asm__("fsin" : "=t"(result) : "0"(x));
    return result;
}
inline float platform_x87_cos(float x) {
    float result;
    __asm__("fcos" : "=t"(result) : "0"(x));
    return result;
}
#else
#include <cmath>
inline float platform_x87_sin(float x) { return std::sin(x); }
inline float platform_x87_cos(float x) { return std::cos(x); }
#endif
