#include "dobj.h"

#include <cstring>

#include "compiler.h"
#include "lanes.h"
#include "nal_anim.h"
#include "nal_pose.h"
#include "ngl_debug.h"
#include "stream.h"
#include "sv.h"
#include "nal_math.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

const XModelParts* model_parts(const XModel* model) {
    // No end check, as in the original: a model always has a level of detail.
    const XModelLod* const* lod = model->lods;
    while (!*lod) ++lod;
    return (*lod)->parts;
}

// lods[index] without bounds, as in the original (its loops stop at 5; callers pass
// 0..4).
const XModelLod* lod_entry(const XModel* model, int32_t index) {
    return *reinterpret_cast<XModelLod* const*>(reinterpret_cast<const uint8_t*>(model) + 0x24 + index * 4);
}

// DObj_SetForcedLod and DObj_SetMinLod: the LOD, else coarser, else finer.
void set_nearest_lod(const DObj* obj, int32_t lod, int32_t& out) {
    out = -1;
    if (lod < 5 && lod_entry(obj->models[0], lod)) {
        out = lod;
        return;
    }
    for (int32_t i = lod + 1; i < 5; ++i) {
        if (lod_entry(obj->models[0], i)) {
            out = i;
            return;
        }
    }
    for (int32_t i = lod - 1; i >= 0; --i) {
        if (i < 5 && lod_entry(obj->models[0], i)) {
            out = i;
            return;
        }
    }
}

}  // namespace

PLATFORM_ASM_ENTRY(DObj_AnyModelHasFlags_entry,
                   "pushl %edi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(DObj_AnyModelHasFlags) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL DObj_AnyModelHasFlags(const DObj* obj, uint32_t flags) {
    for (int i = 0; i < obj->model_count; ++i)
        if (obj->models[i]->flags & flags) return 1;
    return 0;
}

uint8_t* XB_STDCALL DObj_GetBoneBasePose(const DObj* obj, int32_t bone) {
    int32_t first = 0;
    for (int i = 0; i < obj->model_count; ++i) {
        const XModelParts* parts = model_parts(obj->models[i]);
        if (bone - first < static_cast<int32_t>(parts->bone_count)) {
            uint32_t index = static_cast<uint32_t>(bone - first);
            if (index >= parts->pose_count) index = 0;
            return parts->base_poses + index * 0x30;
        }
        first += parts->bone_count;
    }
    return obj->models[0]->default_parts->base_poses;
}

int32_t XB_STDCALL DObj_GetBoneParent(const DObj* obj, int32_t bone) {
    int32_t first = 0;
    for (int i = 0; i < obj->model_count; ++i) {
        const XModelParts* parts = model_parts(obj->models[i]);
        if (bone - first < static_cast<int32_t>(parts->bone_count)) {
            if (i > 0 && bone == first) return obj->attach_bones[i];
            uint32_t index = static_cast<uint32_t>(bone - first);
            if (index >= parts->bone_count) index = 0;
            return parts->bones[index].parent;
        }
        first += parts->bone_count;
    }
    return -1;
}

int32_t XB_REGS1 XB_STDCALL DObj_GetBoneIndex(const DObj* obj, uint32_t name) {
    int32_t first = 0;
    for (int i = 0; i < obj->model_count; ++i) {
        const XModelParts* parts = model_parts(obj->models[i]);
        for (uint32_t j = 0; j < parts->bone_count; ++j) {
            if (parts->bones[j].name != name) continue;
            if (static_cast<int32_t>(j) >= 0) return first + static_cast<int32_t>(j);
            break;
        }
        const XModelParts* counted = model_parts(obj->models[i]);
        first += counted ? counted->bone_count : 0;
    }
    return -1;
}

void XB_CDECL DObj_TraceBoneBoxes(const DObj* obj, const float* start, const float* end,
                                  const uint8_t* priority, DObjTrace* result, float radius) {
    const float dir_z = end[2] - start[2];
    const float dir_y = end[1] - start[1];
    const float dir_x = end[0] - start[0];
    const float inv_len_sq = 1.0f / ((dir_z * dir_z + dir_y * dir_y) + dir_x * dir_x);
    const float* matrix = obj->bone_matrices
                              ? reinterpret_cast<const float*>(obj->bone_matrices + obj->matrix_offset * 0x40 + 0x30)
                              : nullptr;
    result->unknown04 = 0;
    result->start_solid = 0;
    result->all_solid = 0;
    result->bone_name = 0;
    result->hit_class = 0;
    result->normal[2] = 0.0f;
    result->normal[1] = 0.0f;
    result->normal[0] = 0.0f;
    uint32_t best_priority = 2;
    int32_t bone = 0;  // over all models
    // The entering plane: kept across bones and models, as in the original.
    int32_t hit_axis = -1;
    float hit_sign = 0.0f;
    uint16_t bone_class[128];
    for (int model = 0; model < obj->model_count; ++model) {
        const XModelParts* parts = model_parts(obj->models[model]);
        const int32_t count = parts ? static_cast<int32_t>(parts->bone_count) : 0;
        const uint32_t hidden = (1u << model) & obj->hidden_models;
        for (int32_t i = 0; i < count; ++i, ++bone, matrix += 16) {
            uint16_t cls = parts->classes[static_cast<uint32_t>(i) < parts->class_count ? i : 0];
            uint32_t prio = priority[cls];
            if (prio == 1) {
                if (i < parts->root_count) {
                    const uint8_t attach = obj->attach_bones[model];
                    cls = attach == 0xff ? 0 : bone_class[attach];
                } else {
                    uint32_t j = static_cast<uint32_t>(i - parts->root_count);
                    if (j >= parts->bone_count) j = 0;
                    cls = bone_class[bone - parts->bones[j].parent];
                }
                prio = priority[cls];
            }
            bone_class[bone] = cls;
            if (hidden) continue;
            const XModelHitBox& box =
                parts->hit_boxes[static_cast<uint32_t>(i) < parts->hit_box_count ? i : 0];
            if (box.radius_sq == 0.0f) continue;
            if (best_priority > prio) continue;

            // The box's sphere against the segment.
            const float* m = matrix;
            const float cx = box.center[0];
            const float cy = box.center[1];
            const float cz = box.center[2];
            const float wx = ((m[0] * cx + m[8] * cz) + m[4] * cy) + m[12];
            const float wy = ((m[1] * cx + m[9] * cz) + m[5] * cy) + m[13];
            const float wz = ((m[2] * cx + cz * m[10]) + cy * m[6]) + m[14];
            const float dz = start[2] - wz;
            const float dy = start[1] - wy;
            const float dx = start[0] - wx;
            const float t = 0.0f - ((dz * dir_z + dy * dir_y) + dx * dir_x) * inv_len_sq;
            float dist_sq;
            if (!(t < 1.0f)) {
                const float ex = end[0] - wx;
                const float ey = end[1] - wy;
                const float ez = end[2] - wz;
                dist_sq = (ez * ez + ey * ey) + ex * ex;
            } else if (t <= 0.0f) {
                dist_sq = (dz * dz + dy * dy) + dx * dx;
            } else {
                const float pz = t * dir_z + dz;
                const float py = t * dir_y + dy;
                const float px = t * dir_x + dx;
                dist_sq = (pz * pz + py * py) + px * px;
            }
            const float depth = (radius * radius + box.radius_sq) - dist_sq;
            if (depth <= 0.0f) continue;
            if (best_priority == prio && platform_x87_sub_sqrt_ge(t, depth, inv_len_sq, result->fraction)) continue;

            // Into the bone's space: (point - origin) times the transposed rows.
            float local[6];
            {
                const float s0 = start[0] - m[12];
                const float s1 = start[1] - m[13];
                const float s2 = start[2] - m[14];
                local[0] = (s2 * m[2] + m[1] * s1) + m[0] * s0;
                local[1] = (s0 * m[4] + s2 * m[6]) + m[5] * s1;
                local[2] = (s0 * m[8] + s2 * m[10]) + m[9] * s1;
                const float e2 = end[2] - m[14];
                const float e0 = end[0] - m[12];
                const float e1 = end[1] - m[13];
                local[3] = (e2 * m[2] + m[1] * e1) + m[0] * e0;
                local[4] = (e0 * m[4] + e2 * m[6]) + m[5] * e1;
                local[5] = (e0 * m[8] + e2 * m[10]) + m[9] * e1;
            }
            // Slab test against the box grown by radius: mins with sign -1, then maxs.
            float enter = 0.0f;
            float leave = result->fraction;
            bool starts_inside = true;
            bool ends_inside = true;
            bool missed = false;
            const float* planes = box.mins;
            for (float sign = -1.0f;; sign = 1.0f, planes = box.maxs) {
                const float grow = sign * radius;
                for (int axis = 0; axis < 3; ++axis) {
                    const float plane = planes[axis] + grow;
                    const float d0 = (local[axis] - plane) * sign;
                    const float d1 = (local[axis + 3] - plane) * sign;
                    if (d0 > 0.0f) {
                        if (d1 > 0.0f) {
                            missed = true;
                            break;
                        }
                        starts_inside = false;
                        const float span = d0 - d1;
                        if (d0 > span * enter) {
                            enter = d0 / span;
                            if (enter >= leave) {
                                missed = true;
                                break;
                            }
                            hit_sign = sign;
                            hit_axis = axis;
                        }
                    } else if (d1 > 0.0f) {
                        ends_inside = false;
                        const float span = d0 - d1;
                        if (d0 > span * leave) {
                            leave = d0 / span;
                            if (enter >= leave) {
                                missed = true;
                                break;
                            }
                        }
                    }
                }
                if (missed || sign == 1.0f) break;
            }
            if (missed) continue;

            if (starts_inside) {
                result->start_solid = 1;
                if (!ends_inside) continue;
                result->all_solid = 1;
                result->fraction = 0.0f;
                result->bone_name = parts->bones[static_cast<uint32_t>(i) < parts->bone_count ? i : 0].name;
                result->hit_class = cls;
                result->normal[2] = 0.0f;
                result->normal[1] = 0.0f;
                result->normal[0] = 0.0f;
                return;
            }
            if (best_priority == prio) {
                if (enter >= result->fraction) continue;
            } else {
                best_priority = prio;
            }
            result->fraction = enter;
            result->bone_name = parts->bones[static_cast<uint32_t>(i) < parts->bone_count ? i : 0].name;
            result->hit_class = cls;
            // The entering plane's axis row of the bone matrix (from an earlier bone
            // when this one never moved enter, as in the original).
            const float* axis_row = m + hit_axis * 4;
            result->normal[0] = axis_row[0] * hit_sign;
            result->normal[1] = axis_row[1] * hit_sign;
            result->normal[2] = axis_row[2] * hit_sign;
        }
    }
}

