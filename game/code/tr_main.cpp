#include "tr_main.h"

#include "title_data.h"

namespace game {

namespace {

struct Cvar;  // Q3's cvar_t; only `value` is needed so far
float cvar_value(const Cvar* cvar) {
    return *reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(cvar) + 0x1c);
}

// Scene lists: counts filled by the RE_Add*ToScene side and where the current
// scene's entries start in each (three pairs, which list is which is not known yet).
uint32_t& scene_count_a() { return title_global<uint32_t>(0x0095e628); }
uint32_t& scene_count_b() { return title_global<uint32_t>(0x0095e60c); }
uint32_t& scene_count_c() { return title_global<uint32_t>(0x0095e738); }
uint32_t& scene_first_a() { return title_global<uint32_t>(0x0095e6bc); }
uint32_t& scene_first_b() { return title_global<uint32_t>(0x0095e710); }
uint32_t& scene_first_c() { return title_global<uint32_t>(0x0095e6f4); }

}  // namespace

void XB_REGS3 myGLMultMatrix(const float* a, float* out, const float* b) {
    // Same operation order as the original (its float sums are not associative),
    // reading `a` after each store as it does, in case out == a.
    for (int i = 0; i < 4; ++i, a += 4, out += 4) {
        out[0] = a[2] * b[8] + b[12] * a[3] + b[4] * a[1] + a[0] * b[0];
        out[1] = a[3] * b[13] + a[1] * b[5] + b[9] * a[2] + a[0] * b[1];
        out[2] = a[3] * b[14] + a[1] * b[6] + a[0] * b[2] + b[10] * a[2];
        out[3] = b[11] * a[2] + a[3] * b[15] + a[1] * b[7] + b[3] * a[0];
    }
}

void XB_CDECL RE_ClearScene() {
    scene_first_a() = scene_count_a();
    scene_first_b() = scene_count_b();
    scene_first_c() = scene_count_c();
}

void XB_CDECL R_ClearFrameLists() {
    RFrameList* lists = title_global<RFrameList*>(0x008db128);
    for (int i = 0; i < 2; ++i) {
        lists[i].count = 0;
        lists[i].head = &lists[i].tail;
        lists[i].tail = nullptr;
        lists[i].tail_prev = &lists[i].head;
    }
    title_global<uint32_t>(0x0095e708) = 0;
    title_global<uint32_t>(0x0095e6e8) = 0;
    scene_count_a() = 0;
    scene_count_b() = 0;
    scene_count_c() = 0;
    scene_first_a() = 0;
    scene_first_b() = 0;
    scene_first_c() = 0;
}

void XB_CDECL RE_SetFarCullDist(float distance) {
    // A NaN is stored as given, as in the original.
    title_global<float>(0x0095e2cc) = 0.0f >= distance ? 0.0f : distance;
}

float XB_CDECL RE_GetFarCullDist() {
    const float zfar = cvar_value(title_global<const Cvar*>(0x0095e378));
    const float far_cull = title_global<float>(0x0095e2cc);
    return far_cull > zfar ? far_cull : zfar;
}

}  // namespace game
