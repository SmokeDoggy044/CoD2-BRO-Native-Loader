#include "g_combat.h"

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

}  // namespace game