uint32_t XB_CDECL XModel_GetBoneCount(const XModel* model) {
    int i = 0;
    while (!model->lods[i]) ++i;
    const XModelParts* parts = model->lods[i]->parts;
    return parts ? parts->bone_count : 0;
}

void XB_REGS3 DObj_GetBounds(uint32_t, float* mins, float* maxs, const DObj* obj) {
    const XModel* model = obj->models[0];
    for (int i = 0; i < 3; ++i) {
        mins[i] = model ? model->mins[i] : 0.0f;
        maxs[i] = model ? model->maxs[i] : 0.0f;
    }
}

XModelParts* XB_REGS2 XModel_GetLodParts(int32_t lod, const XModel* model) {
    if (lod < 0) {
        lod = 0;
        while (!model->lods[lod]) ++lod;
    }
    return model->lods[lod]->parts;
}

void XB_REGS2 DObj_SetForcedLod(int32_t lod, DObj* obj) { set_nearest_lod(obj, lod, obj->forced_lod); }

void XB_REGS2 DObj_SetMinLod(int32_t lod, DObj* obj) { set_nearest_lod(obj, lod, obj->min_lod); }

int32_t XB_REGS2 DObj_GetCoarsestLod(uint32_t, const DObj* obj) {
    for (int32_t i = 4; i >= 0; --i)
        if (obj->models[0]->lods[i]) return i;
    title_function<void XB_CDECL()>(0x00042460)();  // breaks into a debugger when one is attached
    return -1;
}

namespace {

uint8_t* bone_matrix(const DObj* obj, uint32_t index) { return obj->bone_matrices + index * 0x40 + 0x30; }

uint8_t* attach_matrix(const DObj* obj, int model) {
    const uint8_t attach = obj->attach_bones[model];
    return attach == 0xff ? nullptr : bone_matrix(obj, attach);
}

const uint8_t& matrix_offset(const DObj* obj, int model) { return (&obj->matrix_offset)[model]; }

}  // namespace

void XB_CDECL DObj_CalcBasePose(const DObj* obj) {
    for (int m = 0; m < obj->model_count; ++m) {
        uint8_t* parent = attach_matrix(obj, m);
        XModel_CalcSkeleton(obj->models[m], bone_matrix(obj, matrix_offset(obj, m)), parent);
    }
}

void XB_CDECL DObj_CalcAnimatedSkeleton(const DObj* obj, int32_t bone) {
    const int count = obj->model_count;
    int found = 0;
    int32_t first = 0;
    for (int m = 0; m < count; ++m) {
        const XModelParts* parts = model_parts(obj->models[m]);
        const int32_t bones = parts ? static_cast<int32_t>(parts->bone_count) : 0;
        if (bone < first + bones) {
            found = m;
            break;
        }
        first += bones;
    }
    for (int m = found; m < obj->model_count; ++m) {
        uint8_t* parent = attach_matrix(obj, m);
        uint8_t* matrices = bone_matrix(obj, matrix_offset(obj, m));
        if (m == found)
            XModel_CalcBoneChildren(obj->models[m], matrices, bone - first);
        else
            XModel_CalcSkeleton(obj->models[m], matrices, parent);
    }
}

void XB_REGS3 XB_CDECL BoneMatrix_Load(const uint32_t* src, uint32_t, uint32_t* dst) {
    for (int row = 0; row < 4; ++row) {
        dst[row * 4 + 0] = src[row * 4 + 0];
        dst[row * 4 + 1] = src[row * 4 + 1];
        dst[row * 4 + 2] = src[row * 4 + 2];
        dst[row * 4 + 3] = src[row * 4 + 0];
    }
}

uint32_t* XB_REGS3 XB_CDECL BoneMatrix_Store(uint32_t* dst, uint32_t, const uint32_t* src) {
    for (int row = 0; row < 4; ++row)
        for (int i = 0; i < 3; ++i) dst[row * 4 + i] = src[row * 4 + i];
    return dst;
}

