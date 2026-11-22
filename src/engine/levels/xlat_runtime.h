#pragma once

// Runtime of the translated level code (tools/translator/lift.py): each level function as C over a register
// struct, entered through a stub at the function's row that behaves like the original x86 (any register convention,
// `ret N`). The C runs on a host-only stack; the guest stack (the title thread's) holds data only, at the same
// addresses as when the original runs. Guest addresses are 32-bit everywhere: title memory is identity-mapped below
// 4 GB in both builds.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <xmmintrin.h>

#include "compiler.h"
#include "functions.h"

namespace xl {

struct Cpu {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;  // 0
    uint32_t ret;                                     // 32: return address the function popped
    uint32_t ret_pop;                                 // 36: its `ret N`
    uint32_t xmm[8][4];                               // 40: XMM0-7 (LTCG passes floats in them, both ways)
    // 168: a float return travelling as the x87 ABI's st(0): the callee sets it (translated: XL_FRETURN; native:
    // xl_native takes what it pushed on the FPU), the caller takes it (XL_FRESULT); xl_run pushes it on the real FPU
    // for a native caller.
    uint32_t fret;
    uint8_t f80[12];
};
static_assert(offsetof(Cpu, xmm) == 40 && offsetof(Cpu, fret) == 168);

using Fn = void (*)(Cpu*);

[[noreturn]] void xl_divide_error();
[[noreturn]] void xl_trap();

// ---- guest memory ------------------------------------------------------------------------------------------------
typedef uint16_t __attribute__((may_alias, aligned(1))) u16a;
typedef uint32_t __attribute__((may_alias, aligned(1))) u32a;
inline uint32_t mem_r8(uint32_t a) { return *reinterpret_cast<const volatile uint8_t*>(uintptr_t{a}); }
inline uint32_t mem_r16(uint32_t a) { return *reinterpret_cast<const u16a*>(uintptr_t{a}); }
inline uint32_t mem_r32(uint32_t a) { return *reinterpret_cast<const u32a*>(uintptr_t{a}); }
inline void mem_w8(uint32_t a, uint32_t v) { *reinterpret_cast<uint8_t*>(uintptr_t{a}) = static_cast<uint8_t>(v); }
inline void mem_w16(uint32_t a, uint32_t v) { *reinterpret_cast<u16a*>(uintptr_t{a}) = static_cast<uint16_t>(v); }
inline void mem_w32(uint32_t a, uint32_t v) { *reinterpret_cast<u32a*>(uintptr_t{a}) = v; }
#define XL_R8(a) ::xl::mem_r8(a)
#define XL_R16(a) ::xl::mem_r16(a)
#define XL_R32(a) ::xl::mem_r32(a)
#define XL_W8(a, v) ::xl::mem_w8((a), (v))
#define XL_W16(a, v) ::xl::mem_w16((a), (v))
#define XL_W32(a, v) ::xl::mem_w32((a), (v))

// FS: the thread's Xbox block (SEH chain at FS:[0], the gap-entry floor at FS:[0x14]); records stay on the guest
// stack at the original's addresses.
#if defined(__i386__)
inline uint32_t fs_r32(uint32_t off) {
    uint32_t v;
    __asm__ volatile("movl %%fs:(%1), %0" : "=r"(v) : "r"(off) : "memory");
    return v;
}
inline void fs_w32(uint32_t off, uint32_t v) { __asm__ volatile("movl %1, %%fs:(%0)" : : "r"(off), "r"(v) : "memory"); }
#else
uint32_t fs_r32(uint32_t off);
void fs_w32(uint32_t off, uint32_t v);
#endif
#define XL_FS_R32(a) ::xl::fs_r32(a)
#define XL_FS_W32(a, v) ::xl::fs_w32((a), (v))

// ---- lazy flags --------------------------------------------------------------------------------------------------
// A kind = operation << 8 | operand bits. fa/fb = operands (MUL: fa = overflow; shifts: fb = count), fr = result
// (masked to the operand size), fc = carry in (ADC/SBB).
enum : uint32_t { XL_OP_ADD = 1, XL_OP_SUB, XL_OP_NEG, XL_OP_LOGIC, XL_OP_INC, XL_OP_DEC, XL_OP_SHL, XL_OP_SHR,
                  XL_OP_SAR, XL_OP_MUL, XL_OP_ADC, XL_OP_SBB, XL_OP_BT, XL_OP_RAW };
#define XL_KINDS(op) XL_K_##op##8 = XL_OP_##op << 8 | 8, XL_K_##op##16 = XL_OP_##op << 8 | 16, \
                     XL_K_##op##32 = XL_OP_##op << 8 | 32
enum : uint32_t { XL_KINDS(ADD), XL_KINDS(SUB), XL_KINDS(NEG), XL_KINDS(LOGIC), XL_KINDS(INC), XL_KINDS(DEC),
                  XL_KINDS(SHL), XL_KINDS(SHR), XL_KINDS(SAR), XL_KINDS(MUL), XL_KINDS(ADC), XL_KINDS(SBB),
                  XL_KINDS(BT), XL_KINDS(RAW) };  // RAW: fr = EFLAGS as the instruction left them (ucomiss)
#undef XL_KINDS

inline uint32_t k_bits(uint32_t k) { return k & 0xff; }
inline uint32_t k_msb(uint32_t k) { return 1u << (k_bits(k) - 1); }
inline int32_t k_signed(uint32_t k, uint32_t v) {  // v sign-extended from the operand size
    return k_bits(k) == 32 ? static_cast<int32_t>(v)
                           : static_cast<int32_t>(v << (32 - k_bits(k))) >> (32 - k_bits(k));
}

inline bool f_c(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) {
    switch (k >> 8) {
    case XL_OP_ADD: return r < a;
    case XL_OP_SUB: return a < b;
    case XL_OP_ADC: return c ? r <= a : r < a;
    case XL_OP_SBB: return c ? a <= b : a < b;
    case XL_OP_NEG: return a != 0;
    case XL_OP_SHL: return b <= k_bits(k) && ((a >> (k_bits(k) - b)) & 1u);
    case XL_OP_SHR: return (a >> (b - 1)) & 1u;
    case XL_OP_SAR: return (static_cast<uint32_t>(k_signed(k, a) >> (b - 1))) & 1u;
    case XL_OP_MUL: return a != 0;
    case XL_OP_BT: return r != 0;
    case XL_OP_RAW: return (r & 0x1u) != 0;
    default: return false;  // LOGIC; INC/DEC keep CF (the translator refuses those readers)
    }
}
inline bool f_z(uint32_t k, uint32_t, uint32_t, uint32_t r) { return (k >> 8) == XL_OP_RAW ? (r & 0x40u) != 0 : r == 0; }
inline bool f_s(uint32_t k, uint32_t, uint32_t, uint32_t r) {
    return (k >> 8) == XL_OP_RAW ? (r & 0x80u) != 0 : (r & k_msb(k)) != 0;
}
inline bool f_p(uint32_t k, uint32_t, uint32_t, uint32_t r) {
    return (k >> 8) == XL_OP_RAW ? (r & 0x4u) != 0 : !__builtin_parity(r & 0xffu);
}
inline bool f_o(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) {
    const uint32_t m = k_msb(k);
    switch (k >> 8) {
    case XL_OP_ADD:
    case XL_OP_ADC: return (~(a ^ b) & (a ^ r) & m) != 0;
    case XL_OP_SUB:
    case XL_OP_SBB: return ((a ^ b) & (a ^ r) & m) != 0;
    case XL_OP_NEG: return r == m;
    case XL_OP_INC: return r == m;
    case XL_OP_DEC: return a == m;
    case XL_OP_SHL: return ((r & m) != 0) != f_c(k, a, b, r, c);
    case XL_OP_SHR: return (a & m) != 0;
    case XL_OP_MUL: return a != 0;
    case XL_OP_RAW: return (r & 0x800u) != 0;
    default: return false;  // LOGIC, SAR, BT
    }
}

// Conditions. SUB (cmp) compares the operands directly - the common case, and what the compiler folds best.
#define XL_SUB_OR(k, direct, generic) (((k) >> 8) == XL_OP_SUB ? (direct) : (generic))
inline bool XL_CC_o(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return f_o(k, a, b, r, c); }
inline bool XL_CC_no(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return !f_o(k, a, b, r, c); }
inline bool XL_CC_b(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return f_c(k, a, b, r, c); }
inline bool XL_CC_ae(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return !f_c(k, a, b, r, c); }
inline bool XL_CC_e(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t) { return f_z(k, a, b, r); }
inline bool XL_CC_ne(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t) { return !f_z(k, a, b, r); }
inline bool XL_CC_be(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) {
    return XL_SUB_OR(k, a <= b, f_c(k, a, b, r, c) || f_z(k, a, b, r));
}
inline bool XL_CC_a(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return !XL_CC_be(k, a, b, r, c); }
inline bool XL_CC_s(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t) { return f_s(k, a, b, r); }
inline bool XL_CC_ns(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t) { return !f_s(k, a, b, r); }
inline bool XL_CC_p(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t) { return f_p(k, a, b, r); }
inline bool XL_CC_np(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t) { return !f_p(k, a, b, r); }
inline bool XL_CC_l(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) {
    return XL_SUB_OR(k, k_signed(k, a) < k_signed(k, b), f_s(k, a, b, r) != f_o(k, a, b, r, c));
}
inline bool XL_CC_ge(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return !XL_CC_l(k, a, b, r, c); }
inline bool XL_CC_le(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) {
    return XL_SUB_OR(k, k_signed(k, a) <= k_signed(k, b), f_z(k, a, b, r) || f_s(k, a, b, r) != f_o(k, a, b, r, c));
}
inline bool XL_CC_g(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) { return !XL_CC_le(k, a, b, r, c); }
#undef XL_SUB_OR
// LAHF: SF ZF 0 AF 0 PF 1 CF (AF only where the producer left raw flags; the title tests AH with 0x41/0x44/0x05).
inline uint32_t XL_LAHF(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t c) {
    return (f_s(k, a, b, r) ? 0x80u : 0u) | (f_z(k, a, b, r) ? 0x40u : 0u) |
           ((k >> 8) == XL_OP_RAW ? (r & 0x10u) : 0u) | (f_p(k, a, b, r) ? 0x04u : 0u) | 0x02u |
           (f_c(k, a, b, r, c) ? 0x01u : 0u);
}

// ---- SSE ---------------------------------------------------------------------------------------------------------
// Intrinsics compile to the instruction itself: the same rounding (the title's MXCSR) and the same sticky flags.
inline __m128 xmm_ld(const uint32_t* v) { return _mm_loadu_ps(reinterpret_cast<const float*>(v)); }
inline void xmm_st(uint32_t* v, __m128 x) { _mm_storeu_ps(reinterpret_cast<float*>(v), x); }
inline __m128 ld_ss(uint32_t a) { return _mm_load_ss(reinterpret_cast<const float*>(uintptr_t{a})); }
inline void st_ss(uint32_t a, __m128 x) { _mm_store_ss(reinterpret_cast<float*>(uintptr_t{a}), x); }
inline __m128 ld_ps(uint32_t a) { return _mm_loadu_ps(reinterpret_cast<const float*>(uintptr_t{a})); }
inline void st_ps(uint32_t a, __m128 x) { _mm_storeu_ps(reinterpret_cast<float*>(uintptr_t{a}), x); }
inline __m128 ss_from_bits(uint32_t v) { return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<int>(v))); }
inline uint32_t ss_bits(__m128 x) {
    float f = _mm_cvtss_f32(x);
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
inline uint32_t eflags_after_ucomiss(__m128 a, __m128 b) {
    uintptr_t f;
    __asm__ volatile("ucomiss %2, %1\n pushf\n pop %0" : "=r"(f) : "x"(a), "x"(b) : "cc");
    return static_cast<uint32_t>(f);
}
inline uint32_t eflags_after_comiss(__m128 a, __m128 b) {
    uintptr_t f;
    __asm__ volatile("comiss %2, %1\n pushf\n pop %0" : "=r"(f) : "x"(a), "x"(b) : "cc");
    return static_cast<uint32_t>(f);
}
// ---- x87 ---------------------------------------------------------------------------------------------------------
// The x87 stack lives in long double locals (the lifter knows each instruction's stack depth). long double arithmetic
// is x87 arithmetic in both 32- and 64-bit builds, under the thread's own control word (precision, rounding), so the
// results are the original's to the bit.
inline long double ld_f32(uint32_t a) {
    float f;
    std::memcpy(&f, reinterpret_cast<const void*>(uintptr_t{a}), 4);
    return f;
}
inline long double ld_f64(uint32_t a) {
    double d;
    std::memcpy(&d, reinterpret_cast<const void*>(uintptr_t{a}), 8);
    return d;
}
inline long double ld_f80(const void* p) {
    long double v = 0;
    std::memcpy(&v, p, 10);
    return v;
}
inline void st_f32(uint32_t a, long double v) {
    const float f = static_cast<float>(v);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{a}), &f, 4);
}
inline void st_f64(uint32_t a, long double v) {
    const double d = static_cast<double>(v);
    std::memcpy(reinterpret_cast<void*>(uintptr_t{a}), &d, 8);
}
inline void st_f80(void* p, long double v) { std::memcpy(p, &v, 10); }
// EFLAGS after fcomip st(0), st(i): ZF, PF, CF as the compare sets them.
inline uint32_t eflags_after_fcomi(long double a, long double b) {
    uintptr_t f;
    __asm__ volatile("fcomip %%st(1), %%st\n pushf\n pop %0" : "=r"(f) : "t"(a), "u"(b) : "st", "cc");
    return static_cast<uint32_t>(f);
}
[[noreturn]] void xl_fret_missing();
#define XL_FRETURN(v) (::xl::st_f80(c->f80, (v)), c->fret = 1u)
#define XL_FRESULT(v) (c->fret ? (void)0 : ::xl::xl_fret_missing(), (v) = ::xl::ld_f80(c->f80), c->fret = 0u)

