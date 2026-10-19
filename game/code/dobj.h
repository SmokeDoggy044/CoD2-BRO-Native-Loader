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

// A bone's hit box, in the bone's space.
struct XModelHitBox {
    float mins[3];
    float maxs[3];      // +0x0c
    float center[3];    // +0x18
    float radius_sq;    // +0x24, 0 = no box
};

// A model's bones, base poses (0x30 bytes each) and counts.
struct XModelParts {
    uint32_t pose_count;
    uint8_t* base_poses;    // +0x04
    uint32_t hit_box_count; // +0x08
    XModelHitBox* hit_boxes; // +0x0c
    uint32_t bone_count;    // +0x10
    XBoneInfo* bones;       // +0x14
    uint32_t class_count;   // +0x18
    uint8_t* classes;       // +0x1c, each bone's hit class (1 = its parent's)
    uint8_t unknown20[0x30 - 0x20];
    int32_t root_count;     // +0x30, bones that hang from the model's attach bone
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
    uint8_t unknown00[0x20];
    uint8_t* bone_matrices;  // +0x20, 0x40 bytes each, from +0x30 + matrix_offset * 0x40
    uint8_t unknown24[0x2e - 0x24];
    uint8_t model_count;  // +0x2e
    uint8_t unknown2f;
    uint32_t hidden_models;  // +0x30, a bit per model
    uint8_t unknown34[0x38 - 0x34];
    XModel* models[8];    // +0x38
    uint8_t attach_bones[8];  // +0x58: the bone each model hangs from (not the first)
    uint8_t matrix_offset;    // +0x60
};

// What DObj_TraceBoneBoxes found.
struct DObjTrace {
    float fraction;      // in: the trace's current fraction
    uint32_t unknown04;  // cleared
    float normal[3];     // +0x08
    uint32_t bone_name;  // +0x14, a script string
    uint32_t hit_class;  // +0x18
    uint8_t start_solid; // +0x1c
    uint8_t all_solid;   // +0x1d
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(XBoneInfo) == 12);
static_assert(sizeof(XModelHitBox) == 0x28);
static_assert(offsetof(XModelParts, bones) == 0x14);
static_assert(offsetof(XModelParts, root_count) == 0x30);
static_assert(offsetof(DObj, hidden_models) == 0x30);
static_assert(offsetof(DObj, matrix_offset) == 0x60);
static_assert(offsetof(DObjTrace, all_solid) == 0x1d);
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

// 0x0023a590: traces the segment start..end, widened by radius, against the hit
// boxes of the object's visible models (bone matrices: 4x4, row vectors). Classes
// rank by priority[class] (from 2 up); a higher one wins whatever its fraction.
// Called by the entity clip test 0x002e79c0.
void XB_CDECL DObj_TraceBoneBoxes(const DObj* obj, const float* start, const float* end,
                                  const uint8_t* priority, DObjTrace* result, float radius);

// 0x0023b1e0: the bone count of the model's first LOD (the first non-null entry of
// lods, unbounded as in the original), 0 when it has no parts.
uint32_t XB_CDECL XModel_GetBoneCount(const XModel* model);

}  // namespace game