namespace {

// A bone's matrix from its parent's (rows as BoneMatrix_Load reads them) and its base
// pose (rotation rows 0-2, translation 9-11), in the original's operation order. The
// packed code's fourth lanes repeat the first (BoneMatrix_Load), so three are enough.
void compose_bone(const uint32_t* parent_bits, const float* pose, float* out) {
    float parent[16];
    std::memcpy(parent, parent_bits, sizeof parent);
    for (int row = 0; row < 3; ++row) {
        const float a = pose[row * 3], b = pose[row * 3 + 1], c = pose[row * 3 + 2];
        for (int i = 0; i < 3; ++i) out[row * 4 + i] = (parent[i] * a + parent[4 + i] * b) + parent[8 + i] * c;
    }
    for (int i = 0; i < 3; ++i)
        out[12 + i] = ((parent[12 + i] + parent[i] * pose[9]) + parent[4 + i] * pose[10]) + parent[8 + i] * pose[11];
}

const float* base_pose(const XModelParts* parts, uint32_t bone) {
    return reinterpret_cast<const float*>(parts->base_poses + (bone < parts->pose_count ? bone : 0) * 0x30);
}

int32_t bone_parent(const XModelParts* parts, uint32_t bone) {
    return parts->bones[bone < parts->bone_count ? bone : 0].parent;
}

// The base pose alone (a root bone): its rows into the matrix.
void store_pose(uint8_t* out, const float* pose) {
    uint32_t rows[16] = {};
    for (int row = 0; row < 4; ++row) std::memcpy(&rows[row * 4], pose + row * 3, 12);
    BoneMatrix_Store(reinterpret_cast<uint32_t*>(out), 0, rows);
}

void store_child(uint8_t* out, const uint8_t* parent, const float* pose) {
    uint32_t loaded[16];
    BoneMatrix_Load(reinterpret_cast<const uint32_t*>(parent), 0, loaded);
    float composed[16] = {};
    compose_bone(loaded, pose, composed);
    uint32_t bits[16];
    std::memcpy(bits, composed, sizeof bits);
    BoneMatrix_Store(reinterpret_cast<uint32_t*>(out), 0, bits);
}

}  // namespace

int32_t XB_CDECL XModel_CalcSkeleton(const XModel* model, uint8_t* matrices, const uint8_t* attach) {
    const XModelParts* parts = model_parts(model);
    const int32_t count = static_cast<int32_t>(parts->bone_count);
    for (int32_t bone = 0; bone < count; ++bone) {
        uint8_t* out = matrices + bone * 0x40;
        const int32_t parent = bone_parent(parts, static_cast<uint32_t>(bone));
        if (parent >= 0)
            store_child(out, matrices + parent * 0x40, base_pose(parts, static_cast<uint32_t>(bone)));
        else if (attach)
            store_child(out, attach, base_pose(parts, static_cast<uint32_t>(bone)));
        else
            store_pose(out, base_pose(parts, static_cast<uint32_t>(bone)));
    }
    return count;
}

int32_t XB_CDECL XModel_CalcBoneChildren(const XModel* model, uint8_t* matrices, int32_t parent) {
    const XModelParts* parts = model_parts(model);
    const int32_t count = static_cast<int32_t>(parts->bone_count);
    for (int32_t bone = parent + 1; bone < count; ++bone) {
        const int32_t bone_parent_index = bone_parent(parts, static_cast<uint32_t>(bone));
        if (bone_parent_index != parent) continue;
        uint8_t* out = matrices + bone * 0x40;
        if (bone_parent_index < 0) {
            store_pose(out, base_pose(parts, static_cast<uint32_t>(bone)));
            continue;
        }
        store_child(out, matrices + bone_parent_index * 0x40, base_pose(parts, static_cast<uint32_t>(bone)));
        XModel_CalcBoneChildren(model, matrices, bone);
    }
    return count;
}

PLATFORM_ASM_ENTRY(DObj_SetLod_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(DObj_SetLod) "addl $8, %esp\n"
                   "ret")

void XB_CDECL DObj_SetLod(DObj* obj, int32_t lod) {
    const XModel* model = obj->models[0];
    if (!model) return;
    const uint8_t* table = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<const uint8_t*>(model_parts(model)) + 0x38);
    if (table && *reinterpret_cast<const uint32_t*>(table) == title_global<uint32_t>(0x00903fdc) &&
        lod == *reinterpret_cast<const int32_t*>(table + 0x60) - 1) {
        obj->lod = lod;
        return;
    }
    obj->lod = -1;
    if (lod < 5 && lod_entry(model, lod)) {
        obj->lod = lod;
        return;
    }
    for (int32_t i = lod - 1; i >= 0; --i) {
        if (i < 5 && lod_entry(model, i)) {
            obj->lod = i;
            return;
        }
    }
    for (int32_t i = lod + 1; i < 5; ++i) {
        if (lod_entry(model, i)) {
            obj->lod = i;
            return;
        }
    }
}

PLATFORM_ASM_ENTRY(XModelParts_FindBone_entry,
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(XModelParts_FindBone) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL XModelParts_FindBone(const char* name, const XModelParts* parts) {
    if (!name) return -1;
    for (uint32_t i = 0; i < parts->bone_count; ++i) {
        const char* text = parts->bones[i].text;
        if (text && title_function<int XB_CDECL(const char*, const char*)>(0x001f1443)(text, name) == 0)  // _stricmp
            return static_cast<int32_t>(i);
    }
    return -1;
}

int32_t XB_STDCALL DObj_FindBoneIndex2(const DObj* obj, const char* name) {
    int32_t first = 0;
    for (int i = 0; i < obj->model_count; ++i) {
        const int32_t bone = XModelParts_FindBone(name, model_parts(obj->models[i]));
        if (bone >= 0) return bone + first;
        const XModelParts* parts = model_parts(obj->models[i]);
        first += parts ? static_cast<int32_t>(parts->bone_count) : 0;
    }
    return -1;
}

PLATFORM_ASM_ENTRY(DObj_GetTagIndex_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DObj_GetTagIndex) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL DObj_GetTagIndex(const DObj* obj, uint32_t name) {
    const int32_t bone = name ? DObj_GetBoneIndex(obj, name) : -1;
    if (!(title_global<uint8_t>(0x0098b840) & 1)) {
        title_global<uint32_t>(0x0098b840) |= 1;
        title_global<uint32_t>(0x0098b83c) = Com_HashKeyNoCase(1, "tag_origin");
    }
    if (bone < 0 && name == title_global<uint32_t>(0x0098b83c)) return 0;
    return bone;
}

void XB_CDECL DObj_CreateSkeleton(DObj* obj) {
    // 0x40 bytes per bone after a 0x30-byte header, from the client's memory context
    // when there is one, else the heap.
    const uint32_t size = obj->bone_count * 0x40u + 0x30;
    uint8_t* memory = nullptr;
    if (obj->client_index != -1) {
        const int32_t client = obj->client_index;
        uint32_t context = 0;
        if (client >= 0 && client < 0x63)
            context = *reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f638) + 0x38 + client * 4);
        memory = title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x0028c0f0)(context, 0x20,
                                                                                            size);  // MemContext_Alloc_r
    }
    if (!memory) memory = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x20, size);  // Heap_AllocAligned
    obj->bone_matrices = memory;
    auto* header = reinterpret_cast<uint32_t*>(memory);
    for (int i = 0; i < 12; ++i) header[i] = 0;
    for (int m = 0; m < obj->model_count; ++m) {
        uint8_t* parent = attach_matrix(obj, m);
        XModel_CalcSkeleton(obj->models[m], bone_matrix(obj, matrix_offset(obj, m)), parent);
    }
}

