#pragma once

// Skeletal model instances (DObj): a few models (a body plus attachments), whose
// bones count on from one model to the next.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct XBoneInfo {
    const char* text; // the bone name (XModelParts_FindBone)
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
    // +0x24: 5 LODs, the first non-null one has the parts. lods[5] (+0x38) is the collision part
    // count and lods[6] (+0x3c) the part list (XModel_TraceParts); kept in the array because the
    // title's first-LOD walks have no bound.
    XModelLod* lods[7];
    uint32_t flags;       // +0x40
};

struct DObj {
    uint8_t unknown00[0x20];  // +0: each model's anim tree (DObj_UpdateSkeletons)
    uint8_t* bone_matrices;  // +0x20, 0x40 bytes each, from +0x30 + matrix_offset * 0x40
    uint8_t unknown24[0x2e - 0x24];
    uint8_t model_count;  // +0x2e
    uint8_t bone_count;  // +0x2f, over all models
    uint32_t hidden_models;  // +0x30, a bit per model
    int32_t client_index;    // +0x34, -1 = none; else drawn only while that slot of the
                             // table at 0x0095f638 (+0x38) is set and its +0xdc is 0;
                             // also the memory context of the clip states
    XModel* models[8];    // +0x38
    uint8_t attach_bones[8];  // +0x58: the bone each model hangs from (not the first)
    uint8_t matrix_offset;    // +0x60, the first model's; one byte per model follows
    uint8_t matrix_offsets_rest[7];
    const uint8_t* lod_limit; // +0x68: the first model's NAL clip state (one per model, 8);
                              // its +4 (group range) is the finest LOD allowed (DObj_SetMinLod)
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

// 0x00014260: the parts of an LOD of a model; lod < 0 = the first LOD it has. EAX =
// lod, EDX = model.
XModelParts* XB_REGS2 XModel_GetLodParts(int32_t lod, const XModel* model);
// 0x00014610 / 0x00029490: forced_lod / min_lod = the first model's LOD `lod` when it
// has one, else the nearest coarser one, else the nearest finer one; -1 when none
// (from 5 on nothing is looked up). EAX = lod, EDX = obj.
void XB_REGS2 DObj_SetForcedLod(int32_t lod, DObj* obj);
void XB_REGS2 DObj_SetMinLod(int32_t lod, DObj* obj);
// 0x00029520: the coarsest LOD (4 down to 0) the first model has; without one the
// debug break 0x00042460, then -1. EDX = obj (EAX unused).
int32_t XB_REGS2 DObj_GetCoarsestLod(uint32_t unused_eax, const DObj* obj);
// 0x0029c790 (src EAX, dst ECX): a bone matrix (four rows of x y z, 16 bytes apart)
// into four 16-byte rows {x, y, z, x}.
void XB_REGS3 XB_CDECL BoneMatrix_Load(const uint32_t* src, uint32_t unused_edx, uint32_t* dst);
// 0x0029c9e0 (dst EAX, src ECX): x y z of four 16-byte rows into a bone matrix (the
// fourth dwords stay); returns dst.
uint32_t* XB_REGS3 XB_CDECL BoneMatrix_Store(uint32_t* dst, uint32_t unused_edx, const uint32_t* src);
// 0x0023df00: a model's base skeleton into `matrices` (0x40 bytes per bone): each bone's
// base pose (0x30 bytes: rotation rows, translation) times its parent's matrix; root
// bones hang from `attach` (a bone matrix) or, without one, take the pose alone.
// Returns the bone count.
int32_t XB_CDECL XModel_CalcSkeleton(const XModel* model, uint8_t* matrices, const uint8_t* attach);
// 0x0023db60: the same for the descendants of bone `parent` (recursive, children in
// index order after it); returns the bone count.
int32_t XB_CDECL XModel_CalcBoneChildren(const XModel* model, uint8_t* matrices, int32_t parent);
// 0x00240f00: every model's base skeleton (XModel_CalcSkeleton) into the object's bone
// matrices (+0x20, from +0x30 + matrix offset * 0x40), each model hanging from its
// attach bone's matrix (none for 0xff).
void XB_CDECL DObj_CalcBasePose(const DObj* obj);
// 0x00240d70: the skeleton again after bone `bone` (counted over all models) changed:
// its model recomputes the bone's descendants (XModel_CalcBoneChildren, with the bone
// index in that model), the models after it their whole skeletons. A bone past the
// last model counts as model 0's (as in the original).
void XB_CDECL DObj_CalcAnimatedSkeleton(const DObj* obj, int32_t bone);

// 0x0001d0f0: the distance LOD (lod) of an object, taken as given when it is the last
// of the first model's LOD table at parts +0x38 (whose first word is 0x00903fdc), else
// the first model's LOD `lod`, the nearest finer one or the nearest coarser one (-1
// when none). Nothing without a first model. EDI = obj, ESI = lod: entered through
// DObj_SetLod_entry (from G_UpdateEntityLOD).
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_SetLod(DObj* obj, int32_t lod);
extern "C" void DObj_SetLod_entry(void);
// The index of the bone named `name` (case-insensitive) in `parts`, -1 if none or
// `name` is null. EBX = name, ESI = parts: entered through XModelParts_FindBone_entry.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL XModelParts_FindBone(const char* name, const XModelParts* parts);
extern "C" void XModelParts_FindBone_entry(void);
// The DObj-wide index of the bone named `name`: the first model that has it, counting
// the bones of the models before it. -1 if none has it.
int32_t XB_STDCALL DObj_FindBoneIndex2(const DObj* obj, const char* name);

// 0x0023aea0 (obj in EAX, the tag's name hash in ESI): DObj_GetBoneIndex, -1 for hash 0;
// a missing "tag_origin" is bone 0 (its hash cached in 0x0098b83c, guard bit 0x0098b840).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL DObj_GetTagIndex(const DObj* obj, uint32_t name);
extern "C" void DObj_GetTagIndex_entry(void);

// 0x00240c00: allocates the bone matrices (header 0x30 bytes cleared, 0x40 per bone;
// the client's memory context 0x0095f638 +0x38 first, else the heap) and computes the
// base pose of every model (as DObj_CalcBasePose).
void XB_CDECL DObj_CreateSkeleton(DObj* obj);
// 0x00241ea0 (parts in ESI): when the first bone has no script name yet, every bone's
// name text is hashed (ScrIface_HashStringNoCase) and added to the name map.
extern "C" PLATFORM_HIDDEN void XB_CDECL XModel_ResolveBoneNames(XModelParts* parts);
extern "C" void XModel_ResolveBoneNames_entry(void);

// 0x002af200 (tree in ESI): unlinks an animation tree from the live list (0x008dda40) and
// frees it from its memory context (+0x10, -1 = the zone heap).
extern "C" PLATFORM_HIDDEN void XB_CDECL AnimTree_Free(uint32_t* tree);
extern "C" void AnimTree_Free_entry(void);
// 0x00240ce0 (obj in EDI, flag on the stack, cdecl): frees the bone matrices (+0x20) from
// the client's memory context (+0x34); with free_trees each model's animation tree (the
// first model_count words) cleared and freed; the trees are forgotten and +0x2c cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_Free(DObj* obj, uint32_t free_trees);
extern "C" void DObj_Free_entry(void);

// 0x000291d0 (parts in EDX, name in EBX): the index of the bone with that script name, -1
// when none.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL XModelParts_FindBoneByName(const XModelParts* parts, uint32_t name);
extern "C" void XModelParts_FindBoneByName_entry(void);
// One model of a DObj_Create call (16 bytes).
struct DObjModelInfo {
    XModel* model;
    const uint8_t* bone;  // a script string (text at +6): the earlier model's bone it hangs from
    uint32_t hidden;
    uint32_t tree;        // the model's animation tree, 0 = keep
};
// 0x00245ff0 (tree0 EAX, flags CX, models EDX; count and obj on the stack, cdecl): fills a
// DObj from up to `count` models (stops at a null one): attach bones by name, matrix
// offsets, hidden bits; at most 0x7f bones (Com_Error); then DObj_CreateSkeleton and
// DObj_UpdateSkeletons, and the distance LOD.
// Entered through DObj_Create_entry (entry = the original's entry ESP).
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_Create(uint32_t entry, uint32_t tree0, uint32_t flags,
                                                     const DObjModelInfo* infos, uint32_t count, DObj* obj);
extern "C" void DObj_Create_entry(void);

// 0x002b7ba0: the skeleton's root motion {quaternion, position} (32 bytes) from the clip
// state at +0x68 when the first model's skeleton and the state's clip are generic NAL
// clips; identity otherwise.
void XB_CDECL DObj_GetAnimRootTransform(uint32_t* out, const DObj* obj);

// 0x00242540: the model's bones (first LOD's count, 0x40 bytes each) moved under the
// parent bone's matrix: bone = bone * parent (rows, packed SSE).
void XB_CDECL XModel_TransformBones(const XModel* model, uint8_t* bones, const uint8_t* parent);

// 0x002df880: every model's skeleton: animated ones through their anim tree (+0, one per
// model) into their clip state (+0x68, one per model) and NAL_GenericAnim_EvaluatePose (moved under the attach bone),
// others XModel_CalcSkeleton. An owner entity with flag 0x1000000 (+0x2a4) then takes the
// root motion: into its tag link's local matrix (+0x3cc), straight from the anim tree,
// accumulated in its record (+0x3d4), or applied to its axis and origin.
void XB_CDECL DObj_UpdateSkeletons(DObj* obj);

// 0x0023cea0: frees a model's anim states (+0x68, eight) inside its memory context, releases
// its handle (+0x8c) and poisons +0x88 (0xafafafaf).
void XB_STDCALL XModel_Free(uint8_t* model);

// 0x002be7c0 (model in EBX; no callers by CALL): XModel_Free's anim-state part over the model's
// count (+0x2e): each state destroyed and freed, its slot cleared, inside the memory context.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL XModel_FreeAnimStates(uint8_t* model);
extern "C" void XModel_FreeAnimStates_entry(void);
// 0x002c9b30 (object in EDI): debug text of each model's anim tree (AnimTree_DebugDraw_r with
// one line counter) after a DebugDraw_Flush(0); nothing when the first model has no tree.
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_DebugDrawAnimTrees(uint8_t* const* obj);
extern "C" void DObj_DebugDrawAnimTrees_entry(void);

// 0x0023d7b0 (matrix in EAX; no callers by CALL): the bounds of every model of `obj` (each
// model's box +0/+0x10 through the matrix, Bounds_AddPoints8 0x0023c400) into mins / maxs;
// +-FLT_MAX for an object without models.
void XB_REGS1 XB_CDECL DObj_GetTransformedBounds(const float* matrix, const DObj* obj, float* mins, float* maxs);
// 0x002d7e30 (obj in EAX, dir out EBX, delta out EDI): the root motion of the first model's
// generic NAL clip (as DObj_GetAnimRootTransform): dir = the first rotation row's x, y,
// delta = the position; the identity without one.
extern "C" PLATFORM_HIDDEN void XB_CDECL DObj_GetRootAnimDelta(const DObj* obj, float* dir, float* delta);
extern "C" void DObj_GetRootAnimDelta_entry(void);

// 0x002d7310: XModel_CalcBoneChildren for an animated skeleton (from G_DObjSetBoneRotation):
// each child of bone `parent` (in index order after it) gets its own full 4x4 matrix in
// `matrices` (0x40 bytes per bone, all four lanes) premultiplied by the parent's, is
// re-orthonormalized (Matrix_Orthonormalize 0x002cede0) and recursed into.
void XB_CDECL XModel_CalcBoneChildrenOrtho(const XModel* model, uint8_t* matrices, int32_t parent);

// 0x0023d070: the segment start-end (in the model's space) against the model's collision
// parts (count +0x38, pointer list +0x3c; each: box +0/+0x10, plane count +0x20 and
// pointer list +0x24, bone +0x28, contents +0x2c, surface flags +0x30). Parts whose
// contents match `content_mask` and whose box (SegmentMissesBounds) the segment reaches
// in their bone's space (`skeleton`, 0x40 bytes per bone) test each triangle plane
// (normal, dist, then two edge rows with offsets; inside within -0.005..1.005): a crossing
// from the front pulled back by 0.125 and before trace +0x20 becomes the hit (fraction
// +0x20, normal +0x10, +0x24 flags, +0x28 contents, +0x3c/+0x3d cleared). Returns the hit's
// bone with the normal rotated out of its space, -1 for no hit.
int32_t XB_CDECL XModel_TraceParts(const uint8_t* model, uint8_t* trace, const uint8_t* skeleton, const float* start,
                                   const float* end, uint32_t content_mask);

}  // namespace game
