#include "g_combat.h"

#include <cstring>

#include "g_main.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

uint32_t XB_REGS3 XB_CDECL TriggerDamage_AcceptsMeansOfDeath(int32_t mod, uint32_t, const uint8_t* trigger) {
    // Spawn flags (+0x2a0) that make the trigger ignore groups of means of death.
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(trigger + 0x2a0);
    if ((flags & 0x1) && mod == 1) return 0;
    if ((flags & 0x2) && mod == 2) return 0;
    if ((flags & 0x4) && mod >= 3 && mod <= 6) return 0;
    if ((flags & 0x8) && ((mod >= 9 && mod <= 13) || mod == 0x17)) return 0;
    if ((flags & 0x10) && (mod == 4 || mod == 6 || mod == 10 || mod == 12)) return 0;  // case table 0x00248260
    if ((flags & 0x20) && mod == 7) return 0;
    if ((flags & 0x40) && mod == 0x19) return 0;
    if ((flags & 0x100) && (mod == 0 || (mod > 0xe && mod <= 0x16))) return 0;
    return 1;
}

uint32_t XB_REGS3 XB_CDECL G_ActorClampLethalDamage(uint32_t damage, uint32_t, const uint8_t* ent) {
    if (!damage) return damage;
    const uint32_t actor = *reinterpret_cast<const uint32_t*>(ent + 0x240);
    if (!actor) return 0;
    const TitleRegs regs{damage, 0, address_of(ent), 0, 0, actor};
    return g_ent_call(0x001fd7f0, &regs, 0, nullptr);  // Actor_ClampLethalDamage (EAX, EDI)
}

// The argument slot keeps level.time as a float (written before anything else, as the original).
PLATFORM_ASM_ENTRY(Pain_Func4_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Pain_Func4) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Pain_Func4(uint32_t* args) {
    uint8_t* const ent = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[0]));
    const float now = static_cast<float>(title_global<int32_t>(0x0098540c));  // level.time
    std::memcpy(&args[0], &now, sizeof now);
    const bool keep_origin = (ent[0x2a0] & 4) != 0;
    const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)() % 1000;  // _rand
    float limit = static_cast<float>(r) + *reinterpret_cast<const float*>(ent + 0x38c);
    limit = limit + *reinterpret_cast<const float*>(ent + 0x384);
    limit = limit + title_global<float>(0x0083ebec);
    float time;
    std::memcpy(&time, &args[0], sizeof time);
    if (!(time > limit)) return;
    *reinterpret_cast<float*>(ent + 0x384) = static_cast<float>(title_global<int32_t>(0x0098540c));
    if (keep_origin) {
        // The original copies +0x2e0 over the current origin and writes the old one back.
        uint32_t saved[3];
        std::memcpy(saved, ent + 0x140, sizeof saved);
        std::memcpy(ent + 0x140, ent + 0x2e0, sizeof saved);
        std::memcpy(ent + 0x140, saved, sizeof saved);
    }
}

}  // namespace game