#define XL_LOAD_X()                                                                                           \
    __m128 xmm0 = ::xl::xmm_ld(c->xmm[0]), xmm1 = ::xl::xmm_ld(c->xmm[1]), xmm2 = ::xl::xmm_ld(c->xmm[2]),   \
           xmm3 = ::xl::xmm_ld(c->xmm[3]), xmm4 = ::xl::xmm_ld(c->xmm[4]), xmm5 = ::xl::xmm_ld(c->xmm[5]),   \
           xmm6 = ::xl::xmm_ld(c->xmm[6]), xmm7 = ::xl::xmm_ld(c->xmm[7])
#define XL_SAVE_X()                                                                                           \
    (::xl::xmm_st(c->xmm[0], xmm0), ::xl::xmm_st(c->xmm[1], xmm1), ::xl::xmm_st(c->xmm[2], xmm2),             \
     ::xl::xmm_st(c->xmm[3], xmm3), ::xl::xmm_st(c->xmm[4], xmm4), ::xl::xmm_st(c->xmm[5], xmm5),             \
     ::xl::xmm_st(c->xmm[6], xmm6), ::xl::xmm_st(c->xmm[7], xmm7))
#define XL_RELOAD_X()                                                                                         \
    (xmm0 = ::xl::xmm_ld(c->xmm[0]), xmm1 = ::xl::xmm_ld(c->xmm[1]), xmm2 = ::xl::xmm_ld(c->xmm[2]),          \
     xmm3 = ::xl::xmm_ld(c->xmm[3]), xmm4 = ::xl::xmm_ld(c->xmm[4]), xmm5 = ::xl::xmm_ld(c->xmm[5]),          \
     xmm6 = ::xl::xmm_ld(c->xmm[6]), xmm7 = ::xl::xmm_ld(c->xmm[7]))

