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
    uint8_t unknown20[0x28 - 0x20];
    uint32_t mesh_count;    // +0x28
    uint8_t** meshes;       // +0x2c, one per bone (NGL meshes, may be null)
    int32_t root_count;     // +0x30, bones that hang from the model's attach bone
};

struct XModelLod {
    uint8_t unknown0[8];
    XModelParts* parts;  // +8
};

struct XModel {
    float mins[3];        // bounds
    uint32_t unknown0c;
    float maxs[3];        // +0x10
    uint32_t unknown1c;
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
    int32_t client_index;    // +0x34, -1 = none; else drawn only while that slot of the
                             // table at 0x0095f638 (+0x38) is set and its +0xdc is 0
    XModel* models[8];    // +0x38
    uint8_t attach_bones[8];  // +0x58: the bone each model hangs from (not the first)
    uint8_t matrix_offset;    // +0x60, the first model's; one byte per model follows
    uint8_t matrix_offsets_rest[7];
    const uint8_t* lod_limit; // +0x68, +4: the finest LOD allowed (DObj_SetMinLod)
    uint8_t unknown6c[0x88 - 0x6c];
    const uint8_t* owner;     // +0x88, an entity: its +0x2a4 flag 0x40000000 lifts lod_limit
    uint8_t unknown8c[0x90 - 0x8c];
    int32_t lod;              // +0x90, the distance LOD
    int32_t forced_lod;       // +0x94, -1 = none (DObj_SetForcedLod)
    int32_t min_lod;          // +0x98, -1 = none (DObj_SetMinLod)
    uint32_t cull_flags;      // +0x9c, bit 0: nothing of it was drawn this frame
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
static_assert(offsetof(XModel, maxs) == 0x10);
static_assert(offsetof(XModel, flags) == 0x40);
static_assert(offsetof(DObj, models) == 0x38);
static_assert(offsetof(DObj, attach_bones) == 0x58);
static_assert(offsetof(DObj, lod_limit) == 0x68);
static_assert(offsetof(DObj, owner) == 0x88);
static_assert(offsetof(DObj, cull_flags) == 0x9c);
#endif

// 0x00237cd0: the base pose (0x30 bytes) of a bone; the first model's fallback
// parts' first pose past the last bone.
uint8_t* XB_STDCALL DObj_GetBoneBasePose(const DObj* obj, int32_t bone);
// 0x0023a2a0: a bone's parent; a model's first bone hangs from its attach bone.
// -1 past the last bone.
int32_t XB_STDCALL DObj_GetBoneParent(const DObj* obj, int32_t bone);
// 0x0023a440: the index of the bone named `name`, -1 if none; EAX = obj.
int32_t XB_REGS1 XB_STDCALL DObj_GetBoneIndex(const DObj* obj, uint32_t name);

// 0x0023a560: whether any of the object's models (hidden ones too) has one of `flags`
// in XModel::flags; ECX = obj, EDI = flags: entered through DObj_AnyModelHasFlags_entry.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL DObj_AnyModelHasFlags(const DObj* obj, uint32_t flags);
extern "C" void DObj_AnyModelHasFlags_entry(void);

// 0x0023be90: the bounds of the object's first model; without one the original copies
// uninitialized stack slots (zeros here and in --compare test processes). EDX = mins,
// ECX = maxs, the object on the stack (EAX unused).
void XB_REGS3 DObj_GetBounds(uint32_t unused_eax, float* mins, float* maxs, const DObj* obj);

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
