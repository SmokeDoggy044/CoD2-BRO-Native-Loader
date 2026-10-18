#pragma once

// The Quake 3-shaped side of the renderer (tr_*.c in Q3's source): the RE_* exports
// and the view setup around NGL. Names after their Q3 counterparts where the body
// matches (docs/naming.md, Renderer).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// A doubly linked list with its head and tail nodes folded into one header (the
// layout of AmigaOS's MinList): head points at the `tail` field when empty,
// tail_prev at `head`.
struct RFrameList {
    uint32_t count;
    void* head;
    void* tail;  // always null
    void* tail_prev;
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(RFrameList) == 16);
#endif

// 0x002352c0: out = a * b (4x4, row vectors); EAX = a, EDX = out, ECX = b.
void XB_REGS3 myGLMultMatrix(const float* a, float* out, const float* b);

// 0x00234cc0: the next scene starts after what the last one added (three scene
// lists; Q3: r_firstSceneDlight/Entity/Poly).
void XB_CDECL RE_ClearScene();
// 0x00234cf0: empties the two frame lists and resets every scene list count.
void XB_CDECL R_ClearFrameLists();
// 0x00235dc0: negative or zero = no far cull distance.
void XB_CDECL RE_SetFarCullDist(float distance);
// 0x00235e90: the larger of r_zfar and the far cull distance.
float XB_CDECL RE_GetFarCullDist();

}  // namespace game
