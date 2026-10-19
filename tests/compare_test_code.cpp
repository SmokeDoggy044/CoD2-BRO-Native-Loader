// The "decompiled" side of compare_test, in a shared library of its own as the real
// decompiled code is (libcod_bro_game.so): the harness's test processes may run it.

#include <cstdint>

#include "compiler.h"

extern "C" {

// Set by the test to a function in its executable: host code a test process may not run.
void (*compare_test_host_function)();

// The original (compare_test.cpp): self[1] = self[0] + v, returned; thiscall.
uint32_t XB_THISCALL compare_test_add(uint32_t* self, uint32_t v) {
    self[1] = self[0] + v;
    return self[1];
}

uint32_t XB_THISCALL compare_test_add_off_by_one(uint32_t* self, uint32_t v) {
    self[1] = self[0] + v + 1;
    return self[1];
}

// Right result for v = 5, but pops no arguments.
uint32_t XB_FASTCALL compare_test_add_no_pop(uint32_t* self, uint32_t) {
    self[1] = self[0] + 5;
    return self[1];
}

uint32_t XB_THISCALL compare_test_add_host_call(uint32_t* self, uint32_t v) {
    compare_test_host_function();
    return compare_test_add(self, v);
}

uint32_t XB_THISCALL compare_test_add_hang(uint32_t*, uint32_t) {
    for (volatile int spin = 0;; spin = spin + 1) {
    }
}

uint32_t XB_THISCALL compare_test_add_crash(uint32_t*, uint32_t) {
    *static_cast<volatile uint32_t*>(nullptr) = 1;
    return 0;
}

// Set by the test to a host function the harness records instead of calling.
uint32_t(XB_STDCALL* compare_test_recorded_function)(uint32_t, const uint32_t*);

// The original (compare_test.cpp): recorded(self[0] + v, self); thiscall.
uint32_t XB_THISCALL compare_test_add_recorded(uint32_t* self, uint32_t v) {
    return compare_test_recorded_function(self[0] + v, self);
}

uint32_t XB_THISCALL compare_test_add_recorded_wrong(uint32_t* self, uint32_t v) {
    return compare_test_recorded_function(self[0] + v + 1, self);
}

// The original: x * x on the x87 stack; cdecl.
float XB_CDECL compare_test_square(float x) { return x * x; }

// The functions' addresses, taken here: in the (non-PIE) test executable, &function
// would be its PLT entry, which is host code to the harness.
void compare_test_code(const void** out) {
    const void* const all[] = {
        reinterpret_cast<const void*>(&compare_test_add),       reinterpret_cast<const void*>(&compare_test_add_off_by_one),
        reinterpret_cast<const void*>(&compare_test_add_no_pop), reinterpret_cast<const void*>(&compare_test_add_host_call),
        reinterpret_cast<const void*>(&compare_test_add_hang),  reinterpret_cast<const void*>(&compare_test_add_crash),
        reinterpret_cast<const void*>(&compare_test_square),    reinterpret_cast<const void*>(&compare_test_add_recorded),
        reinterpret_cast<const void*>(&compare_test_add_recorded_wrong),
    };
    for (const void* function : all) *out++ = function;
}

}  // extern "C"