// ---- functions and their entries ---------------------------------------------------------------------------------
#define XL_FUNCTION(addr) extern "C" PLATFORM_HIDDEN void xl_fn_##addr(::xl::Cpu* c)
#define XL_DECLARE(addr) extern "C" PLATFORM_HIDDEN void xl_fn_##addr(::xl::Cpu* c);
#define XL_LOAD()                                                                                                 \
    uint32_t eax = c->eax, ecx = c->ecx, edx = c->edx, ebx = c->ebx, esp = c->esp, ebp = c->ebp, esi = c->esi, \
             edi = c->edi
#define XL_SAVE() \
    (c->eax = eax, c->ecx = ecx, c->edx = edx, c->ebx = ebx, c->esp = esp, c->ebp = ebp, c->esi = esi, c->edi = edi)
#define XL_RELOAD() \
    (eax = c->eax, ecx = c->ecx, edx = c->edx, ebx = c->ebx, esp = c->esp, ebp = c->ebp, esi = c->esi, edi = c->edi)
#define XL_RETURN(n)                \
    do {                            \
        c->ret = XL_R32(esp);       \
        c->ret_pop = (n);           \
        esp += 4u + (n);            \
        XL_SAVE();                  \
        return;                     \
    } while (0)
// A call: the original's return address goes on the guest stack (its contents stay the original's); a translated
// callee is a C call, anything else runs natively (xl_native).
#define XL_CALL_XL(addr, next) (esp -= 4u, XL_W32(esp, next), XL_SAVE(), xl_fn_##addr(c), XL_RELOAD())
#define XL_CALL_NATIVE(target, next) \
    (esp -= 4u, XL_W32(esp, next), XL_SAVE(), ::xl::xl_native(c, target), XL_RELOAD())
