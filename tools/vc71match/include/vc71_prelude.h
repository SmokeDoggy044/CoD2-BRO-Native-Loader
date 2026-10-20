// Force-included (/FI) before every lowered game/code file: what platform/compiler.h
// only defines for GCC/Clang, and what Visual C++ 7.1's headers lack.
#pragma once
#include <cstdint>
#include <xmmintrin.h>
inline float _mm_cvtss_f32(__m128 v) {
    float f;
    _mm_store_ss(&f, v);
    return f;
}
#define PLATFORM_HIDDEN
#define PLATFORM_SECTION(name) __declspec(noinline)
#define PLATFORM_ASM_SYMBOL(name) #name
#define PLATFORM_ASM_CALL(name) ""
#define PLATFORM_ASM_ENTRY(name, body)
