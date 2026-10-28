#include "r_static_init.h"

#include <cstring>

#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

// Four floats stored as one 16-byte block (the original builds them on its aligned stack and
// moves them with MOVAPS): bit copies, no arithmetic.
void store_vec4(uint32_t address, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    const uint32_t lanes[4] = {x, y, z, w};
    std::memcpy(&title_global<uint8_t>(address), lanes, sizeof lanes);
}

// `count` arrays of 15 script strings, `stride` bytes apart, ending at `end`: the CRT's
// `eh vector destructor iterator' (0x00011280, stdcall: array, size, count, destructor) with
// ScriptString_Release, the last array first.
void release_script_string_arrays(uint32_t end, uint32_t stride, int32_t count) {
    using Iterator = void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t array = end;
    for (int32_t i = 0; i < count; ++i) {
        array -= stride;
        title_function<Iterator>(0x00011280)(array, 4, 15, 0x0011d640);  // ScriptString_Release
    }
}

// Mem_AlignedFree of the block a global holds, the global cleared.
void free_aligned_global(uint32_t address) {
    void* block = title_global<void*>(address);
    if (!block) return;
    Mem_AlignedFree(block);
    title_global<uint32_t>(address) = 0;
}

void hash_str_map_clear(uint32_t map) {
    title_function<void XB_THISCALL(uint32_t)>(0x00094010)(map);  // HashStrMap_Clear
}

}  // namespace

void XB_CDECL StaticInit_DownVec4_9717e0() {
    const uint32_t minus_one = title_global<uint32_t>(0x0083eb34);  // -1.0f
    store_vec4(0x009717e0, 0, 0, minus_one, 0);
}

void XB_CDECL StaticInit_ngl_multisampleScale() {
    const uint32_t one = title_global<uint32_t>(0x0083eb20);  // 1.0f
    store_vec4(0x00903c70, one, one, 0, 0);
}

void XB_CDECL StaticInit_FxParticleBounds() {
    const uint32_t big = title_global<uint32_t>(0x007c5000);  // FLT_MAX
    const uint32_t* sign = &title_global<uint32_t>(0x007c52d0);
    store_vec4(0x00909ee0, big, big, big, big);
    store_vec4(0x00909ef0, big ^ sign[0], big ^ sign[1], big ^ sign[2], big ^ sign[3]);
}

void XB_CDECL AtExit_FxScriptStrings_970844() { release_script_string_arrays(0x009717e0, 0xf9c, 2); }

void XB_CDECL AtExit_FxScriptStrings_9718a0() { release_script_string_arrays(0x00971900, 0x60, 2); }

void XB_CDECL AtExit_FxScriptStrings_96e90c() { release_script_string_arrays(0x0096f8a8, 0xf9c, 2); }

void XB_CDECL AtExit_FxScriptStrings_971970() { release_script_string_arrays(0x009719d0, 0x60, 2); }

void XB_CDECL AtExit_ReleaseScriptString_971908() {
    title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(0x00971908);  // ScriptString_Release
}

void XB_CDECL AtExit_FxEffectList() { free_aligned_global(0x00988ce8); }

void XB_CDECL AtExit_FxEffectPool() { free_aligned_global(0x00988804); }

void XB_CDECL AtExit_FreeAligned_988714() { free_aligned_global(0x00988714); }

void XB_CDECL AtExit_ShaderCache_902ac0() { hash_str_map_clear(0x00902ac0); }

void XB_CDECL AtExit_ShaderCache_902ad4() { hash_str_map_clear(0x00902ad4); }

void XB_CDECL AtExit_NalClassMap_903fe0() { hash_str_map_clear(0x00903fe0); }

void XB_CDECL StaticInit_NalConfig_8bb124() { title_global<uint32_t>(0x008bb124) = 0x0083760c; }

}  // namespace game
