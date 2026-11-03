#include "pmove.h"

#include "actor.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

}  // namespace

// Every interval (0x0094d584 ms) the eye position of the sentient behind the entity handle +0x7c goes into the
// next of two history slots (0x0095f42c, 12 bytes each; time 0x0095f444; slot 0x0095f428, last time 0x0095f424).
void XB_CDECL Player_RecordEyeHistory(const uint8_t* pm) {
    const int32_t now = title_global<int32_t>(0x0098540c);  // level_time
    if (static_cast<int32_t>(static_cast<uint32_t>(now) - title_global<uint32_t>(0x0095f424)) <=
        title_global<int32_t>(0x0094d584))
        return;
    title_global<int32_t>(0x0095f424) = now;
    title_global<int32_t>(0x0095f428) = static_cast<int32_t>(title_global<uint32_t>(0x0095f428) + 1u) % 2;
    const uint32_t handle = at<uint32_t>(pm, 0x7c);
    const uint32_t index = handle & 0xfff;
    const uint8_t* ent = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
    alignas(4) uint8_t eye[12];
    Sentient_EyeOrigin(at<uint8_t*>(ent, 0x244), 0, eye);
    for (uint32_t i = 0; i < 3; ++i)
        title_global<uint32_t>(0x0095f42c + title_global<int32_t>(0x0095f428) * 12 + i * 4) = at<uint32_t>(eye, i * 4);
    title_global<int32_t>(0x0095f444 + title_global<int32_t>(0x0095f428) * 4) = title_global<int32_t>(0x0098540c);
}

// Compass marker word for the first listed actor (0x0097d370, count 0x0097d36c) with a marker icon
// (+0x240 -> +0x137c set, icon +0x1382 != -1, sentient +0x244 team 2) that is not visible from origin:
// offsets (rounded, scaled to fit -1022..1024, /4 + 255) in bits 6-14 (x) and 15-23 (y), the angle
// (+0x154 * 0.711) in bits 24-31, the icon in bits 0-5. 0 without one or with markers off.
uint32_t XB_CDECL Player_PackActorMarker(const float* origin) {
    if (!title_global<int32_t>(0x00986008)) return 0;  // g_compassMarkersEnabled
    const uint32_t base = 0x0097d370;
    const uint32_t end = base + title_global<uint32_t>(0x0097d36c) * 4;
    const uint8_t* actor = nullptr;
    for (uint32_t it = base; it != end; it += 4) {
        const uint8_t* const e = *reinterpret_cast<uint8_t* const*>(uintptr_t{it});
        if (!e) continue;
        const uint8_t* const a = at<uint8_t*>(e, 0x240);
        if (!a || !at<uint32_t>(a, 0x137c)) continue;
        if (at<int32_t>(at<uint8_t*>(e, 0x244), 4) != 2) continue;
        if (at<int16_t>(a, 0x1382) == -1) continue;
        if (!SV_EntityVisibleFromPoint(origin, at<uint32_t>(e, 0x224))) {
            actor = e;
            break;
        }
    }
    if (!actor) return 0;
    int32_t icon = -1;
    const uint8_t* const a = at<uint8_t*>(actor, 0x240);
    if (a && at<uint32_t>(a, 0x137c) && at<int32_t>(at<uint8_t*>(actor, 0x244), 4) == 2) icon = at<int16_t>(a, 0x1382);
    const float half = title_global<float>(0x0083eb24);
    int32_t x = static_cast<int32_t>((at<float>(actor, 0x140) - origin[0]) + half);
    int32_t y = static_cast<int32_t>((at<float>(actor, 0x144) - origin[1]) + half);
    const float high = title_global<float>(0x0083eff8), low = title_global<float>(0x0083eff4);
    const float one = title_global<float>(0x0083eb20);
    float sx = one, sy = one;
    if (x > 0x400)
        sx = high / static_cast<float>(x);
    else if (x < -0x3fe)
        sx = low / static_cast<float>(x);
    if (y > 0x400)
        sy = high / static_cast<float>(y);
    else if (y < -0x3fe)
        sy = low / static_cast<float>(y);
    if (one > sx || one > sy) {
        if (sy > sx)
            y = static_cast<int32_t>(static_cast<float>(y) * sx);
        else if (sx > sy)
            x = static_cast<int32_t>(static_cast<float>(x) * sy);
    }
    if (x > 0x400) x = 0x400;
    else if (x < -0x3fe) x = -0x3fe;
    if (y > 0x400) y = 0x400;
    else if (y < -0x3fe) y = -0x3fe;
    const uint32_t yv = static_cast<uint32_t>((y + 2) / 4 + 0xff) << 15;
    const uint32_t xv = static_cast<uint32_t>((x + 2) / 4 + 0xff) << 6;
    uint32_t r = (yv & 0xff8000u) | (xv & 0x7fc0u);
    r |= static_cast<uint32_t>(static_cast<int32_t>(at<float>(actor, 0x154) * title_global<float>(0x0083eff0))) << 24;
    return r | (static_cast<uint32_t>(icon) & 0x3f);
}

void XB_CDECL PM_AddWeaponFireBlend() {
    uint8_t* const* const pm = title_global<uint8_t**>(0x0092eff4);
    uint8_t* const ps = *pm;
    if (at<float>(ps, 0x88) == title_global<float>(0x0083eb20)) return;  // 1
    const float limit = title_global<float>(0x0083eb8c);                // 255
    at<float>(ps, 0x4f0) = at<float>(title_global<uint8_t*>(0x00960020), 0x660) * limit + at<float>(ps, 0x4f0);
    uint8_t* const again = *pm;
    if (at<float>(again, 0x4f0) > limit) at<float>(again, 0x4f0) = limit;
}

}  // namespace game
