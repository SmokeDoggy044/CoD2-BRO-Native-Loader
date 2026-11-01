#include "g_mover.h"

#include "g_spawn.h"
#include "g_utils.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

}  // namespace

PLATFORM_ASM_ENTRY(InitScriptMover_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(InitScriptMover) "addl $4, %esp\n"
                   "ret")

void XB_CDECL InitScriptMover(uint8_t* ent) {
    // The key hashes, made once (guard bits in 0x0098b84c).
    uint32_t guard = title_global<uint32_t>(0x0098b84c);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098b84c) = guard;
        title_global<uint32_t>(0x0098b848) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b07c}));  // "light"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098b84c) = guard;
        title_global<uint32_t>(0x0098b844) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b074}));  // "color"
    }
    float light;
    const uint32_t has_light = G_SpawnFloat(title_global<uint32_t>(0x0098b848), 100.0f, &light);
    const float one = title_global<float>(0x0083eb20);
    float fallback[3] = {one, one, one};
    float color[3];
    const uint32_t has_color =
        G_SpawnVector(title_global<uint32_t>(0x0098b844), color, reinterpret_cast<const uint32_t*>(fallback));
    if (has_light || has_color) {
        // A packed RGBA (alpha = light / 4), each channel clamped at 255 from above only.
        const float scale = title_global<float>(0x0083eb8c);
        int32_t r = static_cast<int32_t>(color[0] * scale);
        if (r > 0xff) r = 0xff;
        int32_t g = static_cast<int32_t>(color[1] * scale);
        if (g > 0xff) g = 0xff;
        int32_t b = static_cast<int32_t>(color[2] * scale);
        if (b > 0xff) b = 0xff;
        int32_t a = static_cast<int32_t>(light * title_global<float>(0x0083ec24));
        if (a > 0xff) a = 0xff;
        word_at(ent, 0xa8) = static_cast<uint32_t>((((a << 8 | b) << 8 | g) << 8) | r);
    }
    word_at(ent, 0x330) = 2;
    word_at(ent, 0xd4) = 0x80;
    ent[0] = 7;
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x44) = word_at(ent, 0x150);
    word_at(ent, 0x48) = word_at(ent, 0x154);
    word_at(ent, 0x4c) = word_at(ent, 0x158);
    const uint32_t flags = word_at(ent, 0x2a4) | 0x8000;
    word_at(ent, 0x38) = 0;
    word_at(ent, 0x2a4) = flags;
}

void XB_CDECL Think_InitDoor(uint8_t* ent) {
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 100;
    if (word_at(ent, 0x2a4) & 0x10) return;  // a team slave
    if (word_at(ent, 0x29c))
        word_at(ent, 0x32c) = 0x19;
    else if (word_at(ent, 0x260) == title_global<uint32_t>(0x0097a6e0))  // "func_door_rotating"
        word_at(ent, 0x32c) = 0x1b;
    else
        word_at(ent, 0x32c) = (ent[0x2a0] & 8) ? 0x1a : 0x19;
    // The team members take the master's +0x3b4.
    uint8_t* member = ent;
    do {
        if (member != ent) word_at(member, 0x3b4) = word_at(ent, 0x3b4);
        member = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(member, 0x37c)));
    } while (member);
}

void XB_CDECL Artillery_Drop(uint8_t* ent) {
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 30000;
    word_at(ent, 0x32c) = 8;
    const float z = *reinterpret_cast<const float*>(ent + 0x148) + title_global<float>(0x0083eb9c);
    const float x = *reinterpret_cast<const float*>(ent + 0x140);
    const float y = *reinterpret_cast<const float*>(ent + 0x144);
    float* const base = reinterpret_cast<float*>(ent + 0x20);
    base[0] = x;
    base[1] = y;
    base[2] = z;
    float* const current = reinterpret_cast<float*>(ent + 0x140);
    current[0] = x;
    current[1] = y;
    current[2] = z;
    word_at(ent, 0x14) = 2;  // gravity
    word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c) - 50;
}

}  // namespace game
