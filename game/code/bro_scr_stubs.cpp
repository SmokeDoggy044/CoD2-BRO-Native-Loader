#include "bro_scr_stubs.h"

#include "bro_scr_common.h"
#include "title_data.h"

namespace game {

void XB_CDECL BroScr_Stub_Ret() {}

uint32_t XB_CDECL BroScr_Stub_ReleaseArg1(ScriptString string) {
    return scr_release(&string);  // the argument slot itself, as the original's lea ecx,[esp+4]
}

uint32_t XB_CDECL BroScr_Stub_ReleaseArg2(uint32_t, ScriptString string) {
    return scr_release(&string);
}

uint32_t XB_CDECL Bro_anim_exit_run() {
    return 0;
}

uint32_t XB_CDECL BroScr_SetScriptGlobal_922e8c(uint32_t value) {
    title_global<uint32_t>(0x00922e8c) = value;
    return value;
}

uint32_t* XB_CDECL BroScr_GetPlayer(uint32_t* out) {
    *out = title_global<uint32_t>(0x009236dc);
    return out;
}

uint32_t* XB_CDECL BroScr_GetDefaultString(uint32_t* out, uint32_t, ScriptString string) {
    *out = title_global<uint32_t>(0x00922ea8);
    scr_release(&string);
    return out;
}

void XB_CDECL BroScr_DeleteEntArg2(uint32_t, uint32_t entity) {
    scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(entity);  // GScr_Delete
}

void XB_CDECL BroScr_EndOnDeath(uint32_t entity) {
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(entity, 0x06730ae8);  // Scr_EndOn
}

uint32_t XB_CDECL BroScr_AnimStateSetDefaults() {
    // The object pointer is re-read before each store.
    *reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922efc) + 0xec) = 8;
    *reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922efc) + 0x5c) = 1;
    *reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922efc) + 0xc4) = 1;
    *reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922efc) + 0x12c) = 1;
    return 1;
}

uint32_t XB_CDECL BroScr_GetStateBlock1To7(uint32_t, uint32_t index) {
    switch (index) {
    case 1: return 0x00923188;
    case 2: return 0x00923220;
    case 3: return 0x009235b0;
    case 4: return 0x009232b8;
    case 5: return 0x00923350;
    case 6: return 0x00923480;
    case 7: return 0x009233e8;
    default: return 0;
    }
}

uint32_t XB_CDECL BroScr_GetStateBlock8To14(uint32_t index) {
    switch (index) {
    case 8: return 0x00923648;
    case 9: return 0x009230f0;
    case 10: return 0x00922fc0;
    case 11: return 0x00923518;
    case 12: return 0x00922f08;
    case 13: return 0x00923058;
    case 14: return 0x00923708;
    default: return 0;
    }
}

uint32_t* XB_CDECL BroScr_ReturnQuarter(uint32_t* out) {
    *out = title_global<uint32_t>(0x0083ec24);  // 0.25f, a word copy (movss)
    return out;
}

}  // namespace game
