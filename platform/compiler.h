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
// sin(angle) * scale with the sine kept in extended precision (FSIN; FMUL).
inline float platform_x87_sin_mul(float angle, float scale) {
    float result;
    __asm__("fsin\n\tfmuls %2" : "=t"(result) : "0"(angle), "m"(scale));
    return result;
}
// (sin(x * a + b) * c + sin(x + x)) * d * e, all on the x87 (CoD BRO CG_BobCycleLateral).
inline float platform_x87_sin_sum_mul(float x, float a, float b, float c, float d, float e) {
    float result;
    __asm__("flds %1\n\tfmuls %2\n\tfadds %3\n\tfsin\n\tfmuls %4\n\tflds %1\n\tfadd %%st(0), %%st\n\tfsin\n\t"
            "faddp %%st, %%st(1)\n\tfmuls %5\n\tfmuls %6"
            : "=&t"(result)
            : "m"(x), "m"(a), "m"(b), "m"(c), "m"(d), "m"(e));
    return result;
}
// FPTAN kept in extended precision (the title divides two of them on the x87).
inline long double platform_x87_tan_ext(long double x) {
    long double result;
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
// half / sqrt(x) and sqrt((one - x) * half) on the x87 (FSQRT/FDIVR, FSUB/FMUL/FSQRT;
// the caller's precision control), stored as float: no MXCSR flags.
inline float platform_x87_half_over_sqrt(float x, float half) {
    float result;
    __asm__("flds %1\n\tfsqrt\n\tfdivrs %2\n\tfstps %0" : "=m"(result) : "m"(x), "m"(half) : "st");
    return result;
}
inline float platform_x87_sqrt_sub_mul(float one, float x, float half) {
    float result;
    __asm__("flds %1\n\tfsubs %2\n\tfmuls %3\n\tfsqrt\n\tfstps %0" : "=m"(result) : "m"(one), "m"(x), "m"(half) : "st");
    return result;
}
// sqrt(a + b) and sqrt(((a - b) - c) + d) wholly on the x87 (FLD/FADD/FSUB/FSQRT/FSTP,
// the caller's precision control), stored as float: no MXCSR flags.
inline float platform_x87_add_sqrt(float a, float b) {
    float result;
    __asm__("flds %1\n\tfadds %2\n\tfsqrt\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}
inline float platform_x87_sub_sub_add_sqrt(float a, float b, float c, float d) {
    float result;
    __asm__("flds %1\n\tfsubs %2\n\tfsubs %3\n\tfadds %4\n\tfsqrt\n\tfstps %0"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(c), "m"(d)
            : "st");
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
// a > b compared on the x87 (FLD/FCOMIP: no MXCSR flags; false when unordered).
inline bool platform_x87_greater(float a, float b) {
    bool result;
    __asm__("flds %2\n\tflds %1\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %0"
            : "=q"(result)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return result;
}
// a >= b compared on the x87 (FLD/FCOMIP: no MXCSR flags; false when unordered).
inline bool platform_x87_greater_equal(float a, float b) {
    bool result;
    __asm__("flds %2\n\tflds %1\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tsetae %0"
            : "=q"(result)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return result;
}
// a * b on the x87 (FLD/FMUL, the caller's precision and rounding control), stored as
// float: no MXCSR flags.
inline float platform_x87_mul(float a, float b) {
    float result;
    __asm__("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
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
// y = ln(k) * t (FLDLN2/FYL2X, FMUL), then (half * y + one) * y + one - a second-order
// exp(y) - all on the x87 (the caller's precision control), stored as float.
inline float platform_x87_exp2_series(float k, float t, float half, float one) {
    float result;
    __asm__("flds %1\n\tfldln2\n\tfxch %%st(1)\n\tfyl2x\n\tfmuls %2\n\tflds %3\n\tfmul %%st(1), %%st\n\t"
            "fadds %4\n\tfmul %%st(1), %%st\n\tfadds %4\n\tfstps %0\n\tfstp %%st(0)"
            : "=m"(result)
            : "m"(k), "m"(t), "m"(half), "m"(one)
            : "st", "st(1)");
    return result;
}
// (a + b) * c on the x87 (the caller's precision control), stored as float.
inline float platform_x87_add_mul(float a, float b, float c) {
    float result;
    __asm__("flds %1\n\tfadds %2\n\tfmuls %3\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(c) : "st");
    return result;
}
// one / sqrt(((a*a + b*b) + c*c) + d*d) on the x87 (the caller's precision control),
// stored as float.
inline float platform_x87_rcp_norm4(float a, float b, float c, float d, float one) {
    float result;
    __asm__("flds %1\n\tfmuls %1\n\tflds %2\n\tfmuls %2\n\tfaddp\n\tflds %3\n\tfmuls %3\n\tfaddp\n\t"
            "flds %4\n\tfmuls %4\n\tfaddp\n\tfsqrt\n\tfdivrs %5\n\tfstps %0"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(c), "m"(d), "m"(one)
            : "st", "st(1)");
    return result;
}
// sqrt((a*a + b*b) + c*c) and sqrt(one - a*a) on the x87 (the caller's precision
// control), stored as float: no MXCSR flags.
inline float platform_x87_norm3(float a, float b, float c) {
    float result;
    __asm__("flds %1\n\tfmuls %1\n\tflds %2\n\tfmuls %2\n\tfaddp\n\tflds %3\n\tfmuls %3\n\tfaddp\n\t"
            "fsqrt\n\tfstps %0"
            : "=m"(result)
            : "m"(a), "m"(b), "m"(c)
            : "st", "st(1)");
    return result;
}
inline float platform_x87_sqrt_one_minus_sq(float a, float one) {
    float result;
    __asm__("flds %1\n\tfmuls %1\n\tfsubrs %2\n\tfsqrt\n\tfstps %0" : "=m"(result) : "m"(a), "m"(one) : "st");
    return result;
}
// limit > sqrt((dz*dz + dy*dy) + dx*dx), d = a - b per axis, all on the x87 (FSUB, FMUL,
// FADDP, FSQRT, FCOMIP; the caller's precision control); false when unordered.
inline bool platform_x87_greater_than_dist3(float limit, float ax, float bx, float ay, float by, float az, float bz) {
    bool result;
    __asm__("flds %1\n\tfsubs %2\n\tflds %3\n\tfsubs %4\n\tflds %5\n\tfsubs %6\n\t"
            "fld %%st(0)\n\tfmul %%st(1), %%st\n\tfld %%st(2)\n\tfmul %%st(3), %%st\n\tfaddp\n\t"
            "fld %%st(3)\n\tfmul %%st(4), %%st\n\tfaddp\n\tfsqrt\n\t"
            "fstp %%st(3)\n\tfstp %%st(0)\n\tfstp %%st(0)\n\t"
            "flds %7\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %0"
            : "=q"(result)
            : "m"(ax), "m"(bx), "m"(ay), "m"(by), "m"(az), "m"(bz), "m"(limit)
            : "st", "st(1)", "st(2)", "st(3)", "st(4)", "cc");
    return result;
}
// ((a * b) + c) + d on the x87 (FLD/FMUL/FADD/FADD, the caller's precision control),
// stored as float.
inline float platform_x87_mul_add_add(float a, float b, float c, float d) {
    float result;
    __asm__("flds %1\n\tfmuls %2\n\tfadds %3\n\tfadds %4\n\tfstps %0"
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
// |value * a * b| on the x87 (FILD, FMUL, FMUL, FABS; the caller's precision control),
// stored as float.
inline float platform_x87_abs_int_mul_mul(std::int32_t value, float a, float b) {
    float result;
    __asm__("fildl %1\n\tfmuls %2\n\tfmuls %3\n\tfabs\n\tfstps %0"
            : "=m"(result)
            : "m"(value), "m"(a), "m"(b)
            : "st");
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
// a * b with b a double (FLD/FMUL QWORD, the caller's precision control), stored as float.
inline float platform_x87_mul_double(float a, double b) {
    float result;
    __asm__("flds %1\n\tfmull %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}
// ((a2 * b2 + a1 * b1) + a0 * b0) / (tan(angle * scale) * d) * k wholly on the x87 (the
// sum kept in the register stack, FPTAN, FDIVP st(1),st as raw bytes: GAS swaps the
// operand-less fdivp/fdivrp), stored as float.
inline float platform_x87_dot3_div_tan_mul(float a0, float a1, float a2, float b0, float b1, float b2, float angle,
                                           float scale, float d, float k) {
    float result;
    __asm__("flds %3\n\tfmuls %6\n\tflds %2\n\tfmuls %5\n\tfaddp\n\tflds %1\n\tfmuls %4\n\tfaddp\n\t"
            "flds %7\n\tfmuls %8\n\tfptan\n\tfstp %%st(0)\n\tfmuls %9\n\t.byte 0xde, 0xf9\n\t"
            "fmuls %10\n\tfstps %0"
            : "=m"(result)
            : "m"(a0), "m"(a1), "m"(a2), "m"(b0), "m"(b1), "m"(b2), "m"(angle), "m"(scale), "m"(d), "m"(k)
            : "st", "st(1)", "st(2)");
    return result;
}
#else
#include <cmath>
inline float platform_x87_mul_double(float a, double b) { return static_cast<float>(a * b); }
inline float platform_x87_dot3_div_tan_mul(float a0, float a1, float a2, float b0, float b1, float b2, float angle,
                                           float scale, float d, float k) {
    return ((a2 * b2 + a1 * b1) + a0 * b0) / (std::tan(angle * scale) * d) * k;
}
inline float platform_x87_unsigned_to_float(std::uint32_t value) { return static_cast<float>(value); }
inline float platform_x87_div_by_unsigned(float numerator, std::uint32_t value) {
    return numerator / static_cast<float>(value);
}
inline float platform_x87_sin(float x) { return std::sin(x); }
inline float platform_x87_cos(float x) { return std::cos(x); }
inline float platform_x87_tan(float x) { return std::tan(x); }
inline long double platform_x87_tan_ext(long double x) { return std::tan(x); }
inline float platform_x87_sin_mul(float angle, float scale) { return std::sin(angle) * scale; }
inline float platform_x87_sin_sum_mul(float x, float a, float b, float c, float d, float e) {
    return (std::sin(x * a + b) * c + std::sin(x + x)) * d * e;
}
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
inline float platform_x87_add_mul(float a, float b, float c) { return (a + b) * c; }
inline float platform_x87_rcp_norm4(float a, float b, float c, float d, float one) {
    return one / std::sqrt(((a * a + b * b) + c * c) + d * d);
}
inline float platform_x87_exp2_series(float k, float t, float half, float one) {
    const float y = std::log(k) * t;
    return (half * y + one) * y + one;
}
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
inline float platform_x87_half_over_sqrt(float x, float half) { return half / std::sqrt(x); }
inline float platform_x87_sqrt_sub_mul(float one, float x, float half) { return std::sqrt((one - x) * half); }
inline float platform_x87_add_sqrt(float a, float b) { return std::sqrt(a + b); }
inline float platform_x87_sub_sub_add_sqrt(float a, float b, float c, float d) { return std::sqrt(((a - b) - c) + d); }
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

// ---- Game module (g_scr_math.cpp): x87 operations whose result stays in extended precision
// (script builtins return it in ST0 unrounded). Plain long double arithmetic covers
// FADD/FMUL/FDIV (the title's precision control applies alike); these are the operations
// without a C spelling that would stay inline in -O0 builds.
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
inline long double platform_x87_sin_ext(long double x) {
    long double result;
    __asm__("fsin" : "=t"(result) : "0"(x));
    return result;
}
inline long double platform_x87_cos_ext(long double x) {
    long double result;
    __asm__("fcos" : "=t"(result) : "0"(x));
    return result;
}
inline long double platform_x87_sqrt_ext(long double x) {
    long double result;
    __asm__("fsqrt" : "=t"(result) : "0"(x));
    return result;
}
inline long double platform_x87_abs_ext(long double x) {
    long double result;
    __asm__("fabs" : "=t"(result) : "0"(x));
    return result;
}
// FLD1; FPATAN: atan(x / 1).
inline long double platform_x87_atan_ext(long double x) {
    long double result;
    __asm__("fld1\n\tfpatan" : "=t"(result) : "0"(x));
    return result;
}
// FLDLN2; FXCH; FYL2X: ln 2 * log2(x).
inline long double platform_x87_ln_ext(long double x) {
    long double result;
    __asm__("fldln2\n\tfxch %%st(1)\n\tfyl2x" : "=t"(result) : "0"(x));
    return result;
}
#else
#include <cmath>
inline long double platform_x87_sin_ext(long double x) { return std::sin(x); }
inline long double platform_x87_cos_ext(long double x) { return std::cos(x); }
inline long double platform_x87_sqrt_ext(long double x) { return std::sqrt(x); }
inline long double platform_x87_abs_ext(long double x) { return std::fabs(x); }
inline long double platform_x87_atan_ext(long double x) { return std::atan(x); }
inline long double platform_x87_ln_ext(long double x) { return std::log(x); }
#endif