PLATFORM_ASM_ENTRY(XModel_ResolveBoneNames_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(XModel_ResolveBoneNames) "addl $4, %esp\n"
                   "ret")

void XB_CDECL XModel_ResolveBoneNames(XModelParts* parts) {
    if (!parts || parts->bones[0].name) return;
    // The bone accessor falls back to bone 0 for an index out of range.
    auto bone = [parts](uint32_t i) -> XBoneInfo& { return parts->bones[i < parts->bone_count ? i : 0]; };
    for (uint32_t i = 0; i < parts->bone_count; ++i) {
        const char* text = bone(i).text;
        uint32_t name = 0;
        if (text) {
            name = title_function<uint32_t XB_CDECL(const char*)>(0x002f5470)(text);  // ScrIface_HashStringNoCase
            title_function<void XB_CDECL(uint32_t, const char*)>(0x00300b30)(name, text);  // NameMap_Insert
        }
        bone(i).name = name;
    }
}

namespace {

// Memory from a client's memory context (0x0095f638 +0x38, index 0..0x62; -1 = the zone
// heap): MemContext_Free_r, Z_Free when that context does not own it.
void free_in_context(void* memory, int32_t context) {
    if (context != -1) {
        const uint32_t owner =
            context >= 0 && context < 0x63 ? title_global<uint32_t*>(0x0095f638)[0x38 / 4 + context] : 0;
        if (title_function<uint8_t XB_THISCALL(uint32_t, void*)>(0x0028bfc0)(owner, memory)) return;  // MemContext_Free_r
    }
    title_function<void XB_CDECL(void*)>(0x000428d0)(memory);  // Z_Free
}

}  // namespace

PLATFORM_ASM_ENTRY(AnimTree_Free_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(AnimTree_Free) "addl $4, %esp\n"
                   "ret")

void XB_CDECL AnimTree_Free(uint32_t* tree) {
    // Unlinked from the live list (0x008dda40, count 0x008dda3c) when found before its last
    // node; tree[0] = next, tree[1] = previous.
    uint32_t* node = title_global<uint32_t*>(0x008dda40);
    for (uint32_t* next = reinterpret_cast<uint32_t*>(uintptr_t{node[0]}); next;
         node = next, next = reinterpret_cast<uint32_t*>(uintptr_t{next[0]})) {
        if (node != tree) continue;
        if (tree[0]) {
            reinterpret_cast<uint32_t*>(uintptr_t{tree[0]})[1] = tree[1];
            reinterpret_cast<uint32_t*>(uintptr_t{tree[1]})[0] = tree[0];
            tree[0] = 0;
            tree[1] = 0;
            --title_global<int32_t>(0x008dda3c);
        }
        break;
    }
    free_in_context(tree, static_cast<int32_t>(tree[4]));
}

PLATFORM_ASM_ENTRY(DObj_Free_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(DObj_Free) "addl $8, %esp\n"
                   "ret")

void XB_CDECL DObj_Free(DObj* obj, uint32_t free_trees) {
    uint8_t* const base = reinterpret_cast<uint8_t*>(obj);
    free_in_context(obj->bone_matrices, obj->client_index);
    uint32_t* const trees = reinterpret_cast<uint32_t*>(base);
    for (uint32_t i = 0; i < obj->model_count; ++i) {
        uint32_t* tree = reinterpret_cast<uint32_t*>(uintptr_t{trees[i]});
        if (!tree) continue;
        if (free_trees) {
            title_function<void XB_CDECL(uint32_t*, uint32_t)>(0x002c3ae0)(tree, 0);  // AnimTree_ClearNode_r
            AnimTree_Free(tree);
        }
        trees[i] = 0;
    }
    uint16_t& flags = *reinterpret_cast<uint16_t*>(base + 0x2c);
    if (flags) flags = 0;
}

PLATFORM_ASM_ENTRY(XModelParts_FindBoneByName_entry,
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(XModelParts_FindBoneByName) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL XModelParts_FindBoneByName(const XModelParts* parts, uint32_t name) {
    for (uint32_t i = 0; i < parts->bone_count; ++i)
        if (parts->bones[i].name == name) return static_cast<int32_t>(i);
    return -1;
}

namespace {

// The model's first LOD (the original scans without a bound).
const XModelLod* first_lod(const XModel* model) {
    const XModelLod* const* lod = model->lods;
    while (!*lod) ++lod;
    return *lod;
}

}  // namespace

// The calls run where the original's put them (its frame: sub esp,0x10 and three pushes,
// ebx pushed around the model loop): DObj_UpdateSkeletons reads stack its callees left.
PLATFORM_ASM_ENTRY(DObj_Create_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DObj_Create) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL DObj_Create(uint32_t entry, uint32_t tree0, uint32_t flags, const DObjModelInfo* infos, uint32_t count,
                          DObj* obj) {
    const uint32_t top = entry - 0x1c;  // after sub esp,0x10 and push ebp/esi/edi
    const auto address = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
    uint8_t* const base = reinterpret_cast<uint8_t*>(obj);
    auto word = [base](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(base + offset); };
    word(0) = tree0;
    word(0x20) = 0;
    word(0x28) = flags & 0xffff;
    *reinterpret_cast<uint16_t*>(base + 0x2c) = 0;
    obj->hidden_models = 0;
    count &= 0xffff;
    uint32_t models = 0;
    uint32_t bones = 0;
    for (; models < count; ++models) {
        const DObjModelInfo& info = infos[models];
        XModel* model = info.model;
        if (!model) break;
        obj->models[models] = model;
        obj->attach_bones[models] = 0xff;
        (&obj->matrix_offset)[models] = static_cast<uint8_t>(bones);
        if (info.hidden) obj->hidden_models |= 1u << models;
        if (info.tree) word(models * 4) = info.tree;
        // Attached to the first earlier model with a bone of that name.
        const char* bone_name = info.bone ? reinterpret_cast<const char*>(info.bone) + 6 : nullptr;
        if (models && bone_name && *bone_name) {
            const TitleRegs at_hash{models, address(model), models, address(bone_name), address(obj->models),
                                    address(&infos[models].hidden) + 4};
            const uint32_t hash = title_call_at_regs(top - 4, 0x00292fe0, &at_hash, 0, nullptr);  // Com_HashKeyNoCase
            if (hash) {
                for (uint32_t j = 0; j < models; ++j) {
                    const XModelParts* parts = first_lod(obj->models[j])->parts;
                    if (!parts) break;
                    const TitleRegs at_find{0, hash, address(first_lod(obj->models[j])), address(parts),
                                            address(&obj->models[j]), j};
                    const int32_t bone = static_cast<int32_t>(
                        title_call_at_regs(top - 4, 0x000291d0, &at_find, 0, nullptr));  // XModelParts_FindBoneByName
                    if (bone < 0) continue;
                    obj->attach_bones[models] = static_cast<uint8_t>((&obj->matrix_offset)[j] + bone);
                    break;
                }
            }
        }
        const XModelParts* parts = first_lod(model)->parts;
        const uint32_t total = (parts ? parts->bone_count : 0) + bones;
        if (static_cast<int32_t>(total) >= 0x80) {
            title_function<void XB_CDECL(int32_t, uint32_t, uint32_t, int32_t)>(0x002938f0)(  // Com_Error
                1, 0x0083c580, *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(obj->models[0]) + 0x48),
                0x7f);
            break;
        }
        bones = total;
    }
    obj->model_count = static_cast<uint8_t>(models);
    obj->bone_count = static_cast<uint8_t>(bones);
    const TitleRegs at_skeleton{0, 0, 0, 0, 0, address(&infos[models])};
    const uint32_t create_args[] = {address(obj), 0};
    title_call_at_regs(top, 0x00240c00, &at_skeleton, 2, create_args);  // DObj_CreateSkeleton
    const uint32_t update_args[] = {address(obj), address(obj), 0};  // its arguments are still there
    title_call_at_regs(top, 0x002df880, &at_skeleton, 3, update_args);  // DObj_UpdateSkeletons
    // The distance LOD: 0 for a model whose first LOD's surface (+0x38 of its parts) is of the
    // kind at 0x00903fdc with +0x60 == 1; else the first LOD the model has among 0-4 (-1 = none).
    const XModel* model = obj->models[0];
    if (!model) return;
    const uint32_t* surface =
        *reinterpret_cast<const uint32_t* const*>(reinterpret_cast<const uint8_t*>(first_lod(model)->parts) + 0x38);
    if (surface && surface[0] == title_global<uint32_t>(0x00903fdc) && surface[0x60 / 4] == 1) {
        obj->lod = 0;
        return;
    }
    obj->lod = -1;
    if (model->lods[0]) {
        obj->lod = 0;
        return;
    }
    for (int32_t k = 1; k < 5; ++k) {
        if (model->lods[k]) {
            obj->lod = k;
            return;
        }
    }
}

