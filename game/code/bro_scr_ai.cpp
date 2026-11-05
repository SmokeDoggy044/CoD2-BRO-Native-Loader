#include "bro_scr_ai.h"

#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

namespace {

// The word for the entity's team in the per-team arrays (index 0-3), or null for another team.
// The team string is hashed and released before the switch.
uint32_t* PerTeamValue(uint32_t entity, uint32_t index) {
    ScriptString team;
    ScrCall_ScrSentient_GetTeam(&entity, &team);
    uint32_t hash_slot;
    const uint32_t hash = *Scr_HashScriptString(&hash_slot, reinterpret_cast<const uint8_t* const*>(&team));
    scr_release(&team);
    uint32_t base;
    switch (hash) {
    case 0x00316d53: base = 0x1c; break;   // "axis"
    case 0x000189ec: base = 0xf8; break;   // "all"
    case 0x4d6bc457: base = 0xd0; break;   // "neutral"
    case 0xc4f7a8d3: base = 0x74; break;   // "allies"
    default: return nullptr;
    }
    return reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922efc) + base + index * 4);
}

uint32_t* GetPerTeamValue(uint32_t* out, uint32_t entity, uint32_t index) {
    const uint32_t* value = PerTeamValue(entity, index);
    *out = value ? *value : 0;
    return out;
}

void SetPerTeamValue(uint32_t entity, uint32_t value, uint32_t index) {
    if (uint32_t* slot = PerTeamValue(entity, index)) *slot = value;
}

}  // namespace

uint32_t* XB_CDECL BroScr_GetPerTeamValue_d0(uint32_t* out, uint32_t entity) {
    return GetPerTeamValue(out, entity, 0);
}

uint32_t* XB_CDECL BroScr_GetPerTeamValue_d4(uint32_t* out, uint32_t entity) {
    return GetPerTeamValue(out, entity, 1);
}

uint32_t* XB_CDECL BroScr_GetPerTeamValue_d8(uint32_t* out, uint32_t entity) {
    return GetPerTeamValue(out, entity, 2);
}

uint32_t* XB_CDECL BroScr_GetPerTeamValue_dc(uint32_t* out, uint32_t entity) {
    return GetPerTeamValue(out, entity, 3);
}

void XB_CDECL BroScr_SetPerTeamValue_d0(uint32_t entity, uint32_t value) {
    SetPerTeamValue(entity, value, 0);
}

void XB_CDECL BroScr_SetPerTeamValue_d4(uint32_t entity, uint32_t value) {
    SetPerTeamValue(entity, value, 1);
}

void XB_CDECL BroScr_SetPerTeamValue_d8(uint32_t entity, uint32_t value) {
    SetPerTeamValue(entity, value, 2);
}

void XB_CDECL BroScr_SetPerTeamValue_dc(uint32_t entity, uint32_t value) {
    SetPerTeamValue(entity, value, 3);
}

}  // namespace game