#define XL_CALL_DYN(target, next) \
    (esp -= 4u, XL_W32(esp, next), XL_SAVE(), ::xl::xl_call(c, target), XL_RELOAD())
// A tail call (jmp to another function): the callee returns to our caller; c already holds the result.
#define XL_TAIL_XL(addr) \
    do {                 \
        XL_SAVE();       \
        xl_fn_##addr(c); \
        return;          \
    } while (0)
#define XL_TAIL_NATIVE(target)               \
    do {                                     \
        XL_SAVE();                           \
        ::xl::xl_native_tail(c, target);     \
        return;                              \
    } while (0)
#define XL_TAIL_DYN(target)            \
    do {                               \
        XL_SAVE();                     \
        ::xl::xl_tail(c, target);      \
        return;                        \
    } while (0)

// Calls out of translated code (xlat_runtime.cpp).
void xl_native(Cpu* c, uint32_t target);       // c->esp points at the return slot the caller pushed
void xl_native_tail(Cpu* c, uint32_t target);  // c->esp points at our own caller's return slot
void xl_call(Cpu* c, uint32_t target);         // translated if the address has a translated function, else native
void xl_tail(Cpu* c, uint32_t target);

// The generated code's address -> function table (sorted by address), one per level.
struct Entry {
    uint32_t address;
    Fn fn;
};
// One level's table; its entry stubs number their functions level << 16 | index (lift.py LEVELS).
void register_table(const Entry* entries, size_t count, uint32_t level);
// One level's game::Function rows (what game::translated_functions() lists).
void register_rows(const game::Function* rows, size_t count);
// Swaps the running guest stack's host stack pointer (FS word; scr_fiber.cpp when it switches fibers).
uint32_t host_sp_swap(uint32_t sp);