void XB_CDECL DObj_GetAnimRootTransform(uint32_t* out, const DObj* obj) {
    // Identity {quaternion, position}; the eighth word is never set (the test stack's 0).
    const float one = title_global<float>(0x0083eb20);
    const float identity[8] = {0.0f, 0.0f, 0.0f, one, 0.0f, 0.0f, 0.0f, 0.0f};
    std::memcpy(out, identity, 32);
    if (!obj->bone_matrices) return;
    const XModel* model = obj->models[0];
    int32_t lod = 0;
    while (!model->lods[lod]) ++lod;
    const uint8_t* skeleton = *reinterpret_cast<uint8_t* const*>(reinterpret_cast<const uint8_t*>(model->lods[lod]->parts) + 0x38);
    const uint32_t generic = title_global<uint32_t>(0x00903fdc);  // the generic clip's vtable
    if (!skeleton || *reinterpret_cast<const uint32_t*>(skeleton) != generic) return;
    const AnimState* state = reinterpret_cast<const AnimState*>(obj->lod_limit);
    if (!state || !state->clip || *reinterpret_cast<const uint32_t*>(state->clip) != generic) return;
    NAL_GenericAnim_GetRootTransform(state->clip, state, out);
}

void XB_CDECL XModel_TransformBones(const XModel* model, uint8_t* bones, const uint8_t* parent) {
    int32_t lod = 0;
    while (!model->lods[lod]) ++lod;
    const int32_t count = static_cast<int32_t>(model->lods[lod]->parts->bone_count);
    alignas(16) float p[16];
    BoneMatrix_Load(reinterpret_cast<const uint32_t*>(parent), 0, reinterpret_cast<uint32_t*>(p));
    for (int32_t i = 0; i < count; ++i, bones += 0x40) {
        alignas(16) float b[16];
        BoneMatrix_Load(reinterpret_cast<const uint32_t*>(bones), 0, reinterpret_cast<uint32_t*>(b));
        alignas(16) float out[16];
        for (int row = 0; row < 3; ++row) {
            const float* r = b + row * 4;
            const Lanes v = add(add(mul(lanes(p), r[0]), mul(lanes(p + 4), r[1])), mul(lanes(p + 8), r[2]));
            std::memcpy(out + row * 4, v.v, 16);
        }
        const Lanes v3 = add(add(add(lanes(p + 12), mul(lanes(p), b[12])), mul(lanes(p + 4), b[13])), mul(lanes(p + 8), b[14]));
        std::memcpy(out + 12, v3.v, 16);
        BoneMatrix_Store(reinterpret_cast<uint32_t*>(bones), 0, reinterpret_cast<const uint32_t*>(out));
    }
}

namespace {

// Calls a title function with EAX, ECX and EDX set and one stack word pushed (popped
// again here: the callees are cdecl or take no stack argument).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL dobj_call_regs(uint32_t function, uint32_t eax, uint32_t ecx, uint32_t edx,
                                                            uint32_t arg);
PLATFORM_ASM_ENTRY(dobj_call_regs,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl 24(%ebp)\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 20(%ebp), %edx\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The skeleton clip of a model's first LOD (parts +0x38) when it is a generic NAL clip.
uint8_t* generic_skeleton(const XModel* model) {
    int32_t lod = 0;
    while (!model->lods[lod]) ++lod;
    uint8_t* clip = *reinterpret_cast<uint8_t* const*>(reinterpret_cast<const uint8_t*>(model->lods[lod]->parts) + 0x38);
    if (clip && *reinterpret_cast<const uint32_t*>(clip) == title_global<uint32_t>(0x00903fdc)) return clip;
    return nullptr;
}

// q * ((3 - r*r*s) * r * 0.5), r = RSQRTSS(s), s = |q|^2 (lanes summed (0+2)+(1+3)).
void normalize_quat(float* q) {
    const Lanes v = lanes(q);
    const Lanes sq = mul(v, v);
    const Lanes t = add(sq, {{sq.v[2], sq.v[3], sq.v[0], sq.v[0]}});
    platform_keep(t);
    const float s = t.v[0] + t.v[1];
    const float r = platform_rsqrt_estimate(s);
    const float f = (title_global<float>(0x0082e660) - r * r * s) * r * title_global<float>(0x0082e670);
    const Lanes out = mul(v, f);
    std::memcpy(q, out.v, 16);
}

}  // namespace

