#include "dobj.h"

#include "compiler.h"
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

using CalcSkeleton = void XB_CDECL(const XModel*, uint8_t*, const uint8_t*);

}  // namespace

void XB_CDECL DObj_CalcBasePose(const DObj* obj) {
    for (int m = 0; m < obj->model_count; ++m) {
        uint8_t* parent = attach_matrix(obj, m);
        title_function<CalcSkeleton>(0x0023df00)(obj->models[m], bone_matrix(obj, matrix_offset(obj, m)),
                                                 parent);  // XModel_CalcSkeleton
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
            title_function<void XB_CDECL(const XModel*, uint8_t*, int32_t)>(0x0023db60)(obj->models[m], matrices,
                                                                                    bone - first);  // XModel_CalcBoneChildren
        else
            title_function<CalcSkeleton>(0x0023df00)(obj->models[m], matrices, parent);  // XModel_CalcSkeleton
    }
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

}  // namespace game
