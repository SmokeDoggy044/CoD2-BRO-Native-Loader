#include "dobj.h"

namespace game {

namespace {

const XModelParts* model_parts(const XModel* model) {
    // No end check, as in the original: a model always has a level of detail.
    const XModelLod* const* lod = model->lods;
    while (!*lod) ++lod;
    return (*lod)->parts;
}

}  // namespace

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

uint32_t XB_CDECL XModel_GetBoneCount(const XModel* model) {
    int i = 0;
    while (!model->lods[i]) ++i;
    const XModelParts* parts = model->lods[i]->parts;
    return parts ? parts->bone_count : 0;
}

}  // namespace game