void XB_CDECL DObj_UpdateSkeletons(DObj* obj) {
    uint8_t* bones = obj->bone_matrices;
    if (!bones) return;
    uint8_t* ent = const_cast<uint8_t*>(obj->owner);
    if (ent && ((*reinterpret_cast<const uint32_t*>(ent + 0x2a4) & 0x80400000) || *reinterpret_cast<const uint32_t*>(ent + 0x3b0)))
        return;
    const float one = title_global<float>(0x0083eb20);
    for (int32_t i = 0; i < obj->model_count; ++i) {
        XModel* model = obj->models[i];
        uint8_t* skeleton = generic_skeleton(model);
        // Per model: its anim tree (+0 + 4i) and clip state (+0x68 + 4i).
        const uint8_t* tree = reinterpret_cast<uint8_t* const*>(obj)[i];
        uint8_t* matrices = bones + (&obj->matrix_offset)[i] * 0x40 + 0x30;
        const uint8_t parent_bone = obj->attach_bones[i];
        const uint8_t* attach = parent_bone == 0xff ? nullptr : bones + parent_bone * 0x40 + 0x30;
        const bool no_anim = !tree || (ent && (*reinterpret_cast<const uint32_t*>(ent + 0x248) || *reinterpret_cast<const uint32_t*>(ent + 0x24c)) &&
                                       !*reinterpret_cast<const uint32_t*>(tree + 0x14));
        if (no_anim) {
            XModel_CalcSkeleton(model, matrices, attach);
            continue;
        }
        if (!*reinterpret_cast<const uint32_t*>(tree + 0x14)) continue;
        if (!skeleton) {
            // Models without a generic skeleton share the one named "simple".
            alignas(4) uint8_t name[0x20];
            title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(name, &title_global<char>(0x00832a28));
            int32_t lod = 0;
            while (!model->lods[lod]) ++lod;
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(model->lods[lod]->parts) + 0x38) =
                NAL_FindSkeleton(addr(name));
            skeleton = generic_skeleton(model);
        }
        AnimState*& clip_state = reinterpret_cast<AnimState**>(reinterpret_cast<uint8_t*>(obj) + 0x68)[i];
        if (!clip_state) clip_state = Anim_CreateClipState(obj->client_index, skeleton);
        title_global<uint32_t>(0x00928588) = addr(tree);
        AnimState* state = clip_state;
        if (!state || !state->clip || *reinterpret_cast<const uint32_t*>(state->clip) != title_global<uint32_t>(0x00903fdc))
            state = nullptr;
        uint8_t root_motion = 0;
        if (title_global<uint32_t>(0x00928588)) {
            const int32_t lod = obj->forced_lod >= 0 ? obj->forced_lod : obj->lod;
            root_motion = AnimTree_CalcPose_r(0, state, reinterpret_cast<AnimState*>(skeleton + 0xc8), 1, 0, lod);
        }
        int32_t lod = 0;
        while (!model->lods[lod]) ++lod;
        const XModelParts* parts = model->lods[lod]->parts;
        const uint32_t bone_count = parts ? parts->bone_count : 0;
        const uint8_t* ranges = *reinterpret_cast<uint8_t* const*>(state->clip + 0x64);
        if (bone_count >= *reinterpret_cast<const uint32_t*>(ranges + state->range * 20)) {
            NAL_GenericAnim_EvaluatePose(state->clip, reinterpret_cast<uint8_t*>(state), reinterpret_cast<float*>(matrices),
                                         static_cast<uint32_t>(state->range));
            if (attach) XModel_TransformBones(model, matrices, attach);
        }
        if (!ent || !(*reinterpret_cast<const uint32_t*>(ent + 0x2a4) & 0x1000000)) continue;
        // Root motion moves the entity.
        alignas(16) uint32_t record[8];
        alignas(16) float origin[3];
        alignas(16) float angles[3];
        alignas(16) float product[16];
        uint8_t* link = *reinterpret_cast<uint8_t**>(ent + 0x3cc);
        uint8_t* accumulated = *reinterpret_cast<uint8_t**>(ent + 0x3d4);
        if (link) {
            // Tag-linked: the root motion moves the link's local matrix (4x3 at +0x10).
            DObj_GetAnimRootTransform(record, obj);
            alignas(16) float m[16];
            QuatTrans_ToMat44(m, 0, reinterpret_cast<const float*>(record));
            const float* local = reinterpret_cast<const float*>(link + 0x10);
            alignas(16) float t[16] = {local[0], local[1], local[2], 0.0f, local[3], local[4], local[5], 0.0f,
                                       local[6], local[7], local[8], 0.0f, local[9], local[10], local[11], one};
            Mat44_MultiplyInPlace(t, product, m);
            const int rows[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
            for (int k = 0; k < 12; ++k)
                reinterpret_cast<float*>(*reinterpret_cast<uint8_t**>(ent + 0x3cc) + 0x10)[k] = t[rows[k]];
            alignas(16) float parent[12];
            dobj_call_regs(0x002df3e0, addr(ent), addr(parent), 0, addr(parent));  // G_GetTagParentMatrix
            alignas(16) float world[12];
            dobj_call_regs(0x002954a0, addr(parent), 0, addr(world), addr(*reinterpret_cast<uint8_t**>(ent + 0x3cc) + 0x10));  // MatrixMultiply43
            origin[0] = world[9];
            origin[1] = world[10];
            origin[2] = world[11];
            dobj_call_regs(0x002a27f0, addr(angles), addr(world), 0, 0);  // AxisToAngles
        } else if (root_motion) {
            DObj_GetAnimRootTransform(record, obj);
            alignas(16) float m[16];
            QuatTrans_ToMat44(m, 0, reinterpret_cast<const float*>(record));
            std::memcpy(origin, record + 4, 12);
            dobj_call_regs(0x002a26a0, addr(angles), addr(m), 0, 0);  // MatrixToAngles
        } else if (accumulated) {
            // The root delta accumulated into the entity's record (+0x20), renormalized.
            DObj_GetAnimRootTransform(record, obj);
            alignas(16) float concat[8] = {};
            std::memcpy(accumulated + 0x20, QuatTrans_Concat(concat, reinterpret_cast<const float*>(record),
                                                             reinterpret_cast<const float*>(accumulated + 0x20)),
                        32);
            normalize_quat(reinterpret_cast<float*>(accumulated + 0x20));
            alignas(16) float base[8] = {};
            std::memcpy(record, QuatTrans_Concat(base, reinterpret_cast<const float*>(accumulated + 0x20),
                                                 reinterpret_cast<const float*>(accumulated)),
                        32);
            normalize_quat(reinterpret_cast<float*>(record));
            alignas(16) float m[16];
            QuatTrans_ToMat44(m, 0, reinterpret_cast<const float*>(record));
            origin[0] = m[12];
            origin[1] = m[13];
            origin[2] = m[14];
            dobj_call_regs(0x002a26a0, addr(angles), addr(m), 0, 0);  // MatrixToAngles
        } else {
            // The delta applied to the entity's current axis and origin.
            DObj_GetAnimRootTransform(record, obj);
            alignas(16) float m[16];
            QuatTrans_ToMat44(m, 0, reinterpret_cast<const float*>(record));
            float axis[9];
            dobj_call_regs(0x002a4610, addr(ent + 0x150), 0, 0, addr(axis));  // AnglesToAxisLeft
            const float* position = reinterpret_cast<const float*>(ent + 0x140);
            alignas(16) float b[16] = {axis[0], axis[1], axis[2], 0.0f, axis[3], axis[4], axis[5], 0.0f,
                                       axis[6], axis[7], axis[8], 0.0f, position[0], position[1], position[2], one};
            Mat44_MultiplyInPlace(b, product, m);
            origin[0] = b[12];
            origin[1] = b[13];
            origin[2] = b[14];
            dobj_call_regs(0x002a26a0, addr(angles), addr(b), 0, 0);  // MatrixToAngles
        }
        dobj_call_regs(0x002b7730, addr(ent), addr(origin), 0, 0);  // G_SetOrigin2
        dobj_call_regs(0x002b7690, addr(ent), addr(angles), 0, 0);  // G_SetAngle2
    }
}

