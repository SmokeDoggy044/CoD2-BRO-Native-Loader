#pragma once

#include <cstdint>

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

// Entry points for title functions whose arguments sit where C cannot name them (ESI,
// EDI, EBX, an XMM register): PLATFORM_ASM_ENTRY(name, body) defines `name` from AT&T
// assembly that moves them to the stack and calls the C version - an extern "C"
// PLATFORM_HIDDEN function (no PLT in a shared object), called with
// PLATFORM_ASM_CALL(c_name). Declare the entry as extern "C" void name(void).
// Only 32-bit x86 builds need them (route B runs the title's callers).
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#if defined(_WIN32)
#define PLATFORM_HIDDEN
#define PLATFORM_ASM_SYMBOL(name) "_" #name
#define PLATFORM_ASM_ENTRY(name, body)                                                        \
    __asm__(".text\n.p2align 4\n.globl " PLATFORM_ASM_SYMBOL(name) "\n" PLATFORM_ASM_SYMBOL( \
        name) ":\n" body "\n");
#else
#define PLATFORM_HIDDEN __attribute__((visibility("hidden")))
#define PLATFORM_ASM_SYMBOL(name) #name
#define PLATFORM_ASM_ENTRY(name, body)                                                   \
    __asm__(".pushsection .text\n.p2align 4\n.globl " #name "\n.type " #name ",@function\n" #name \
            ":\n" body "\n.size " #name ",.-" #name "\n.popsection\n");
#endif
#define PLATFORM_ASM_CALL(name) "call " PLATFORM_ASM_SYMBOL(name) "\n"
#endif

// Makes the compiler compute `value` although nothing reads it: the title's packed
// SSE code computes unused lanes too, and their exception flags (MXCSR) are part of
// what the comparison harness checks.
#if defined(__GNUC__) || defined(__clang__)
template <typename T>
inline void platform_keep(const T& value) {
    __asm__ volatile("" : : "m"(value));
}
#else
template <typename T>
inline void platform_keep(const T&) {}
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
// FPTAN (it also pushes 1.0, popped here).
inline float platform_x87_tan(float x) {
    float result;
    __asm__("fptan\n\tfstp %%st(0)" : "=t"(result) : "0"(x));
    return result;
}
// t - sqrt(a * b) >= limit, all on the x87 (FMUL/FSQRT/FSUBR/FCOMIP); false when
// unordered.
inline bool platform_x87_sub_sqrt_ge(float t, float a, float b, float limit) {
    bool result;
    __asm__("flds %1\n\tfmuls %2\n\tfsqrt\n\tfsubrs %3\n\tflds %4\n\tfxch %%st(1)\n\t"
            "fcomip %%st(1), %%st\n\tfstp %%st(0)\n\tsetae %0"
            : "=q"(result)
            : "m"(a), "m"(b), "m"(t), "m"(limit)
            : "cc", "st");
    return result;
}
// 1 / sqrt(v * v + 1) on the x87 (FMUL/FADD/FSQRT/FDIVR), stored as float.
inline float platform_x87_rcp_sqrt_sq_plus_one(float v) {
    static const float one = 1.0f;
    float result;
    __asm__("flds %1\n\tfmuls %1\n\tfadds %2\n\tfsqrt\n\tfdivrs %2" : "=t"(result) : "m"(v), "m"(one));
    return result;
}
// one / sqrt(a * a + b * b) on the x87 (FMUL, FMUL, FADDP, FSQRT, FDIVR), as float.
inline float platform_x87_rcp_hypot(float a, float b, float one) {
    float result;
    __asm__("flds %1\n\tfmuls %1\n\tflds %2\n\tfmuls %2\n\tfaddp\n\tfsqrt\n\tfdivrs %3"
            : "=t"(result)
            : "m"(a), "m"(b), "m"(one));
    return result;
}
// sqrt(|(one - x) * half|) on the x87 (FSUB, FMUL, FABS, FSQRT), as float.
inline float platform_x87_sqrt_abs_sub_mul(float one, float x, float half) {
    float result;
    __asm__("flds %1\n\tfsubs %2\n\tfmuls %3\n\tfabs\n\tfsqrt" : "=t"(result) : "m"(one), "m"(x), "m"(half));
    return result;
}
// The cosine and sine of a * b * c, all on the x87 (FMUL twice, FCOS of a copy, FSIN),
// each stored as float: no MXCSR flags for the products.
inline void platform_x87_sin_cos_of_product(float a, float b, float c, float* sine, float* cosine) {
    __asm__("flds %2\n\tfmuls %3\n\tfmuls %4\n\tfld %%st(0)\n\tfcos\n\tfstps %1\n\tfsin\n\tfstps %0"
            : "=m"(*sine), "=m"(*cosine)
            : "m"(a), "m"(b), "m"(c)
            : "st", "st(1)");
}
// sqrt on the x87 (FLD/FSQRT/FSTP, rounded by the caller's precision control), stored
// as float: no MXCSR flags.
inline float platform_x87_sqrt(float x) {
    float result;
    __asm__("flds %1\n\tfsqrt\n\tfstps %0" : "=m"(result) : "m"(x) : "st");
    return result;
}
// a / b * c on the x87 (FLD/FDIV/FMUL, rounded by the caller's precision control),
// stored as float: no MXCSR flags.
inline float platform_x87_div_mul(float a, float b, float c) {
    float result;
    __asm__("flds %1\n\tfdivs %2\n\tfmuls %3\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c) : "st");
    return result;
}
// a + b on the x87 (FLD/FADD/FSTP, the caller's precision and rounding control),
// stored as float: no MXCSR flags.
inline float platform_x87_add(float a, float b) {
    float result;
    __asm__("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}
// a * b / c + d on the x87 (FLD/FMUL/FDIV/FADD, rounded by the caller's precision
// control), stored as float: no MXCSR flags.
inline float platform_x87_mul_div_add(float a, float b, float c, float d) {
    float result;
    __asm__("flds %1\n\tfmuls %2\n\tfdivs %3\n\tfadds %4\n\tfstps %0"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(c), "m"(d)
            : "st");
    return result;
}
// FILD + FMUL + FSTP: an integer times a float on the x87 (the caller's precision and
// rounding control), stored as float. No MXCSR flags.
inline float platform_x87_int_mul(std::int32_t value, float factor) {
    float result;
    __asm__("fildl %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(value), "m"(factor) : "st");
    return result;
}
// MSVC's _ftol of value * factor (+ addend with FIADD) computed on the x87: FILD, FMUL,
// [FIADD,] then a chopping FISTP through 64 bits, low half.
inline std::int32_t platform_x87_mul_add_ftol(std::int32_t value, float factor, std::int32_t addend, bool add) {
    std::int64_t result;
    std::uint16_t control, chop;
    __asm__("fnstcw %0" : "=m"(control));
    chop = static_cast<std::uint16_t>(control | 0x0c00);
    if (add)
        __asm__("fildl %1\n\tfmuls %2\n\tfiaddl %3\n\tfldcw %4\n\tfistpll %0\n\tfldcw %5"
                : "=m"(result)
                : "m"(value), "m"(factor), "m"(addend), "m"(chop), "m"(control)
                : "st");
    else
        __asm__("fildl %1\n\tfmuls %2\n\tfldcw %3\n\tfistpll %0\n\tfldcw %4"
                : "=m"(result)
                : "m"(value), "m"(factor), "m"(chop), "m"(control)
                : "st");
    return static_cast<std::int32_t>(result);
}
// a * b + c on the x87 (FLD/FMUL/FADD, the caller's precision and rounding control),
// stored as float: no MXCSR flags.
inline float platform_x87_mul_add(float a, float b, float c) {
    float result;
    __asm__("flds %1\n\tfmuls %2\n\tfadds %3\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c) : "st");
    return result;
}
// a * b * c + d on the x87 (FLD/FMUL/FMUL/FADD, the caller's precision and rounding
// control), stored as float: no MXCSR flags.
inline float platform_x87_mul_mul_add(float a, float b, float c, float d) {
    float result;
    __asm__("flds %1\n\tfmuls %2\n\tfmuls %3\n\tfadds %4\n\tfstps %0"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(c), "m"(d)
            : "st");
    return result;
}
// MSVC's unsigned to float (FILD, FADD 2^32 when negative) times b times c, all on the
// x87 without storing in between, stored as float: no MXCSR flags.
inline float platform_x87_unsigned_mul_mul(std::uint32_t value, float b, float c) {
    static const float two32 = 4294967296.0f;
    float result;
    if (static_cast<std::int32_t>(value) < 0)
        __asm__("fildl %1\n\tfadds %2\n\tfmuls %3\n\tfmuls %4\n\tfstps %0"
                : "=m"(result)
                : "m"(value), "m"(two32), "m"(b), "m"(c)
                : "st");
    else
        __asm__("fildl %1\n\tfmuls %2\n\tfmuls %3\n\tfstps %0" : "=m"(result) : "m"(value), "m"(b), "m"(c) : "st");
    return result;
}
// MSVC's unsigned to float (FILD, FADD 2^32 when negative) times b plus c on the x87,
// stored as float: no MXCSR flags.
inline float platform_x87_unsigned_mul_add(std::uint32_t value, float b, float c) {
    static const float two32 = 4294967296.0f;
    float result;
    if (static_cast<std::int32_t>(value) < 0)
        __asm__("fildl %1\n\tfadds %2\n\tfmuls %3\n\tfadds %4\n\tfstps %0"
                : "=m"(result)
                : "m"(value), "m"(two32), "m"(b), "m"(c)
                : "st");
    else
        __asm__("fildl %1\n\tfmuls %2\n\tfadds %3\n\tfstps %0" : "=m"(result) : "m"(value), "m"(b), "m"(c) : "st");
    return result;
}
// MSVC's _ftol of ((a + b) * c * d + e) computed on the x87: FLD, FADD, FMUL, FMUL,
// FADD, then a chopping FISTP through 64 bits, low half.
inline std::int32_t platform_x87_add_mul_mul_add_ftol(float a, float b, float c, float d, float e) {
    std::int64_t result;
    std::uint16_t control, chop;
    __asm__("fnstcw %0" : "=m"(control));
    chop = static_cast<std::uint16_t>(control | 0x0c00);
    __asm__("flds %1\n\tfadds %2\n\tfmuls %3\n\tfmuls %4\n\tfadds %5\n\tfldcw %6\n\tfistpll %0\n\tfldcw %7"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(c), "m"(d), "m"(e), "m"(chop), "m"(control)
            : "st");
    return static_cast<std::int32_t>(result);
}
// FLD/FRNDINT/FISTP: a float rounded to an integer by the caller's rounding control.
inline std::int32_t platform_x87_round_to_int(float x) {
    std::int32_t result;
    __asm__("flds %1\n\tfrndint\n\tfistpl %0" : "=m"(result) : "m"(x) : "st");
    return result;
}
// MSVC's unsigned to float on the x87: FILD of the value as signed, FADD 2^32 when
// negative (rounded by the caller's precision control), FSTP as float. No MXCSR flags.
inline float platform_x87_unsigned_to_float(std::uint32_t value) {
    static const float two32 = 4294967296.0f;
    float result;
    if (static_cast<std::int32_t>(value) < 0)
        __asm__("fildl %1\n\tfadds %2\n\tfstps %0" : "=m"(result) : "m"(value), "m"(two32) : "st");
    else
        __asm__("fildl %1\n\tfstps %0" : "=m"(result) : "m"(value) : "st");
    return result;
}
// numerator / (float)value with the unsigned conversion above, the division on the x87
// too (FILD, FADD 2^32 when negative, FDIVR, FSTP). No MXCSR flags.
inline float platform_x87_div_by_unsigned(float numerator, std::uint32_t value) {
    static const float two32 = 4294967296.0f;
    float result;
    if (static_cast<std::int32_t>(value) < 0)
        __asm__("fildl %1\n\tfadds %2\n\tfdivrs %3\n\tfstps %0"
                : "=m"(result)
                : "m"(value), "m"(two32), "m"(numerator)
                : "st");
    else
        __asm__("fildl %1\n\tfdivrs %2\n\tfstps %0" : "=m"(result) : "m"(value), "m"(numerator) : "st");
    return result;
}
#else
#include <cmath>
inline float platform_x87_unsigned_to_float(std::uint32_t value) { return static_cast<float>(value); }
inline float platform_x87_div_by_unsigned(float numerator, std::uint32_t value) {
    return numerator / static_cast<float>(value);
}
inline float platform_x87_sin(float x) { return std::sin(x); }
inline float platform_x87_cos(float x) { return std::cos(x); }
inline float platform_x87_tan(float x) { return std::tan(x); }
inline bool platform_x87_sub_sqrt_ge(float t, float a, float b, float limit) {
    return t - std::sqrt(a * b) >= limit;
}
inline float platform_x87_rcp_sqrt_sq_plus_one(float v) { return 1.0f / std::sqrt(v * v + 1.0f); }
inline float platform_x87_rcp_hypot(float a, float b, float one) { return one / std::sqrt(a * a + b * b); }
inline float platform_x87_sqrt_abs_sub_mul(float one, float x, float half) {
    return std::sqrt(std::fabs((one - x) * half));
}
inline float platform_x87_div_mul(float a, float b, float c) { return a / b * c; }
inline float platform_x87_mul_div_add(float a, float b, float c, float d) { return a * b / c + d; }
inline float platform_x87_add(float a, float b) { return a + b; }
inline float platform_x87_mul_add(float a, float b, float c) { return a * b + c; }
inline float platform_x87_mul_mul_add(float a, float b, float c, float d) { return a * b * c + d; }
inline float platform_x87_unsigned_mul_mul(std::uint32_t value, float b, float c) {
    return static_cast<float>(value) * b * c;
}
inline float platform_x87_unsigned_mul_add(std::uint32_t value, float b, float c) {
    return static_cast<float>(value) * b + c;
}
inline std::int32_t platform_x87_add_mul_mul_add_ftol(float a, float b, float c, float d, float e) {
    return static_cast<std::int32_t>(static_cast<std::int64_t>((a + b) * c * d + e));
}
inline std::int32_t platform_x87_round_to_int(float x) { return static_cast<std::int32_t>(std::nearbyint(x)); }
inline float platform_x87_int_mul(std::int32_t value, float factor) { return static_cast<float>(value) * factor; }
inline std::int32_t platform_x87_mul_add_ftol(std::int32_t value, float factor, std::int32_t addend, bool add) {
    const float product = static_cast<float>(value) * factor;
    return static_cast<std::int32_t>(static_cast<std::int64_t>(add ? product + static_cast<float>(addend) : product));
}
inline float platform_x87_sqrt(float x) { return std::sqrt(x); }
inline void platform_x87_sin_cos_of_product(float a, float b, float c, float* sine, float* cosine) {
    *sine = std::sin(a * b * c);
    *cosine = std::cos(a * b * c);
}
#endif

// MSVC's _ftol: truncates through a 64-bit integer and keeps the low half, so NaN and
// values outside int64 give 0 (the low half of 0x8000000000000000). GCC on i386
// converts float to int64 with FISTP under a chopping control word the same way.
inline std::int32_t platform_ftol(float x) { return static_cast<std::int32_t>(static_cast<std::int64_t>(x)); }

// SSE RSQRTSS, the title's reciprocal square root estimate (its precision is the
// CPU's: it differs between vendors, so only results on the same CPU compare).
#if defined(__SSE__) || defined(_M_IX86_FP) || defined(_M_X64)
#include <xmmintrin.h>
inline float platform_rsqrt_estimate(float x) { return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x))); }
// RCPSS/RCPPS, the reciprocal estimate (per CPU vendor as well).
inline float platform_rcp_estimate(float x) { return _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(x))); }
#else
#include <cmath>
inline float platform_rsqrt_estimate(float x) { return 1.0f / std::sqrt(x); }
inline float platform_rcp_estimate(float x) { return 1.0f / x; }
#endif