// Per-thread words in FS (Linux: spare PRCB words of our KPCR, src/runtime/loader/thread_env.h; the title reads
// none of them). The entry and the way out touch nothing on the guest stack the original does not touch - the game
// reads stale stack, and our engine's gap entries keep live C frames below the title's ESP (title_data.h).
//   0x300 the running guest stack's host stack pointer (script fibers swap it, scr_fiber.cpp; 0 = not made yet)
//   0x304 the guest ESP at an entry / to leave with
//   0x308 the entered function's index / the `ret N` thunk to leave through
//   0x30c a call out's target
#define XL_FS_HOST "0x300"
#define XL_FS_ESP "0x304"
#define XL_FS_INDEX "0x308"
#define XL_FS_TARGET "0x30c"
constexpr uint32_t kFsHost = 0x300, kFsEsp = 0x304, kFsIndex = 0x308;

#if defined(__i386__) && defined(__linux__)
// The row's code: the function's index (an immediate - the Linux game code is position-independent) into FS, then
// xl_enter_common (xlat_runtime.cpp) moves to the host stack and runs it.
#define XL_ENTRY(addr, index) XL_ENTRY2(xl_entry_##addr, index)
#define XL_ENTRY2(entry, index)                                                       \
    extern "C" void entry();                                                          \
    PLATFORM_ASM_ENTRY(entry, "movl $" #index ", %fs:" XL_FS_INDEX "\n jmp " PLATFORM_ASM_SYMBOL(xl_enter_common) "\n")
#define XL_CODE(addr) reinterpret_cast<const void*>(&::xl::xl_entry_##addr)
#else
#define XL_ENTRY(addr, index)
#define XL_CODE(addr) nullptr
#endif

}  // namespace xl

extern "C" {
// Makes the running guest stack's host stack (FS word 0x300 was 0): a script fiber's own, or the thread's.
PLATFORM_HIDDEN void xl_host_init();
struct XlFrame;
PLATFORM_HIDDEN void xl_run(XlFrame* frame);
}