void XB_STDCALL XModel_Free(uint8_t* model) {
    // Inside the model's memory context (+0x34, -1 = none): the eight anim states at +0x68
    // destroyed and freed, the handle at +0x8c released in the handle table (slot = low 12
    // bits, generation = the rest: the slot's free bit set, its generation bumped, its pointer
    // cleared), +0x88 poisoned.
    const int32_t context = *reinterpret_cast<const int32_t*>(model + 0x34);
    uint8_t saved = 0;
    if (context != -1) {
        saved = title_global<uint8_t>(0x0095f5d0);
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = 1;
        MemContext_Push(title_global<uint8_t*>(0x0095f638), static_cast<uint32_t>(context));
    }
    auto* states = reinterpret_cast<AnimState**>(model + 0x68);
    for (int i = 0; i < 8; ++i) {
        if (AnimState* state = states[i]) {
            NAL_AnimState_Destroy(state);
            title_function<void XB_CDECL(void*)>(0x000939b0)(state);  // Mem_AlignedFree
        }
    }
    const uint32_t handle = *reinterpret_cast<const uint32_t*>(model + 0x8c);
    if (handle) {
        const int32_t slot = static_cast<int32_t>(handle & 0xfff);
        if (slot < 0x540 && title_global<uint32_t>(0x0098141c + slot * 8) == handle >> 12) {
            title_global<uint32_t>(0x00981370 + (slot >> 5) * 4) |= 1u << (slot & 31);
            ++title_global<uint32_t>(0x0098141c + slot * 8);
            title_global<uint32_t>(0x00981418 + slot * 8) = 0;
        }
    }
    *reinterpret_cast<uint32_t*>(model + 0x88) = 0xafafafaf;
    if (context != -1) {
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = saved;
        MemContext_Pop(title_global<uint8_t*>(0x0095f638));
    }
}

