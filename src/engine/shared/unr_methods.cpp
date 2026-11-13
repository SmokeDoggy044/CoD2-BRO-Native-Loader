#include "unr_methods.h"

#include "nal_anim.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T, typename P>
T& word(P* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

}  // namespace

// --- Shared constant slots ---

uint32_t XB_STDCALL EmptyVFunc_ReturnZero4(uint32_t) { return 0; }

uint32_t XB_STDCALL EmptyVFunc_ReturnOne12(uint32_t, uint32_t, uint32_t) { return 1; }

// --- UI ---

uint32_t XB_THISCALL UIElement_GetOwnerField0A0(uint8_t* self) {
    const uint8_t* owner = word<uint8_t*>(self, 0x20);
    return word<uint32_t>(word<uint8_t*>(owner, 0), 0xa0);
}

// 0x00011c50: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot8,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0x20(%eax)")

// 0x00011dd0: tail jump to the wrapped item's virtual: not compared
PLATFORM_ASM_ENTRY(UIMenuItem_InnerSlot50,
                   "movl 0x10(%ecx), %ecx\n"
                   "movl (%ecx), %eax\n"
                   "jmp *0xc8(%eax)")

// --- Empty callbacks ---

void XB_CDECL Sys_DebugBreak() {
    if (title_global<uint8_t>(0x00901aec)) platform_debug_break();
}

void XB_CDECL nglProfileShaders() {}

void XB_CDECL Ent_ControllerNop(uint8_t*, uint32_t*) {}

void XB_CDECL PathNodeField_SetReadOnly(uint8_t*, int32_t) {}

// --- NAL generic anim factories ---

uint8_t* XB_THISCALL NAL_GenericAnim_NewState(uint8_t* self) {
    auto* state = static_cast<AnimState*>(Mem_AlignedAlloc(0x10, 4, 0));
    if (!state) return nullptr;
    return reinterpret_cast<uint8_t*>(NAL_AnimState_Init(state, self, 0));
}

uint8_t* XB_THISCALL NAL_GenericAnim_NewTrack(uint8_t* self, uint8_t* skeleton) {
    if (!skeleton) skeleton = word<uint8_t*>(self, 0x30);
    auto* track = static_cast<uint8_t*>(Mem_AlignedAlloc(0x30, 4, 0));
    if (!track) return nullptr;
    return NAL_AnimTrack_Construct(track, self, skeleton);
}

// --- Engine helpers level code calls ---

uint32_t* XB_THISCALL LevelParams_InitDefaults(uint32_t* self) {
    const uint32_t undefined_int = title_global<uint32_t>(0x00922ec0);    // g_scrUndefinedInt
    const uint32_t undefined_float = title_global<uint32_t>(0x00922eac);  // g_scrUndefinedFloat
    self[0] = undefined_int;
    self[1] = undefined_int;
    self[2] = undefined_float;
    self[3] = undefined_float;
    self[4] = undefined_float;
    self[5] = undefined_float;
    self[3] = title_global<uint32_t>(0x0083ece8);  // 5.0f
    self[0] = 3;
    self[1] = 13;
    self[2] = title_global<uint32_t>(0x0083eb24);  // 0.5f
    self[4] = title_global<uint32_t>(0x0083ebf8);  // 0.1f
    self[5] = title_global<uint32_t>(0x0083eb24);  // 0.5f
    return self;
}

uint32_t* XB_THISCALL ScrTwoMaps_Ctor(uint32_t* self) {
    // The maps' comparator/allocator arguments: both the address of one (never written) stack byte.
    using MapCtor = void XB_THISCALL(uint32_t*, uint8_t*, uint8_t*);
    uint8_t empty;
    self[0] = 0;
    self[1] = 0;
    self[2] = title_global<uint32_t>(0x00922ec0);  // g_scrUndefinedInt
    title_function<MapCtor>(0x0078c6a0)(self + 3, &empty, &empty);
    title_function<MapCtor>(0x0078c720)(self + 13, &empty, &empty);
    return self;
}

}  // namespace game
