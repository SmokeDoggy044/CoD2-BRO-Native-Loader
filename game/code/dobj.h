#pragma once

// Skeletal model instances (DObj): a few models (a body plus attachments), whose
// bones count on from one model to the next.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct XBoneInfo {
    uint32_t unknown0;
    uint32_t name;    // +4, a script string
    int32_t parent;   // +8
};

// A model's bones, base poses (0x30 bytes each) and counts.
struct XModelParts {
    uint32_t pose_count;
    uint8_t* base_poses;  // +0x04
    uint8_t unknown08[0x10 - 0x08];
    uint32_t bone_count;  // +0x10
    XBoneInfo* bones;     // +0x14
};

struct XModelLod {
    uint8_t unknown0[8];
    XModelParts* parts;  // +8
};

struct XModel {
    uint8_t unknown00[0x20];
    XModelParts* default_parts;  // +0x20
    XModelLod* lods[7];   // +0x24, the first non-null one has the parts
    uint32_t flags;       // +0x40
};

struct DObj {
    uint8_t unknown00[0x2e];
    uint8_t model_count;  // +0x2e
    uint8_t unknown2f[0x38 - 0x2f];
    XModel* models[8];    // +0x38
    uint8_t attach_bones[8];  // +0x58: the bone each model hangs from (not the first)
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(XBoneInfo) == 12);
static_assert(offsetof(XModelParts, bones) == 0x14);
static_assert(offsetof(XModel, flags) == 0x40);
static_assert(offsetof(DObj, models) == 0x38);
static_assert(offsetof(DObj, attach_bones) == 0x58);
#endif

// 0x00237cd0: the base pose (0x30 bytes) of a bone; the first model's fallback
// parts' first pose past the last bone.
uint8_t* XB_STDCALL DObj_GetBoneBasePose(const DObj* obj, int32_t bone);
// 0x0023a2a0: a bone's parent; a model's first bone hangs from its attach bone.
// -1 past the last bone.
int32_t XB_STDCALL DObj_GetBoneParent(const DObj* obj, int32_t bone);
// 0x0023a440: the index of the bone named `name`, -1 if none; EAX = obj.
int32_t XB_REGS1 XB_STDCALL DObj_GetBoneIndex(const DObj* obj, uint32_t name);

// 0x0023b1e0: the bone count of the model's first LOD (the first non-null entry of
// lods, unbounded as in the original), 0 when it has no parts.
uint32_t XB_CDECL XModel_GetBoneCount(const XModel* model);

}  // namespace game