PLATFORM_ASM_ENTRY(XModel_FreeAnimStates_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(XModel_FreeAnimStates) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL XModel_FreeAnimStates(uint8_t* model) {
    // XModel_Free's anim-state part for the model's own count (+0x2e, read again each
    // iteration): each state at +0x68 destroyed, freed and its slot cleared, inside the
    // model's memory context (+0x34, -1 = none; returns MemContext_Pop's result then).
    const int32_t context = *reinterpret_cast<const int32_t*>(model + 0x34);
    uint8_t saved = 0;
    if (context != -1) {
        saved = title_global<uint8_t>(0x0095f5d0);
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = 1;
        MemContext_Push(title_global<uint8_t*>(0x0095f638), static_cast<uint32_t>(context));
    }
    auto* states = reinterpret_cast<AnimState**>(model + 0x68);
    for (int32_t i = 0; i < model[0x2e]; ++i) {
        if (AnimState* state = states[i]) {
            NAL_AnimState_Destroy(state);
            title_function<void XB_CDECL(void*)>(0x000939b0)(state);  // Mem_AlignedFree
            states[i] = nullptr;
        }
    }
    if (context == -1) return model[0x2e];
    title_global<uint8_t>(0x0095f5d1) = 0;
    title_global<uint8_t>(0x0095f5d0) = saved;
    return MemContext_Pop(title_global<uint8_t*>(0x0095f638));
}

PLATFORM_ASM_ENTRY(DObj_DebugDrawAnimTrees_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(DObj_DebugDrawAnimTrees) "addl $4, %esp\n"
                   "ret")

void XB_CDECL DObj_DebugDrawAnimTrees(uint8_t* const* obj) {
    // With an anim tree on the first model: the debug queues flushed (group 2), then each
    // model's tree (+0, one per model, count +0x2e) as debug text, one shared line counter.
    if (!obj[0]) return;
    DebugDraw_Flush(0);
    const uint8_t* count = reinterpret_cast<const uint8_t*>(obj) + 0x2e;
    int32_t line = 0;
    for (int32_t i = 0; i < *count; ++i) {
        if (uint8_t* tree = obj[i]) {
            title_function<void XB_CDECL(uint8_t*, uint32_t, uint32_t, int32_t*)>(0x002c4310)(tree, 0, 0, &line);  // AnimTree_DebugDraw_r
        }
    }
}

namespace {

// Bounds_AddPoints8 (0x0023c400): the eight corners of the box at `box` (mins +0, maxs
// +0x10) through `matrix` into `bounds` (mins, then maxs at +0x10, four lanes each).
extern "C" PLATFORM_HIDDEN void XB_CDECL dobj_bounds_add_points8(const float* matrix, const void* box, float* bounds);
PLATFORM_ASM_ENTRY(dobj_bounds_add_points8,
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 12(%esp), %esi\n"
                   "movl 16(%esp), %edi\n"
                   "pushl 20(%esp)\n"
                   "movl $0x0023c400, %eax\n"
                   "call *%eax\n"
                   "addl $4, %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "ret")

}  // namespace

void XB_REGS1 XB_CDECL DObj_GetTransformedBounds(const float* matrix, const DObj* obj, float* mins, float* maxs) {
    const uint32_t big = title_global<uint32_t>(0x0082eed0);       // FLT_MAX
    const uint32_t neg_big = title_global<uint32_t>(0x0083ef2c);   // -FLT_MAX
    alignas(16) uint32_t bounds[8] = {big, big, big, big, neg_big, neg_big, neg_big, neg_big};
    float* box = reinterpret_cast<float*>(bounds);
    for (int32_t i = 0; i < obj->model_count; ++i) dobj_bounds_add_points8(matrix, obj->models[i], box);
    mins[0] = box[0];
    maxs[0] = box[4];
    mins[1] = box[1];
    maxs[1] = box[5];
    mins[2] = box[2];
    maxs[2] = box[6];
}

PLATFORM_ASM_ENTRY(DObj_GetRootAnimDelta_entry,
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DObj_GetRootAnimDelta) "addl $12, %esp\n"
                   "ret")

void XB_CDECL DObj_GetRootAnimDelta(const DObj* obj, float* dir, float* delta) {
    // Identity {quaternion, position}; the eighth word is never set.
    const float one = title_global<float>(0x0083eb20);
    alignas(16) float root[8] = {0.0f, 0.0f, 0.0f, one, 0.0f, 0.0f, 0.0f, 0.0f};
    if (obj->bone_matrices) {
        const XModel* model = obj->models[0];
        int32_t lod = 0;
        while (!model->lods[lod]) ++lod;
        const uint8_t* skeleton =
            *reinterpret_cast<uint8_t* const*>(reinterpret_cast<const uint8_t*>(model->lods[lod]->parts) + 0x38);
        const uint32_t generic = title_global<uint32_t>(0x00903fdc);  // the generic clip's vtable
        const AnimState* state = reinterpret_cast<const AnimState*>(obj->lod_limit);
        if (skeleton && *reinterpret_cast<const uint32_t*>(skeleton) == generic && state && state->clip &&
            *reinterpret_cast<const uint32_t*>(state->clip) == generic)
            NAL_GenericAnim_GetRootTransform(state->clip, state, reinterpret_cast<uint32_t*>(root));
    }
    alignas(16) float rotation[16];
    Quat_ToMat33(rotation, root);
    dir[0] = rotation[0];
    dir[1] = rotation[1];
    delta[0] = root[4];
    delta[1] = root[5];
    delta[2] = root[6];
}

void XB_CDECL XModel_CalcBoneChildrenOrtho(const XModel* model, uint8_t* matrices, int32_t parent) {
    const XModelParts* first = model_parts(model);  // its bone table (no null check, as in the original)
    for (int32_t bone = parent + 1;; ++bone) {
        const XModelParts* parts = model_parts(model);
        if (bone >= static_cast<int32_t>(parts ? parts->bone_count : 0)) break;
        if (bone_parent(first, static_cast<uint32_t>(bone)) != parent) continue;
        const float* p = reinterpret_cast<const float*>(matrices + parent * 0x40);
        float* b = reinterpret_cast<float*>(matrices + bone * 0x40);
        alignas(16) float rows[12];
        for (int row = 0; row < 3; ++row) {
            const float* r = b + row * 4;
            const Lanes v = add(add(mul(lanes(p), r[0]), mul(lanes(p + 4), r[1])), mul(lanes(p + 8), r[2]));
            std::memcpy(rows + row * 4, v.v, 16);
        }
        std::memcpy(b, rows, sizeof rows);
        const float* t = b + 12;
        const Lanes v3 = add(add(add(lanes(p + 12), mul(lanes(p), t[0])), mul(lanes(p + 4), t[1])), mul(lanes(p + 8), t[2]));
        std::memcpy(b + 12, v3.v, 16);
        title_function<void XB_THISCALL(float*)>(0x002cede0)(b);  // Matrix_Orthonormalize
        XModel_CalcBoneChildrenOrtho(model, matrices, bone);
    }
}

namespace {

template <typename T>
T& at(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

template <typename T>
const T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
}

// p - origin (+12) times the rows of the bone matrix m, as XModel_TraceParts sums them.
void into_bone_space(const float* m, const float* p, float* out) {
    const float dx = p[0] - m[12];
    const float dz = p[2] - m[14];
    const float dy = p[1] - m[13];
    out[0] = (m[0] * dx + m[2] * dz) + m[1] * dy;
    out[1] = (m[4] * dx + m[6] * dz) + m[5] * dy;
    out[2] = (m[8] * dx + dz * m[10]) + dy * m[9];
}

}  // namespace

int32_t XB_CDECL XModel_TraceParts(const uint8_t* model, uint8_t* trace, const uint8_t* skeleton, const float* start,
                                   const float* end, uint32_t content_mask) {
    int32_t hit_bone = -1;
    int32_t bone = -1;  // the bone the segment below is in
    float s[3] = {};    // start, end and end - start in that bone's space
    float e[3] = {};
    float d[3] = {};
    for (uint32_t i = 0; i < at<uint32_t>(model, 0x38); ++i) {
        const uint32_t count = at<uint32_t>(model, 0x38);
        const uint8_t* part = at<const uint8_t* const*>(model, 0x3c)[i < count ? i : 0];
        if (!(at<uint32_t>(part, 0x2c) & content_mask)) continue;
        if (at<int32_t>(part, 0x28) != bone) {
            bone = at<int32_t>(part, 0x28);
            const float* m = reinterpret_cast<const float*>(skeleton + bone * 0x40);
            into_bone_space(m, start, s);
            into_bone_space(m, end, e);
            d[0] = e[0] - s[0];
            d[1] = e[1] - s[1];
            d[2] = e[2] - s[2];
        }
        const float* box = reinterpret_cast<const float*>(part);
        alignas(16) const float mins[4] = {box[0], box[1], box[2], box[0]};
        alignas(16) const float maxs[4] = {box[4], box[5], box[6], box[4]};
        alignas(16) const float p[4] = {e[0], e[1], e[2], e[0]};
        alignas(16) const float q[4] = {s[0], s[1], s[2], s[0]};
        if (SegmentMissesBounds(mins, maxs, p, q, at<float>(trace, 0x20))) continue;
        for (uint32_t j = 0; j < at<uint32_t>(part, 0x20); ++j) {
            const uint32_t planes = at<uint32_t>(part, 0x20);
            const float* plane = at<const float* const*>(part, 0x24)[j < planes ? j : 0];
            const float d_end = ((plane[2] * e[2] + plane[1] * e[1]) + plane[0] * e[0]) - plane[3];
            if (d_end >= 0.0f) continue;
            const float d_start = ((plane[2] * s[2] + plane[1] * s[1]) + plane[0] * s[0]) - plane[3];
            if (0.0f >= d_start) continue;
            const float span = d_start - d_end;
            const float fraction = (d_start - title_global<float>(0x0083ec68)) / span;  // 0.125
            if (fraction >= at<float>(trace, 0x20)) continue;
            const float t = d_start / span;
            const float px = t * d[0] + s[0];
            const float pz = t * d[2] + s[2];
            const float py = t * d[1] + s[1];
            const float low = title_global<float>(0x0083eef4);  // -0.005
            const float u = ((plane[6] * pz + plane[5] * py) + plane[4] * px) - plane[7];
            if (low > u) continue;
            const float high = title_global<float>(0x0083ef74);  // 1.005
            if (u > high) continue;
            const float v = ((plane[10] * pz + plane[9] * py) + plane[8] * px) - plane[11];
            if (low > v) continue;
            if (v + u > high) continue;
            at<uint8_t>(trace, 0x3d) = 0;
            at<uint8_t>(trace, 0x3c) = 0;
            at<float>(trace, 0x20) = fraction;
            hit_bone = bone;
            at<uint32_t>(trace, 0x24) = at<uint32_t>(part, 0x30);
            at<uint32_t>(trace, 0x28) = at<uint32_t>(part, 0x2c);
            std::memcpy(trace + 0x10, plane, 12);
        }
    }
    if (hit_bone < 0) return -1;
    const float* m = reinterpret_cast<const float*>(skeleton + hit_bone * 0x40);
    float* n = reinterpret_cast<float*>(trace + 0x10);
    const float nx = n[0], ny = n[1], nz = n[2];
    const float wx = (m[8] * nz + m[4] * ny) + m[0] * nx;
    const float wy = (m[9] * nz + m[5] * ny) + m[1] * nx;
    const float wz = (m[10] * nz + m[6] * ny) + m[2] * nx;
    n[0] = wx;
    n[1] = wy;
    n[2] = wz;
    return hit_bone;
}

// EntHandleTable_Alloc (0x00027f30): EAX = the table (free-slot bits, then {object,
// generation} pairs at +0xa8), EBX = where the new handle goes (returned).
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL EntHandleTable_Alloc_call(uint32_t table, uint32_t* out);
PLATFORM_ASM_ENTRY(EntHandleTable_Alloc_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %eax\n"
                   "movl 12(%esp), %ebx\n"
                   "movl $0x00027f30, %ecx\n"
                   "call *%ecx\n"
                   "popl %ebx\n"
                   "ret")

PLATFORM_ASM_ENTRY(DObj_Ctor_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DObj_Ctor) "addl $8, %esp\n"
                   "ret")

DObj* XB_CDECL DObj_Ctor(int32_t client_index, DObj* obj) {
    uint8_t* self = reinterpret_cast<uint8_t*>(obj);
    auto word = [self](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(self + offset); };
    obj->client_index = client_index;
    for (uint32_t offset = 0x38; offset <= 0x54; offset += 4) word(offset) = 0;  // models
    obj->owner = nullptr;
    obj->handle = 0;
    obj->forced_lod = -1;
    obj->min_lod = -1;
    obj->lod = 0;
    obj->cull_flags = 0;
    // A handle from the DObj table (0x00981370; objects at 0x00981418 + slot * 8).
    uint32_t handle;
    handle = *EntHandleTable_Alloc_call(0x00981370, &handle);
    obj->handle = handle;
    const uint32_t slot = handle & 0xfff;
    if (slot < 0x540) title_global<uint32_t>(0x00981418 + slot * 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj));
    // The per-model anim trees (+0) and clip states (+0x68).
    for (uint32_t i = 0; i < 8; ++i) {
        word(i * 4) = 0;
        word(0x68 + i * 4) = 0;
    }
    return obj;
}


}  // namespace game
