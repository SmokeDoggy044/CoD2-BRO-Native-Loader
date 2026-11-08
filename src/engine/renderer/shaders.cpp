#include "shaders.h"

#include <cstring>

#include "lanes.h"
#include "qcommon.h"
#include "ngl_gfx.h"
#include "title_data.h"

namespace game {

namespace {

NglScene* current_scene() { return title_global<NglScene*>(0x0090259c); }

bool disabled(uint32_t mask, uint8_t bit) { return title_global<uint8_t>(mask) & bit; }

void toggle(uint32_t mask, uint8_t bit) { title_global<uint8_t>(mask) ^= bit; }

void** class_vtable(uint32_t address) { return reinterpret_cast<void**>(static_cast<uintptr_t>(address)); }

uint32_t class_id(uint32_t singleton) { return title_global<NglShaderClass*>(singleton)->id; }

// The base class constructor: the new object goes onto the class list. Null when
// Z_Malloc fails (the singleton is then null too).
NglShaderClass* new_class(uint32_t singleton) {
    void* memory = title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(sizeof(NglShaderClass));  // Z_Malloc
    auto* self = static_cast<NglShaderClass*>(memory);
    title_global<NglShaderClass*>(singleton) = self;
    if (!self) return nullptr;
    self->vtable = class_vtable(0x007b3ec0);
    NglShaderClass*& list = title_global<NglShaderClass*>(0x00909904);
    self->next = list;
    list = self;
    return self;
}

void init_class(uint32_t singleton, uint32_t vtable, uint32_t mask, uint8_t bit) {
    NglShaderClass* self = new_class(singleton);
    if (!self) return;
    self->vtable = class_vtable(vtable);
    if (mask) title_global<uint8_t>(mask) &= static_cast<uint8_t>(~bit);
}

NglHashedName* hashed_name(NglHashedName* out, const char* name) {
    return title_function<NglHashedName* XB_THISCALL(NglHashedName*, const char*)>(0x00011060)(out, name);
}

// A section's draw-list node in scene memory (16-byte aligned; some classes make it
// larger than the 0x18 bytes filled here).
NglMeshNode* make_node(uint32_t vtable, uint32_t size, NglMeshDrawNode* node, const NglMeshSection* section,
                       const uint8_t* material) {
    uintptr_t& next = title_global<uintptr_t>(0x009025a8);
    const uintptr_t block = (next + 15) & ~uintptr_t{15};
    next = block + size;
    auto* item = reinterpret_cast<NglMeshNode*>(block);
    item->vtable = class_vtable(vtable);
    item->world_matrix = node->world;
    item->section = section;
    item->material = material;
    return item;
}

NglMeshNode* make_node(uint32_t vtable, NglMeshDrawNode* node, const NglMeshSection* section,
                       const uint8_t* material) {
    return make_node(vtable, sizeof(NglMeshNode), node, section, material);
}

void add_opaque(NglMeshNode* item, uint32_t key) {
    NglScene* scene = current_scene();
    item->sort_key = key;
    item->next = scene->opaque_nodes;
    scene->opaque_nodes = item;
    ++scene->opaque_count;
}

void add_translucent(NglMeshNode* item, float key) {
    NglScene* scene = current_scene();
    std::memcpy(&item->sort_key, &key, 4);
    item->next = scene->translucent_nodes;
    scene->translucent_nodes = item;
    ++scene->translucent_count;
}

// The section's bounding sphere against the scene frustum, its centre moved by the
// instance matrix (all four lanes, as packed: row 3 + row 0 x + row 1 y + row 2 z).
bool section_in_frustum(const NglMeshDrawNode* node, const NglMeshSection* section) {
    const float* m = node->world;
    const float* c = section->center;
    alignas(16) float center[4];
    for (int i = 0; i < 4; ++i) center[i] = ((m[12 + i] + m[i] * c[0]) + m[4 + i] * c[1]) + m[8 + i] * c[2];
    return nglSphereInFrustum(center, section->radius);
}

// nglVertexShader_Init's and nglPixelShader_Init's loops over a class's tables.
void init_vertex_shaders(uint32_t shaders, uint32_t programs, int count, uint32_t declaration) {
    for (int i = 0; i < count; ++i) {
        nglVertexShader_Init(&title_global<uint32_t>(shaders + i * 8), declaration,
                             title_global<uint32_t>(programs + i * 4));
    }
}

void init_pixel_shaders(uint32_t shaders, uint32_t programs, int count) {
    for (int i = 0; i < count; ++i) {
        nglPixelShader_Init(&title_global<uint32_t>(shaders + i * 4), title_global<uint32_t>(programs + i * 4));
    }
}

}  // namespace

void XB_CDECL cdSimpleInstance_Toggle() { toggle(0x00901e8e, 0x02); }

void XB_CDECL cdDebug_Toggle() { toggle(0x00901e8c, 0x08); }

void XB_CDECL cdDebug_Init() { init_class(0x00901ec4, 0x007b3ecc, 0x00901e8c, 0x08); }

NglHashedName* XB_THISCALL cdDebug_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdDebug");
}

void XB_THISCALL cdDebug_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8c, 0x08)) return;
    nglListAddNode(make_node(0x007b3ec4, node, section, material));
}

void XB_CDECL ShaderPCUV_Init() { init_class(0x00901ecc, 0x007b4160, 0, 0); }

NglHashedName* XB_THISCALL ShaderPCUV_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "PCUV");
}

void XB_THISCALL ShaderPCUV_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                      const NglMeshSection* section, const uint8_t* material) {
    NglMeshNode* item = make_node(0x007b4154, node, section, material);
    // Material +0x20: 0 and 1 go on the opaque list, other values are translucent.
    const uint32_t mode = *reinterpret_cast<const uint32_t*>(material + 0x20);
    if (mode == 0 || mode == 1) {
        add_opaque(item, class_id(0x00901ecc));
    } else {
        add_translucent(item, nglNode_ViewDepth(item));
    }
}

void XB_CDECL cdGunSight_Toggle() { toggle(0x00901e8d, 0x02); }

void XB_CDECL cdGunSight_Init() { init_class(0x00901ed4, 0x007b43ec, 0x00901e8d, 0x02); }

NglHashedName* XB_THISCALL cdGunSight_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdGunSight");
}

void XB_THISCALL cdGunSight_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                      const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x02)) return;
    add_opaque(make_node(0x007b43e4, node, section, material), class_id(0x00901ed4));
}

void XB_CDECL cdGun_Toggle() { toggle(0x00901e8d, 0x01); }

void XB_CDECL cdGun_Init() { init_class(0x00901edc, 0x007b46c8, 0x00901e8d, 0x01); }

NglHashedName* XB_THISCALL cdGun_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdGun");
}

void XB_THISCALL cdGun_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                 const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x01)) return;
    add_opaque(make_node(0x007b46c0, node, section, material), class_id(0x00901edc));
}

void XB_CDECL cdGlass_Toggle() { toggle(0x00901e8c, 0x40); }

void XB_CDECL cdGlass_Init() { init_class(0x00901ee4, 0x007b4990, 0x00901e8c, 0x40); }

NglHashedName* XB_THISCALL cdGlass_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdGlass");
}

void XB_THISCALL cdGlass_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    // Not into the target glass itself samples (global 0x0095f6c8).
    if (current_scene()->render_target == title_global<NglRenderTarget*>(0x0095f6c8)) return;
    if (disabled(0x00901e8c, 0x40)) return;
    NglMeshNode* item = make_node(0x007b49bc, node, section, material);
    add_translucent(item, nglNode_ViewDepth(item));
}

void XB_CDECL cdSimpleAlpha_Toggle() { toggle(0x00901e8e, 0x01); }

void XB_CDECL cdSimpleAlpha_Init() { init_class(0x00901eec, 0x007b4c78, 0x00901e8e, 0x01); }

NglHashedName* XB_THISCALL cdSimpleAlpha_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdSimpleAlpha");
}

void XB_THISCALL cdSimpleAlpha_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                         const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x01)) return;
    NglMeshNode* item = make_node(0x007b4c70, node, section, material);
    add_translucent(item, nglNode_ViewDepth(item));
}

void XB_CDECL cdPropeller_Toggle() { toggle(0x00901e8d, 0x40); }

void XB_CDECL cdPropeller_Init() { init_class(0x00901ef4, 0x007b4f58, 0x00901e8d, 0x40); }

NglHashedName* XB_THISCALL cdPropeller_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdPropeller");
}

void XB_THISCALL cdPropeller_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                       const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x40)) return;
    NglMeshNode* item = make_node(0x007b4f50, node, section, material);
    add_translucent(item, nglNode_ViewDepth(item));
}

void XB_CDECL cdDecal_Toggle() { toggle(0x00901e8c, 0x10); }

void XB_CDECL cdDecal_Init() { init_class(0x00901f00, 0x007b5228, 0x00901e8c, 0x10); }

NglHashedName* XB_THISCALL cdDecal_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdDecal");
}

void XB_THISCALL cdDecal_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8c, 0x10)) return;
    // Bit 31: decals sort after the other opaque sections.
    add_opaque(make_node(0x007b5220, node, section, material), class_id(0x00901f00) | 0x80000000);
}

void XB_CDECL cdChar_Toggle() { toggle(0x00901e8c, 0x04); }

void XB_CDECL cdChar_Init() { init_class(0x00901f08, 0x007b54f8, 0x00901e8c, 0x04); }

NglHashedName* XB_THISCALL cdChar_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdChar");
}

void XB_THISCALL cdChar_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                  const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8c, 0x04)) return;
    add_opaque(make_node(0x007b54f0, node, section, material), class_id(0x00901f08));
}

void XB_CDECL cdPrelit_Toggle() { toggle(0x00901e8d, 0x20); }

void XB_CDECL cdPrelit_Init() { init_class(0x00901f10, 0x007b57c8, 0x00901e8d, 0x20); }

NglHashedName* XB_THISCALL cdPrelit_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdPrelit");
}

void XB_THISCALL cdPrelit_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                    const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x20)) return;
    add_opaque(make_node(0x007b57c0, node, section, material), class_id(0x00901f10));
}

void XB_CDECL cdAirplaneMetal_Toggle() { toggle(0x00901e8c, 0x01); }

void XB_CDECL cdAirplaneMetal_Init() { init_class(0x00901f18, 0x007b5a98, 0x00901e8c, 0x01); }

NglHashedName* XB_THISCALL cdAirplaneMetal_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdAirplaneMetal");
}

void XB_THISCALL cdAirplaneMetal_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                           const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8c, 0x01)) return;
    add_opaque(make_node(0x007b5a90, node, section, material), class_id(0x00901f18));
}

void XB_CDECL cdSimpleUVAnim_Toggle() { toggle(0x00901e8e, 0x08); }

void XB_CDECL cdSimpleUVAnim_Init() { init_class(0x00901f20, 0x007b5d78, 0x00901e8e, 0x08); }

NglHashedName* XB_THISCALL cdSimpleUVAnim_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdSimpleUVAnim");
}

void XB_THISCALL cdSimpleUVAnim_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                          const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x08)) return;
    add_opaque(make_node(0x007b5d70, 0x60, node, section, material), class_id(0x00901f20));
}

void XB_CDECL cdSimplePrelit_Toggle() { toggle(0x00901e8e, 0x04); }

void XB_CDECL cdSimplePrelit_Init() { init_class(0x00901f28, 0x007b6058, 0x00901e8e, 0x04); }

NglHashedName* XB_THISCALL cdSimplePrelit_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdSimplePrelit");
}

void XB_THISCALL cdSimplePrelit_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                          const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x04)) return;
    add_opaque(make_node(0x007b6050, node, section, material), class_id(0x00901f28));
}

void XB_CDECL cdSimple_Toggle() { toggle(0x00901e8d, 0x80); }

void XB_CDECL cdSimple_Init() { init_class(0x00901f30, 0x007b6338, 0x00901e8d, 0x80); }

NglHashedName* XB_THISCALL cdSimple_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdSimple");
}

void XB_THISCALL cdSimple_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                    const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x80)) return;
    add_opaque(make_node(0x007b6330, node, section, material), class_id(0x00901f30));
}

void XB_CDECL cdRiver_Toggle() { toggle(0x00901e8d, 0x10); }

void XB_CDECL cdRiver_Init() {
    // cdOcean's constructor first: its vtable and enable bit, then cdRiver's.
    NglShaderClass* self = new_class(0x00901f38);
    if (!self) return;
    self->vtable = class_vtable(0x007b6638);
    title_global<uint8_t>(0x00901e8d) &= ~0x04;
    self->vtable = class_vtable(0x007b6664);
    title_global<uint8_t>(0x00901e8d) &= ~0x10;
}

NglHashedName* XB_THISCALL cdRiver_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdRiver");
}

void XB_THISCALL cdRiver_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x10)) return;
    add_opaque(make_node(0x007b661c, node, section, material), 0xfffffffe);
}

void XB_CDECL cdOcean_Toggle() { toggle(0x00901e8d, 0x04); }

void XB_CDECL cdOcean_Init() {
    init_class(0x00901f40, 0x007b6638, 0x00901e8d, 0x04);
    title_function<void XB_CDECL()>(0x00044eb0)();
}

namespace {

// Wave set row words -> tweak globals; 0 = no global (stored as 0, the 1.0 pair below).
constexpr uint32_t kWaveSetGlobals[0x34] = {
    0x00902134, 0x0089e6dc, 0x0089e6e0, 0,          0x00902138, 0x0090213c, 0x00902140, 0x00902144,
    0x00902148, 0x0090214c, 0,          0,          0x0089e6e4, 0x0089e6e8, 0x0089e6ec, 0x0089e6f0,
    0x0089e6f4, 0x0089e6f8, 0,          0,          0x00902150, 0x00902154, 0x00902158, 0x0090215c,
    0x00902160, 0x00902164, 0x00902168, 0x0090216c, 0x00902170, 0x00902174, 0x00902178, 0x0090217c,
    0x0089e6fc, 0x0089e700, 0x0089e704, 0x0089e708, 0x0089e70c, 0x0089e710, 0x0089e714, 0x0089e718,
    0x00902180, 0x00902184, 0x00902188, 0x0090218c, 0x00902190, 0x00902194, 0x00902198, 0x0090219c,
    0x0089e71c, 0x0089e720, 0x0089e724, 0x0089e728,
};

uint32_t* wave_set(uint32_t index) { return &title_global<uint32_t>(0x00901b30 + index * 0xd0); }

}  // namespace

void XB_REGS1 XB_CDECL cdOcean_LoadWaveSet(uint32_t index) {
    const uint32_t* set = wave_set(index);
    for (uint32_t i = 0; i < 0x34; ++i)
        if (kWaveSetGlobals[i]) title_global<uint32_t>(kWaveSetGlobals[i]) = set[i];
}

void XB_REGS1 XB_CDECL cdOcean_StoreWaveSet(uint32_t index) {
    uint32_t* set = wave_set(index);
    for (uint32_t i = 0; i < 0x34; ++i) set[i] = kWaveSetGlobals[i] ? title_global<uint32_t>(kWaveSetGlobals[i]) : 0;
    set[0x12] = 0x3f800000;  // 1.0f
    set[0x13] = 0x3f800000;
}

NglHashedName* XB_THISCALL cdOcean_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdOcean");
}

void XB_THISCALL cdOcean_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8d, 0x04)) return;
    add_opaque(make_node(0x007b6600, node, section, material), 0xfffffffe);
}

void XB_CDECL cdWater_Toggle() { toggle(0x00901e8e, 0x20); }

void XB_CDECL cdWater_Init() { init_class(0x00901f48, 0x007b6bd8, 0x00901e8e, 0x20); }

NglHashedName* XB_THISCALL cdWater_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdWater");
}

void XB_THISCALL cdWater_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x20)) return;
    add_opaque(make_node(0x007b6bd0, node, section, material), 0xffffffff);
}

void XB_CDECL cdWorldBlend_Toggle() { toggle(0x00901e8e, 0x40); }

void XB_CDECL cdWorldBlend_Init() { init_class(0x00901f50, 0x007b6ea8, 0x00901e8e, 0x40); }

NglHashedName* XB_THISCALL cdWorldBlend_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdWorldBlend");
}

void XB_THISCALL cdWorldBlend_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                        const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x40)) return;
    if (!section_in_frustum(node, section)) return;
    add_opaque(make_node(0x007b6ea0, node, section, material), class_id(0x00901f50));
}

void XB_CDECL cdWorldVertexLit_Toggle() { toggle(0x00901e8f, 0x01); }

void XB_CDECL cdWorldVertexLit_Init() { init_class(0x00901f60, 0x007b7198, 0x00901e8f, 0x01); }

NglHashedName* XB_THISCALL cdWorldVertexLit_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdWorldVertexLit");
}

void XB_THISCALL cdWorldVertexLit_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                            const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8f, 0x01)) return;
    add_opaque(make_node(0x007b7190, node, section, material), class_id(0x00901f60));
}

void XB_CDECL cdWorld_Toggle() { toggle(0x00901e8e, 0x80); }

void XB_CDECL cdWorld_Init() { init_class(0x00901f68, 0x007b7478, 0x00901e8e, 0x80); }

NglHashedName* XB_THISCALL cdWorld_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdWorld");
}

void XB_THISCALL cdWorld_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x80)) return;
    if (!section_in_frustum(node, section)) return;
    NglMeshNode* item = make_node(0x007b7470, 0x1c, node, section, material);
    reinterpret_cast<uint32_t*>(item)[6] = 0;
    add_opaque(item, class_id(0x00901f68));
}

void XB_CDECL cdBackground_Toggle() { toggle(0x00901e8c, 0x02); }

void XB_CDECL cdBackground_Init() { init_class(0x00901f78, 0x007b7748, 0x00901e8c, 0x02); }

NglHashedName* XB_THISCALL cdBackground_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdBackground");
}

void XB_THISCALL cdBackground_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                        const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8c, 0x02)) return;
    add_opaque(make_node(0x007b7740, node, section, material), class_id(0x00901f78));
}

void XB_CDECL cdSky_Toggle() { toggle(0x00901e8e, 0x10); }

void XB_CDECL cdSky_Init() { init_class(0x00901f80, 0x007b7a20, 0x00901e8e, 0x10); }

NglHashedName* XB_THISCALL cdSky_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "cdSky");
}

void XB_THISCALL cdSky_AddToList(const NglShaderClass*, NglMeshDrawNode* node,
                                 const NglMeshSection* section, const uint8_t* material) {
    if (disabled(0x00901e8e, 0x10)) return;
    // Material +0x30: the sky layer. Translucent lists draw larger keys first, so
    // higher layers go on top.
    const int32_t layer = *reinterpret_cast<const int32_t*>(material + 0x30);
    const float key = title_global<float>(0x0083eb9c) - static_cast<float>(layer);  // 1000
    add_translucent(make_node(0x007b7a50, node, section, material), key);
}

NglHashedName* XB_THISCALL nglDebugShader_GetName(const NglShaderClass*, NglHashedName* out) {
    return hashed_name(out, "Debug");
}

void XB_CDECL cdGlass_InitVertexShaders(uint32_t declaration) {
    init_vertex_shaders(0x00901fd8, 0x0089a6dc, 4, declaration);
}

void XB_CDECL cdGlass_InitPixelShaders() { init_pixel_shaders(0x00901fd0, 0x0089a8d0, 2); }

void XB_CDECL cdSimpleAlpha_InitVertexShaders(uint32_t declaration) {
    init_vertex_shaders(0x00902000, 0x0089abdc, 2, declaration);
}

void XB_CDECL cdSimpleAlpha_InitPixelShaders() { init_pixel_shaders(0x00902010, 0x0089adc8, 2); }

void XB_CDECL cdSimpleAlpha_InitPixelShaders2() { init_pixel_shaders(0x00902018, 0x0089afb0, 2); }

void XB_CDECL cdRiver_InitPixelShaders() { init_pixel_shaders(0x009020c8, 0x0089d8f8, 8); }

void XB_CDECL cdOcean_InitPixelShaders() { init_pixel_shaders(0x009020f4, 0x0089e588, 8); }

void XB_CDECL cdWorldBlend_InitVertexShaders(uint32_t declaration) {
    init_vertex_shaders(0x009021b4, 0x0089ec3c, 2, declaration);
}

void XB_CDECL cdWorldBlend_InitVertexShaders2(uint32_t declaration) {
    init_vertex_shaders(0x009021c4, 0x0089f084, 2, declaration);
}

void XB_CDECL cdWorldBlend_InitPixelShaders() { init_pixel_shaders(0x009021d4, 0x0089f450, 4); }

void XB_CDECL cdWorldBlend_InitPixelShaders2() { init_pixel_shaders(0x009021e4, 0x0089f640, 2); }

void XB_CDECL cdWorld_InitVertexShaders(uint32_t declaration) {
    init_vertex_shaders(0x00902208, 0x0089fc4c, 2, declaration);
}

void XB_CDECL cdWorld_InitVertexShaders2(uint32_t declaration) {
    init_vertex_shaders(0x0090221c, 0x008a0094, 2, declaration);
}

void XB_CDECL cdWorld_InitPixelShaders() { init_pixel_shaders(0x0090222c, 0x008a0460, 4); }

void XB_CDECL cdWorld_InitPixelShaders2() { init_pixel_shaders(0x0090223c, 0x008a0650, 2); }

namespace {

uint8_t* state_cache() { return &title_global<uint8_t>(0x00902f08); }

// The state cache's cull mode (+0xa4), through SetRenderState_CullMode.
void set_cull_mode(uint32_t mode) {
    uint32_t& cached = title_global<uint32_t>(0x00902fac);
    if (cached == mode) return;
    xb::set_render_state(0x003e80f0, mode);
    cached = mode;
}

// The scene's fog colour as a D3DCOLOR (each component * 255, truncated; ORed in
// whole, as the title does) through the cache (+0x8c), and fog on or off (+0x8b;
// D3D's D3DRS_FOGENABLE and its dirty bit).
void set_scene_fog(uint8_t enable = 1) {
    const float* c = current_scene()->fog_color;
    const float scale = title_global<float>(0x0083eb8c);  // 255
    const float r = c[0] * scale, g = c[1] * scale, b = c[2] * scale, a = c[3] * scale;
    uint32_t color = static_cast<uint32_t>(platform_ftol(a));
    color = color << 8 | static_cast<uint32_t>(platform_ftol(r));
    color = color << 8 | static_cast<uint32_t>(platform_ftol(g));
    color = color << 8 | static_cast<uint32_t>(platform_ftol(b));
    uint32_t& cached_color = title_global<uint32_t>(0x00902f94);
    if (cached_color != color) {
        xb::set_render_state(0x003e8090, color);  // SetRenderState_FogColor
        cached_color = color;
    }
    uint8_t& fog_enabled = title_global<uint8_t>(0x00902f93);
    if (fog_enabled != enable) {
        title_global<uint32_t>(0x003f8320) |= 0x2000;
        xb::set_d3d_state(0x003f8498, enable);
        fog_enabled = enable;
    }
}

NglMeshDrawNode* draw_node(const NglMeshNode* node) {
    return reinterpret_cast<NglMeshDrawNode*>(const_cast<float*>(node->world_matrix));
}

void set_transform_constants(int32_t slot, const NglMeshNode* node) {
    const float* fog = current_scene()->fog;
    nglSetTransformConstants(slot, draw_node(node), fog[0], fog[1], fog[2], fog[3]);
}

// Vertex constants 6..9: the node's world * view * projection (transposed).
void set_world_view_projection(const NglMeshNode* node) {
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    xb::set_vertex_constants(6, draw_node(node)->world_view_projection, 16);  // NotInlineFast
}

// The material's texture (+0x1c) on stage 0, and stage 0's texture states marked dirty.
void set_material_texture(const NglMeshNode* node) {
    nglSetTexture(0, *reinterpret_cast<NglTexture* const*>(node->material + 0x1c), 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1;
    xb::set_d3d_state(0x003f5c68, 1);
    xb::set_d3d_state(0x003f5c6c, 1);
}

const NglShaderRef* shader_ref(uint32_t address) { return &title_global<NglShaderRef>(address); }

// The pixel program variant by the debug view mode.
const NglShaderRef* pick_shader(uint32_t if_one, uint32_t otherwise) {
    return shader_ref(R_GetDebugViewMode() == 1 ? if_one : otherwise);
}

// The instance parameter block (the draw node's +0x8c): a 64-bit mask of the slots
// present, then one word per slot.
const uint32_t* instance_params(const NglMeshNode* node) {
    return *reinterpret_cast<const uint32_t* const*>(reinterpret_cast<const uint8_t*>(node->world_matrix) + 0x8c);
}

// Whether the mask has `slot`: the bit as a sign-extended 32-bit shift (cdq), as the
// title tests it.
bool has_param(const uint32_t* params, uint32_t slot) {
    const uint32_t low = 1u << (slot & 31);
    const uint32_t high = static_cast<int32_t>(low) < 0 ? 0xffffffff : 0;
    return (params[0] & low) == low && (params[1] & high) == high;
}

// Constants 6..17: the matrix (as set_world_view_projection), then the node's four
// object lights: directions, colours (r, g, b per light) and the ambient.
void set_object_lights(const NglMeshNode* node) {
    alignas(16) float constants[48];
    NglMeshDrawNode* draw = draw_node(node);
    nglSelectMeshLightSet(draw);
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    std::memcpy(constants, draw->world_view_projection, 64);
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    xb::set_vertex_constants(6, constants, 48);  // NotInlineFast
}

// After states set behind nglSetBlendMode's back: its cache no longer applies.
void reset_blend_cache() {
    title_global<uint32_t>(0x00902ff0) = 9;
    title_global<uint32_t>(0x00902ff4) = 0xffffffff;
    title_global<uint32_t>(0x00902ff8) = 0xffffffff;
}

}  // namespace

void XB_THISCALL cdWorldVertexLit_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetAlphaRef(cache, 0);
    nglStateCache_SetAlphaFunc(cache, 0x206);
    set_cull_mode(0);
    nglSetBlendMode(cache, 2, 0, 0);
    set_transform_constants(-0x4e, node);
    set_scene_fog();
    set_world_view_projection(node);
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(0x009021f8));
    nglSetPixelShaderProgramCached(pick_shader(0x009021f4, 0x00902200));
    nglDrawMeshSection(node->section);
    reset_blend_cache();
}

void XB_THISCALL cdBackground_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 1);
    nglStateCache_SetAlphaRef(cache, 0x80);
    nglStateCache_SetAlphaFunc(cache, 0x206);
    nglStateCache_SetBlendEnable(cache, 0);
    set_cull_mode(0);
    set_world_view_projection(node);
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(0x0090224c));
    nglSetPixelShaderProgramCached(pick_shader(0x00902254, 0x00902248));
    set_transform_constants(-0x4e, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
    reset_blend_cache();
}

void XB_THISCALL cdSimplePrelit_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    set_cull_mode(*reinterpret_cast<const int32_t*>(node->material + 0x20) == 2 ? 0 : 0x900);
    nglStateCache_SetBlendEnable(cache, 0);
    nglStateCache_SetAlphaTestEnable(cache, 1);
    nglStateCache_SetAlphaFunc(cache, 0x204);
    nglStateCache_SetAlphaRef(cache, 0x80);
    set_world_view_projection(node);
    nglSetVertexShaderCached(shader_ref(0x009020a4));
    set_material_texture(node);
    nglSetPixelShaderProgramCached(shader_ref(0x009020a0));
    set_transform_constants(-0x56, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
    reset_blend_cache();
}

void XB_THISCALL cdPrelit_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetAlphaRef(cache, 0);
    nglStateCache_SetAlphaFunc(cache, 0x206);
    nglStateCache_SetBlendEnable(cache, 0);
    set_cull_mode(0);
    set_material_texture(node);
    if (NglTexture* second = *reinterpret_cast<NglTexture* const*>(node->material + 0x24)) {
        nglSetTexture(1, second, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 2;
        xb::set_d3d_state(0x003f5ce8, 3);
        xb::set_d3d_state(0x003f5cec, 3);
    }
    nglSetVertexShaderCached(shader_ref(0x00902064));
    nglSetPixelShaderProgramCached(pick_shader(0x00902070, 0x00902060));
    set_world_view_projection(node);
    set_transform_constants(-0x56, node);
    set_scene_fog(0);
    nglDrawMeshSection(node->section);
    reset_blend_cache();
}

void XB_THISCALL nglSetTextureAnimFrame(const NglMeshNode* node) {
    const uint32_t slot = title_global<uint32_t>(0x00902918);
    const uint32_t* params = instance_params(node);
    uint32_t frame;
    if (has_param(params, slot)) {
        // The title tests the mask twice; the second test's failure (impossible) would
        // take the node's address.
        frame = has_param(params, slot) ? params[2 + slot] : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    } else {
        frame = current_scene()->frame_counter;
    }
    title_global<uint32_t>(0x009026fc) = frame;
}

void XB_THISCALL ShaderPCUV_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    nglSetTextureAnimFrame(node);
    const uint32_t* params = instance_params(node);
    if (has_param(params, title_global<uint32_t>(0x0090291c))) {
        const float bias = reinterpret_cast<const float*>(params)[2 + title_global<uint32_t>(0x0090291c)];
        nglSetDecalZBias(cache, static_cast<uint32_t>(platform_ftol(bias)));
    }
    set_cull_mode(0);
    nglSetBlendMode(cache, *reinterpret_cast<const uint32_t*>(node->material + 0x20), 0, 0);
    set_world_view_projection(node);
    nglSetVertexShaderCached(shader_ref(0x00901f98));
    const uint32_t address_flags = *reinterpret_cast<const uint32_t*>(node->material + 0x24);
    nglSetTexture(0, *reinterpret_cast<NglTexture* const*>(node->material + 0x1c), static_cast<uint8_t>(address_flags), 3);
    const uint8_t flags = node->material[0x24];
    title_global<uint32_t>(0x003f8320) |= 1;
    xb::set_d3d_state(0x003f5c68, (flags & 0x40) ? 3 : 1);
    xb::set_d3d_state(0x003f5c6c, (flags & 0x80) ? 3 : 1);
    title_global<uint32_t>(0x003f8320) |= 1;
    nglSetPixelShaderProgramCached(shader_ref(0x00901fa0));
    set_transform_constants(-0x56, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
    nglSetDecalZBias(cache, 0);
}

namespace {

void draw_lit_alpha_tested(const NglMeshNode* node, uint32_t vertex_shader, uint32_t pixel_shader_if_one,
                           uint32_t pixel_shader) {
    uint8_t* cache = state_cache();
    set_cull_mode(*reinterpret_cast<const int32_t*>(node->material + 0x20) == 2 ? 0 : 0x900);
    nglStateCache_SetBlendEnable(cache, 0);
    nglStateCache_SetAlphaTestEnable(cache, 1);
    nglStateCache_SetAlphaFunc(cache, 0x204);
    nglStateCache_SetAlphaRef(cache, 0x80);
    reset_blend_cache();
    set_object_lights(node);
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(vertex_shader));
    nglSetPixelShaderProgramCached(pick_shader(pixel_shader_if_one, pixel_shader));
    set_transform_constants(-0x4e, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

}  // namespace

void XB_THISCALL cdGunSight_Draw(const NglMeshNode* node) { draw_lit_alpha_tested(node, 0x00901fac, 0x00901fa8, 0x00901fb4); }

void XB_THISCALL cdGun_Draw(const NglMeshNode* node) { draw_lit_alpha_tested(node, 0x00901fc0, 0x00901fc8, 0x00901fbc); }

void XB_THISCALL cdSimple_Draw(const NglMeshNode* node) { draw_lit_alpha_tested(node, 0x009020b8, 0x009020b0, 0x009020b4); }

namespace {

// The lit classes' constant block as set_object_lights fills it: the light set
// picked, the four object lights at +16, their colours and the ambient at +32, the
// matrix at 0.
void compute_object_lights(const NglMeshNode* node, float* constants) {
    NglMeshDrawNode* draw = draw_node(node);
    nglSelectMeshLightSet(draw);
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    std::memcpy(constants, draw->world_view_projection, 64);
}

void set_vertex_constants(uint32_t first, const float* constants, uint32_t count) {
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    xb::set_vertex_constants(first, constants, count);  // NotInlineFast
}

// The alpha test on (0x204, 0x80) without blending, and the blend cache reset.
void set_alpha_tested_opaque() {
    uint8_t* cache = state_cache();
    nglStateCache_SetBlendEnable(cache, 0);
    nglStateCache_SetAlphaTestEnable(cache, 1);
    nglStateCache_SetAlphaFunc(cache, 0x204);
    nglStateCache_SetAlphaRef(cache, 0x80);
    reset_blend_cache();
}

// Alpha blending (ADD, SRCALPHA, INVSRCALPHA) over an alpha test of 0x204 against 0.
void set_alpha_blended_tested() {
    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 1);
    nglStateCache_SetAlphaFunc(cache, 0x204);
    nglStateCache_SetAlphaRef(cache, 0);
    nglStateCache_SetBlendEnable(cache, 1);
    nglStateCache_SetBlendOp(cache, 0x8006);
    nglStateCache_SetSrcBlend(cache, 0x302);
    nglStateCache_SetDestBlend(cache, 0x303);
}

uint32_t two_sided_cull(const uint8_t* material, uint32_t offset) {
    return *reinterpret_cast<const int32_t*>(material + offset) == 2 ? 0 : 0x900;
}

}  // namespace

void XB_THISCALL cdChar_Draw(const NglMeshNode* node) {
    alignas(16) float constants[48];
    set_cull_mode(0);
    set_alpha_tested_opaque();
    NglMeshDrawNode* draw = draw_node(node);
    nglSelectMeshLightSet(draw);
    nglSetTextureAnimFrame(node);
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(0x00902054));
    std::memcpy(constants, draw->world_view_projection, 64);
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    nglSetPixelShaderProgramCached(pick_shader(0x00902050, 0x0090204c));
    set_vertex_constants(6, constants, 48);
    nglSetSkinningConstants(-0x4c, draw, node->section);
    set_transform_constants(-0x4e, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

void XB_THISCALL cdSimpleUVAnim_GetUVParams(const NglMeshNode* node, float* out) {
    const uint32_t slot = title_global<uint32_t>(0x00902908);
    const uint32_t* params = instance_params(node);
    // The title tests the mask twice; the second test's failure (impossible) would
    // read an uninitialized stack slot.
    if (has_param(params, slot) && has_param(params, slot)) {
        if (const uint8_t* object = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(params[2 + slot]))) {
            std::memcpy(out, object + 0x30, 16);
            return;
        }
    }
    std::memcpy(out, &title_global<float>(0x007b5b80), 16);
}

void XB_THISCALL cdSimpleUVAnim_Draw(const NglMeshNode* node) {
    alignas(16) float constants[52];
    set_cull_mode(two_sided_cull(node->material, 0x20));
    set_alpha_tested_opaque();
    compute_object_lights(node, constants);
    cdSimpleUVAnim_GetUVParams(node, constants + 48);
    set_vertex_constants(6, constants, 52);
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(0x00902090));
    nglSetPixelShaderProgramCached(pick_shader(0x00902098, 0x0090208c));
    set_transform_constants(-0x4d, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

void XB_THISCALL cdDebug_Draw(const NglMeshNode* node) {
    // The colour: the instance's (a pointer to four floats in slot 0x00902908), else
    // white with the default alpha.
    alignas(16) float color[4];
    const uint32_t slot = title_global<uint32_t>(0x00902908);
    const uint32_t* params = instance_params(node);
    if (has_param(params, slot) && has_param(params, slot)) {
        std::memcpy(color, reinterpret_cast<const void*>(static_cast<uintptr_t>(params[2 + slot])), 16);
    } else {
        color[0] = color[1] = color[2] = title_global<float>(0x0083eb20);
        color[3] = title_global<float>(0x0083eb24);
    }
    set_cull_mode(0);
    nglSetBlendMode(state_cache(), 2, 0, 0);
    uint8_t& fog_enabled = title_global<uint8_t>(0x00902f93);
    if (fog_enabled != 0) {
        title_global<uint32_t>(0x003f8320) |= 0x2000;
        xb::set_d3d_state(0x003f8498, 0);
        fog_enabled = 0;
    }
    set_world_view_projection(node);
    using SetConstant1 = void XB_FASTCALL(uint32_t, const float*);
    xb::set_vertex_constant(0xc, color);  // SetVertexShaderConstant1Fast
    nglSetVertexShaderCached(shader_ref(0x00901f88));
    nglSetPixelShaderProgramCached(shader_ref(0x00901f90));
    set_transform_constants(-0x56, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

void XB_THISCALL cdDecal_Draw(const NglMeshNode* node) {
    alignas(16) float constants[16];
    uint8_t* cache = state_cache();
    set_cull_mode(two_sided_cull(node->material, 0x24));
    set_alpha_blended_tested();
    nglSetDecalZBias(cache, 10);
    reset_blend_cache();
    // The matrix's depth row pulled toward the camera by the material's offset (+0x20)
    // over the view depth, at least 0x0083ec9c (NaN depths stay).
    const float depth = nglNode_ViewDepth(node);
    const float min_depth = title_global<float>(0x0083ec9c);
    const float d = min_depth > depth ? min_depth : depth;
    std::memcpy(constants, draw_node(node)->world_view_projection, 64);
    const float offset = *reinterpret_cast<const float*>(node->material + 0x20) / d * title_global<float>(0x0083f010);
    constants[11] = constants[11] - offset;
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(0x00902040));
    nglSetPixelShaderProgramCached(pick_shader(0x00902038, 0x0090203c));
    set_vertex_constants(6, constants, 16);
    set_transform_constants(-0x4e, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
    nglSetDecalZBias(cache, 0);
}

void XB_THISCALL cdWater_Draw(const NglMeshNode* node) {
    alignas(16) float constants[20];
    title_global<uint32_t>(0x009026fc) = current_scene()->frame_counter;
    set_cull_mode(0x900);
    set_alpha_blended_tested();
    // Constant 10: the material's scroll speeds (+0x28, +0x2c) times the time
    // (0x00901e90), fractional parts only.
    const uint8_t* material = node->material;
    std::memcpy(constants, draw_node(node)->world_view_projection, 64);
    const float time = title_global<float>(0x00901e90);
    const float u = *reinterpret_cast<const float*>(material + 0x28) * time;
    const float v = *reinterpret_cast<const float*>(material + 0x2c) * time;
    constants[19] = 0;
    constants[18] = 0;
    constants[17] = v - static_cast<float>(static_cast<int32_t>(v));
    constants[16] = u - static_cast<float>(static_cast<int32_t>(u));
    set_vertex_constants(6, constants, 20);
    set_material_texture(node);
    NglTexture* second = *reinterpret_cast<NglTexture* const*>(material + 0x24);
    nglSetTexture(1, second, 2, 3);
    title_global<uint32_t>(0x003f8320) |= 2;
    xb::set_d3d_state(0x003f5ce8, 3);
    xb::set_d3d_state(0x003f5cec, 3);
    nglSetVertexShaderCached(shader_ref(0x009021a4));
    const bool reflective = *reinterpret_cast<NglTexture* const*>(node->material + 0x24) != nullptr &&
                            R_GetDebugViewMode() != 1;
    nglSetPixelShaderProgramCached(shader_ref(reflective ? 0x009021a0 : 0x009021ac));
    set_transform_constants(-0x56, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

namespace {

Lanes splat(float x) { return {{x, x, x, x}}; }

// The nearest integer per lane through 1.5 * 2^23 (0x00898ea0): (v + magic) - magic.
Lanes round_lanes(const Lanes& v) {
    const Lanes magic = lanes(&title_global<float>(0x00898ea0));
    return sub(add(v, magic), magic);
}

// MAXPS / MINPS: the second operand when either is NaN.
Lanes max_lanes(const Lanes& a, const Lanes& b) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return r;
}

Lanes min_lanes(const Lanes& a, const Lanes& b) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return r;
}

// 1 / v per lane: RCPPS and one Newton step (2.0 from 0x007b6710).
Lanes reciprocal_lanes(const Lanes& v) {
    Lanes estimate;
    for (int i = 0; i < 4; ++i) estimate.v[i] = platform_rcp_estimate(v.v[i]);
    return mul(sub(lanes(&title_global<float>(0x007b6710)), mul(estimate, v)), estimate);
}

Lanes sine_lanes(const Lanes& angles) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = scene_sine_poly(angles.v[i]);
    return r;
}

// Rows 0..2 of a matrix with lane 3 replaced.
void store_row_w(float* out, const float* row, float w) {
    out[0] = row[0];
    out[1] = row[1];
    out[2] = row[2];
    out[3] = w;
}

void store_lanes(float* out, const Lanes& v) { std::memcpy(out, v.v, 16); }

}  // namespace

void XB_THISCALL cdOcean_SetWaveConstants(const NglMeshNode* node) {
    const float one = title_global<float>(0x0083eb20);
    const float wave_scale = title_global<float>(0x00901e90);
    const float quarter = title_global<float>(0x0083ec24);
    int32_t& selected = title_global<int32_t>(0x00902128);
    if (selected >= 4) selected = 3;
    const int32_t index = selected;
    const int32_t material_set = *reinterpret_cast<const int32_t*>(node->material + 0x3c);
    // The wave set's rows; rows 1 and 2 feed three offset vectors.
    Lanes r[13];
    Lanes offset_a, offset_b, offset_c;
    alignas(16) float pixel[4];
    float wave_z;
    if (title_global<uint32_t>(0x00902124) && index == material_set) {
        if (index != title_global<int32_t>(0x0089e6d0)) {
            title_global<int32_t>(0x0089e6d0) = index;
            cdOcean_LoadWaveSet(index);
        } else {
            cdOcean_StoreWaveSet(index);
        }
        if (title_global<uint32_t>(0x0090212c)) title_global<uint32_t>(0x0090212c) = 0;
        const auto g = [](uint32_t address) { return title_global<float>(address); };
        const auto row = [](uint32_t address) { return lanes(&title_global<float>(address)); };
        wave_z = g(0x0089e6e0);
        const float z = wave_z * quarter;
        r[0] = {{g(0x00902134), g(0x0089e6dc), z, wave_scale}};
        pixel[0] = 0.0f;
        pixel[1] = one;
        pixel[2] = z;
        pixel[3] = g(0x0089e6dc);
        offset_a = {{g(0x00902138), g(0x0090213c), 0.0f, 0.0f}};
        offset_b = {{g(0x00902140), g(0x00902144), 0.0f, 0.0f}};
        offset_c = {{g(0x00902148), g(0x0090214c), 0.0f, 0.0f}};
        r[3] = row(0x0089e6e4);
        r[4] = {{g(0x0089e6f4), g(0x0089e6f8), one, one}};
        r[5] = row(0x00902150);
        r[6] = row(0x00902160);
        r[7] = row(0x00902170);
        r[8] = row(0x0089e6fc);
        r[9] = row(0x0089e70c);
        r[10] = row(0x00902180);
        r[11] = row(0x00902190);
        r[12] = row(0x0089e71c);
    } else {
        if (index == material_set) title_global<int32_t>(0x0089e6d0) = -1;
        const float* set = &title_global<float>(0x00901b30 + material_set * 0xd0);
        for (int i = 0; i < 13; ++i) r[i] = lanes(set + i * 4);
        // Row 2 twice (the tweak path has row 1's zw in the middle one).
        offset_a = r[1];
        offset_b = r[2];
        offset_c = r[2];
        const Lanes quarters = mul(r[0], quarter);
        platform_keep(quarters);
        pixel[0] = 0.0f;
        pixel[1] = one;
        pixel[2] = quarters.v[2];
        pixel[3] = r[0].v[1];
        wave_z = r[0].v[2];
    }

    NglMeshDrawNode* draw = draw_node(node);
    alignas(16) float c[25 * 4];
    std::memcpy(c, draw->world_view_projection, 64);
    const float w0 = title_global<float>(0x007b6760);  // 0
    const float w1 = title_global<float>(0x007b6770);  // 1
    for (int i = 0; i < 4; ++i) store_row_w(c + 16 + i * 4, draw->world + i * 4, i < 3 ? w0 : w1);
    alignas(16) float inverse[16];
    nglNode_GetScaledWorldInverse(draw, inverse);
    for (int i = 0; i < 4; ++i) store_row_w(c + 32 + i * 4, inverse + i * 4, i < 3 ? w0 : w1);
    store_lanes(c + 48, r[0]);
    // The fractional wave offsets, less the section's rounded offset (+0x10).
    const Lanes s = splat(wave_scale);
    const Lanes a = sub(mul(offset_a, s), round_lanes(offset_a));
    const Lanes b = sub(mul(offset_b, s), round_lanes(offset_b));
    const Lanes cc = sub(mul(offset_c, s), round_lanes(offset_c));
    const Lanes ones = splat(one);
    const Lanes inverse3 = reciprocal_lanes(max_lanes(ones, r[3]));
    const Lanes inverse4 = reciprocal_lanes(max_lanes(ones, r[4]));
    const Lanes section_offset =
        round_lanes(lanes(reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(node->section) + 0x10)));
    const Lanes da = sub(a, section_offset);
    const Lanes db = sub(b, section_offset);
    const Lanes dc = sub(cc, section_offset);
    store_lanes(c + 52, {{inverse3.v[0], inverse3.v[1], da.v[0], da.v[1]}});
    store_lanes(c + 56, {{inverse3.v[2], inverse3.v[3], db.v[0], db.v[1]}});
    store_lanes(c + 60, {{inverse4.v[0], inverse4.v[1], dc.v[0], dc.v[1]}});
    store_lanes(c + 64, r[5]);
    store_lanes(c + 68, r[6]);
    // Row 7 in degrees: sin and sin(+ 3 pi / 2) of each.
    const Lanes angles = mul(r[7], splat(title_global<float>(0x0083edd8)));
    const float phase = title_global<float>(0x0083efe0);  // 3 pi / 2
    const Lanes phases = {{phase, 0.0f, phase, 0.0f}};
    store_lanes(c + 72, sine_lanes(add({{angles.v[0], angles.v[0], angles.v[1], angles.v[1]}}, phases)));
    store_lanes(c + 76, sine_lanes(add({{angles.v[2], angles.v[2], angles.v[3], angles.v[3]}}, phases)));
    store_lanes(c + 80, reciprocal_lanes(max_lanes(ones, r[8])));
    store_lanes(c + 84, mul(splat(title_global<float>(0x0083edac)), reciprocal_lanes(max_lanes(ones, r[9]))));  // 2 pi
    store_lanes(c + 88, r[10]);
    const Lanes blend = min_lanes(ones, max_lanes(splat(0.0f), round_lanes(r[10])));
    store_lanes(c + 92, mul(mul(add(r[11], mul(r[12], s)), splat(title_global<float>(0x0083eba8))),  // 2
                            splat(title_global<float>(0x007b6800))));                                // pi
    store_lanes(c + 96, mul(mul(splat(wave_z), blend), splat(quarter)));
    using GetSlot = int32_t XB_THISCALL(const NglMeshNode*);
    const int32_t slot = reinterpret_cast<GetSlot*>(node->vtable[6])(node);
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    xb::set_vertex_constants(slot + 0x60, c, 100);  // NotInlineFast
    xb::set_pixel_constants(0, pixel, 1);  // SetPixelShaderConstant
}

void XB_THISCALL cdOcean_Draw(const NglMeshNode* node) {
    title_global<uint32_t>(0x009026fc) = current_scene()->frame_counter;
    set_cull_mode(0x900);
    // Alpha test and blending off, D3D's own render state copies too.
    uint8_t* cache = state_cache();
    if (cache[0x50] != 0) {
        xb::set_render_state_simple(0x00040300, 0);  // SetRenderState_Simple
        xb::set_d3d_state(0x003f8418, 0);
        cache[0x50] = 0;
    }
    if (cache[0x5c] != 0) {
        xb::set_render_state_simple(0x00040304, 0);
        xb::set_d3d_state(0x003f8414, 0);
        cache[0x5c] = 0;
    }
    // Stages 1..3 (bits 0..2): the material's textures at +0x28/+0x30/+0x38. With the
    // global wave set (0x00902124) the bits come from the globals 0x0089e6d4/d8 and
    // 0x00902130, else from the material (+0x18); stage 3 needs its texture either way.
    const uint8_t* material = node->material;
    const bool has_stage3 = *reinterpret_cast<const uint32_t*>(material + 0x38) != 0;
    uint32_t stages = 0;
    if (title_global<uint32_t>(0x00902124) != 0) {
        if (title_global<uint32_t>(0x0089e6d4) != 0) stages = 1;
        if (title_global<uint32_t>(0x0089e6d8) != 0) stages |= 2;
        if (has_stage3 && title_global<uint32_t>(0x00902130) != 0) stages |= 4;
    } else {
        stages = *reinterpret_cast<const uint32_t*>(material + 0x18);
        if (!has_stage3) stages &= ~4u;
    }
    nglSetTexture(0, *reinterpret_cast<NglTexture* const*>(material + 0x20), 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1;
    xb::set_d3d_state(0x003f5c68, 1);
    xb::set_d3d_state(0x003f5c6c, 1);
    if (NglTexture* t = *reinterpret_cast<NglTexture* const*>(node->material + 0x28); t && (stages & 1)) {
        nglSetTexture(1, t, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 2;
        xb::set_d3d_state(0x003f5ce8, 1);
        xb::set_d3d_state(0x003f5cec, 1);
    } else {
        stages &= ~1u;
    }
    if (NglTexture* t = *reinterpret_cast<NglTexture* const*>(node->material + 0x30); t && (stages & 2)) {
        nglSetTexture(2, t, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 4;
        xb::set_d3d_state(0x003f5d68, 1);
        xb::set_d3d_state(0x003f5d6c, 1);
    } else {
        stages &= ~2u;
    }
    if (NglTexture* t = *reinterpret_cast<NglTexture* const*>(node->material + 0x38); t && (stages & 4)) {
        nglSetTexture(3, t, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 8;
        xb::set_d3d_state(0x003f5de8, 3);
        xb::set_d3d_state(0x003f5dec, 3);
    } else {
        stages &= ~4u;
    }
    // The node class's own slots: 2 vertex shader, 3 pixel shader by stages, 4 the
    // other pixel shader variant, 5 the transform slot.
    using GetShader = const NglShaderRef* XB_THISCALL(const NglMeshNode*);
    using GetPixelShader = const NglShaderRef* XB_THISCALL(const NglMeshNode*, uint32_t, uint32_t, uint32_t);
    using GetSlot = int32_t XB_THISCALL(const NglMeshNode*);
    void** vtable = node->vtable;
    nglSetVertexShaderCached(reinterpret_cast<GetShader*>(vtable[2])(node));
    const NglShaderRef* pixel_shader =
        R_GetDebugViewMode() == 1
            ? reinterpret_cast<GetShader*>(vtable[4])(node)
            : reinterpret_cast<GetPixelShader*>(vtable[3])(node, stages & 1, stages >> 1 & 1, stages >> 2 & 1);
    nglSetPixelShaderProgramCached(pixel_shader);
    cdOcean_SetWaveConstants(node);
    const float* fog = current_scene()->fog;
    nglSetTransformConstants(reinterpret_cast<GetSlot*>(vtable[5])(node), draw_node(node), fog[0], fog[1], fog[2],
                             fog[3]);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

}  // namespace game

namespace game {

namespace {

// p * m for a point (w = 1) as the title packs it: ((row 3 + row 0 * x) + row 1 * y)
// + row 2 * z, all four lanes.
Lanes transform_point(const float* m, const float* p) {
    return add(add(add(lanes(m + 12), mul(lanes(m), p[0])), mul(lanes(m + 4), p[1])), mul(lanes(m + 8), p[2]));
}

// Lane 3 replaced by 1 (SHUFPS with a (1, 0, 0, 0) constant of the class).
Lanes with_w_one(Lanes a, uint32_t one) {
    a.v[3] = title_global<float>(one);
    return a;
}

// cdWorld and cdWorldBlend: one draw, with their own textures, constants and shaders.
struct WorldClass {
    uint32_t dynamic_light;  // the "dynamiclight" texture, found on first use
    uint32_t one;            // (1, 0, 0, 0)
    uint32_t w_axis;         // (0, 0, 0, 1)
    // Sets the material's textures and the projector's (null: none); lightmap and
    // projected pick the shaders.
    void (*textures)(const NglMeshNode* node, NglTexture* projector, bool& lightmap, bool& projected);
    uint32_t vertex_shaders, pixel_shaders;  // indexed by projected, + lightmap * 2
    uint32_t light_vertex_shaders[2], light_pixel_shaders[2];  // 3-4 lights, then 1-2
};

// The projector's texture with border addressing and a black border.
void set_projector_texture(uint32_t stage, NglTexture* texture) {
    nglSetTexture(stage, texture, 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1u << stage;
    xb::set_d3d_state(0x003f5c68 + stage * 0x80, 4);
    xb::set_d3d_state(0x003f5c6c + stage * 0x80, 4);
    xb::set_border_color(stage, 0);  // SetTextureState_BorderColor
}

void draw_world(const NglMeshNode* node, const WorldClass& k);

}  // namespace

void XB_THISCALL cdPropeller_Draw(const NglMeshNode* node) {
    // Constant 18 has only its first lane set; the title sends the other three
    // uninitialized (zeros here, and in --compare test processes on both sides).
    alignas(16) float constants[52] = {};
    uint8_t* cache = state_cache();
    set_cull_mode(two_sided_cull(node->material, 0x20));
    nglStateCache_SetBlendEnable(cache, 1);
    nglStateCache_SetSrcBlend(cache, 0x302);
    nglStateCache_SetDestBlend(cache, 0x303);
    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetAlphaRef(cache, 0x80);
    compute_object_lights(node, constants);
    // The blade angle: the material's speed (+0x24) * the time (0x00901e90) * 0x0083edac.
    constants[48] = *reinterpret_cast<const float*>(node->material + 0x24) * title_global<float>(0x00901e90) *
                    title_global<float>(0x0083edac);
    set_vertex_constants(6, constants, 52);
    nglSetTexture(0, *reinterpret_cast<NglTexture* const*>(node->material + 0x1c), 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1;
    xb::set_d3d_state(0x003f5c68, 3);
    xb::set_d3d_state(0x003f5c6c, 3);
    nglSetVertexShaderCached(shader_ref(0x00902024));
    nglSetPixelShaderProgramCached(pick_shader(0x0090202c, 0x00902030));
    set_transform_constants(-0x4a, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
    reset_blend_cache();
}

namespace {

void draw_world(const NglMeshNode* node, const WorldClass& k) {
    // Constants 6..21: 6 (0.5, 2, 0, 0), 7..10 the matrix, 11..13 the projector's
    // matrix columns 0..2 and 14 (0, 0, 0, 1) when a projector reaches the mesh, 15
    // (0x008db198, -, -, -), 16 the fog parameters, 17 the camera in object space,
    // 18..21 up to four light vectors (w = the light's flags). The title leaves
    // whatever it does not set uninitialized (zeros here, and in --compare test
    // processes on both sides).
    alignas(16) float c[64] = {};
    float* const projector_rows = c + 20;
    float* const lights = c + 48;
    NglTexture*& dynamic_light = title_global<NglTexture*>(k.dynamic_light);
    if (!dynamic_light) {
        NglHashedName name;
        hashed_name(&name, "dynamiclight");
        dynamic_light = static_cast<NglTexture*>(nglFindTextureAddRef(reinterpret_cast<const char*>(&name)));
    }
    NglMeshDrawNode* draw = draw_node(node);
    *reinterpret_cast<uint32_t*>(draw->mesh + 4) |= 0x07000000;
    nglSelectMeshLightSet(draw);

    // The mesh's bounding-sphere centre (+0x20) in world space.
    const auto world_center = [draw] { return transform_point(draw->world, reinterpret_cast<const float*>(draw->mesh + 0x20)); };
    int32_t light_count = 0;
    for (NglLight* light = title_global<NglLightSet*>(0x00902e44)->object_lights;
         light != reinterpret_cast<NglLight*>(title_global<NglLightSet*>(0x00902e44));) {
        alignas(16) float center[4];
        alignas(16) float out[10];  // a whole light record (nglLightVectorWorld writes 0x28 bytes)
        std::memcpy(center, world_center().v, 16);
        const float* v = nglLightVectorWorld(out, light, center);
        // w is the record's +0x24 (a point light's range, 1e24 for a directional one).
        std::memcpy(lights + light_count * 4, v, 12);
        lights[light_count * 4 + 3] = v[9];
        light = light->next_object;
        if (++light_count >= 4) break;
    }

    // The first shadow projector, when the mesh's sphere (radius +0x30 plus
    // 0x008db19c) reaches its position in x and y (any distance with 0x0095f6d1 set).
    NglTexture* projector_texture = nullptr;
    NglLightSet* set = title_global<NglLightSet*>(0x00902e44);
    if (set->projectors[0] != reinterpret_cast<NglLight*>(set->projectors)) {
        const float* projector = set->projectors[0]->data;
        Lanes d = sub(with_w_one(world_center(), k.one), with_w_one(lanes(projector + 0x28), k.one));
        const float radius = *reinterpret_cast<const float*>(draw->mesh + 0x30) + title_global<float>(0x008db19c);
        d.v[2] = 0;
        bool reached = true;
        if (title_global<uint8_t>(0x0095f6d1) == 0) {
            const Lanes squares{{d.v[0] * d.v[0], d.v[1] * d.v[1], d.v[2] * d.v[2], d.v[3] * d.v[3]}};
            platform_keep(squares);
            reached = !(squares.v[0] + squares.v[1] + squares.v[2] > radius * radius);
        }
        if (reached) {
            projector_texture = *reinterpret_cast<NglTexture* const*>(projector + 0x22);
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < 4; ++row) projector_rows[column * 4 + row] = projector[row * 4 + column];
            const Lanes w = mul(lanes(&title_global<float>(k.w_axis)), title_global<float>(k.one));
            std::memcpy(c + 32, w.v, 16);
        }
    }

    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetAlphaRef(cache, 0);
    nglStateCache_SetAlphaFunc(cache, 0x206);
    nglStateCache_SetBlendEnable(cache, 0);
    set_cull_mode(0);
    bool lightmap = false;
    bool projected = false;
    k.textures(node, projector_texture, lightmap, projected);
    if (R_GetDebugViewMode() == 1) lightmap = false;
    nglSetVertexShaderCached(shader_ref(k.vertex_shaders + projected * 8));
    nglSetPixelShaderProgramCached(shader_ref(k.pixel_shaders + (projected + lightmap * 2) * 4));

    std::memcpy(c + 4, draw->world_view_projection, 64);
    c[36] = title_global<float>(0x008db198);
    const NglScene* scene = current_scene();
    const float fog_w = scene->fog[3] - scene->fog[2];
    const float fog_scale = title_global<float>(0x0083eb20) / (scene->fog[1] - scene->fog[0]);
    c[40] = scene->fog[2];
    c[41] = scene->fog[0];
    c[42] = fog_scale;
    c[43] = fog_w;
    alignas(16) float inverse[16];
    const float* object = nglNode_GetWorldInverse(draw, inverse);
    std::memcpy(c + 44, with_w_one(transform_point(object, scene->matrix0d0 + 12), k.one).v, 16);
    c[0] = title_global<float>(0x0083eb24);
    c[1] = title_global<float>(0x0083eba8);
    c[2] = 0;
    c[3] = 0;
    set_vertex_constants(6, c, 64);
    set_scene_fog();
    nglDrawMeshSection(node->section);

    // The dynamic lights: added on top (ONE), the white texture on every stage, two
    // lights per pass.
    if (light_count > 0) {
        nglStateCache_SetAlphaTestEnable(cache, 1);
        nglStateCache_SetAlphaFunc(cache, 0x204);
        nglStateCache_SetAlphaRef(cache, 0);
        nglStateCache_SetBlendEnable(cache, 1);
        nglStateCache_SetBlendOp(cache, 0x8006);
        nglStateCache_SetSrcBlend(cache, 0x302);
        nglStateCache_SetDestBlend(cache, 1);
        for (uint32_t stage = 0; stage < 4; ++stage) {
            nglSetTexture(stage, dynamic_light, 2, 3);
            title_global<uint32_t>(0x003f8320) |= 1u << stage;
            for (uint32_t i = 0; i < 3; ++i) xb::set_d3d_state(0x003f5c68 + stage * 0x80 + i * 4, 3);
        }
        int32_t n = light_count;
        if (n > 2) {
            nglSetVertexShaderCached(shader_ref(k.light_vertex_shaders[0] + n * 8));
            nglSetPixelShaderProgramCached(shader_ref(k.light_pixel_shaders[0] + n * 4));
            nglDrawMeshSection(node->section);
            n -= 2;
        }
        nglSetVertexShaderCached(shader_ref(k.light_vertex_shaders[1] + n * 8));
        nglSetPixelShaderProgramCached(shader_ref(k.light_pixel_shaders[1] + n * 4));
        nglDrawMeshSection(node->section);
    }
    reset_blend_cache();
}

void cdWorld_textures(const NglMeshNode* node, NglTexture* projector, bool& lightmap, bool& projected) {
    set_material_texture(node);
    if (NglTexture* second = *reinterpret_cast<NglTexture* const*>(node->material + 0x24)) {
        nglSetTexture(1, second, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 2;
        xb::set_d3d_state(0x003f5ce8, 3);
        xb::set_d3d_state(0x003f5cec, 3);
        lightmap = true;
    }
    if (projector) {
        set_projector_texture(lightmap ? 2 : 1, projector);  // the stage after the lightmap's
        projected = true;
    }
}

// Two textures (+0x1c, +0x24) blended, the lightmap (+0x2c) on stage 2, the projector
// on stage 3.
void cdWorldBlend_textures(const NglMeshNode* node, NglTexture* projector, bool& lightmap, bool& projected) {
    set_material_texture(node);
    nglSetTexture(1, *reinterpret_cast<NglTexture* const*>(node->material + 0x24), 2, 3);
    title_global<uint32_t>(0x003f8320) |= 2;
    xb::set_d3d_state(0x003f5ce8, 1);
    xb::set_d3d_state(0x003f5cec, 1);
    if (NglTexture* third = *reinterpret_cast<NglTexture* const*>(node->material + 0x2c)) {
        nglSetTexture(2, third, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 4;
        xb::set_d3d_state(0x003f5d68, 3);
        xb::set_d3d_state(0x003f5d6c, 3);
        lightmap = true;
    }
    if (projector) {
        set_projector_texture(3, projector);
        projected = true;
    }
}

}  // namespace

void XB_THISCALL cdWorld_Draw(const NglMeshNode* node) {
    draw_world(node, {0x00901f6c, 0x007b72b0, 0x007b7300, cdWorld_textures, 0x00902208, 0x0090222c,
                      {0x00902204, 0x00902214}, {0x00902230, 0x00902238}});
}

void XB_THISCALL cdWorldBlend_Draw(const NglMeshNode* node) {
    draw_world(node, {0x00901f54, 0x007b6ce0, 0x007b6d30, cdWorldBlend_textures, 0x009021b4, 0x009021d4,
                      {0x009021ac, 0x009021bc}, {0x009021d8, 0x009021e0}});
}

void XB_THISCALL cdAirplaneMetal_Draw(const NglMeshNode* node) {
    // Constants 6..12: the matrix, the first object light's vector in object space
    // (w 0), the camera in object space (w 1), the scroll offsets (fractional parts of
    // the material's +0x2c/+0x30 times the time) with 0.0025 and 0.001.
    alignas(16) float c[28];
    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetAlphaRef(cache, 0);
    nglStateCache_SetAlphaFunc(cache, 0x206);
    nglStateCache_SetBlendEnable(cache, 0);
    set_cull_mode(0);
    set_material_texture(node);
    nglSetTexture(1, *reinterpret_cast<NglTexture* const*>(node->material + 0x24), 2, 3);
    title_global<uint32_t>(0x003f8320) |= 2;
    xb::set_d3d_state(0x003f5ce8, 1);
    xb::set_d3d_state(0x003f5cec, 1);
    nglSetVertexShaderCached(shader_ref(0x00902080));
    nglSetPixelShaderProgramCached(shader_ref(0x00902078));
    NglMeshDrawNode* draw = draw_node(node);
    nglSelectMeshLightSet(draw);
    nglSetObjectLightConstants(-0x53, draw);
    alignas(16) float light_out[4];
    const float* light = nglEvalFirstObjectLight(light_out, draw->world + 12);
    std::memcpy(c, draw->world_view_projection, 64);
    alignas(16) float inverse[16];
    const float* m = nglNode_GetWorldInverse(draw, inverse);
    Lanes direction = add(add(mul(lanes(m), light[0]), mul(lanes(m + 4), light[1])), mul(lanes(m + 8), light[2]));
    direction.v[3] = title_global<float>(0x007b58c0);
    std::memcpy(c + 16, direction.v, 16);
    m = nglNode_GetWorldInverse(draw, inverse);
    Lanes camera = transform_point(m, current_scene()->camera_position);
    camera.v[3] = title_global<float>(0x007b58d0);
    std::memcpy(c + 20, camera.v, 16);
    const float time = title_global<float>(0x00901e90);
    const float u = *reinterpret_cast<const float*>(node->material + 0x2c) * time;
    const float v = *reinterpret_cast<const float*>(node->material + 0x30) * time;
    c[27] = title_global<float>(0x0083eb4c);
    c[26] = title_global<float>(0x0083f0e4);
    c[25] = v - static_cast<float>(static_cast<int32_t>(v));
    c[24] = u - static_cast<float>(static_cast<int32_t>(u));
    set_vertex_constants(6, c, 28);
    set_transform_constants(-0x4b, node);
    set_scene_fog();
    // Pixel shader constant 0: (0, 0, 0, the material's +0x28).
    alignas(16) const float shine[4] = {0, 0, 0, *reinterpret_cast<const float*>(node->material + 0x28)};
    xb::set_pixel_constants(0, shine, 1);  // SetPixelShaderConstant
    nglDrawMeshSection(node->section);
    reset_blend_cache();
}

void XB_THISCALL cdSky_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    set_cull_mode(0);
    if (node->material[0x20] != 0) {
        set_alpha_blended_tested();
    } else {
        nglStateCache_SetAlphaTestEnable(cache, 0);
        nglStateCache_SetBlendEnable(cache, 0);
    }
    const auto fog_off = [] {
        uint8_t& fog_enabled = title_global<uint8_t>(0x00902f93);
        if (fog_enabled != 0) {
            title_global<uint32_t>(0x003f8320) |= 0x2000;
            xb::set_d3d_state(0x003f8498, 0);
            fog_enabled = 0;
        }
    };
    fog_off();
    // Depth test and depth writes off for the sky (cache +0x78, +0x74), restored after.
    uint32_t& z_enable = title_global<uint32_t>(0x00902f80);
    uint8_t& z_write = title_global<uint8_t>(0x00902f7c);
    const uint32_t saved_z_enable = z_enable;
    const uint8_t saved_z_write = z_write;
    if (z_enable != 0) {
        xb::set_render_state(0x003e8210, 0);  // SetRenderState_ZEnable
        z_enable = 0;
    }
    if (z_write != 0) {
        xb::set_render_state_simple(0x0004035c, 0);  // SetRenderState_Simple
        z_write = 0;
    }

    // The sky turns about z: angle = time * the material's speed (+0x2c) * 0x0083edd8.
    const uint8_t* material = node->material;
    float sine, cosine;
    platform_x87_sin_cos_of_product(title_global<float>(0x00901e90), *reinterpret_cast<const float*>(material + 0x2c),
                                    title_global<float>(0x0083edd8), &sine, &cosine);
    const float zero = title_global<float>(0x0083eafc);
    const float rotation[3][4] = {{cosine, -sine, zero, cosine}, {sine, cosine, zero, sine}, {0, 0, 1, 0}};
    // Rows of the rotation times the matrix's columns (as directions; the zero fourth
    // row gives the translation), transposed back: constants 6..9.
    const float* wvp = draw_node(node)->world_view_projection;
    Lanes t[4];
    for (int i = 0; i < 4; ++i) t[i] = {{wvp[i], wvp[4 + i], wvp[8 + i], wvp[12 + i]}};
    Lanes rows[4];
    for (int r = 0; r < 3; ++r)
        rows[r] = add(add(mul(t[0], rotation[r][0]), mul(t[1], rotation[r][1])), mul(t[2], rotation[r][2]));
    rows[3] = add(add(add(t[3], mul(t[0], 0.0f)), mul(t[1], 0.0f)), mul(t[2], 0.0f));
    // Constant 10: the scroll offsets (fractional parts of the material's +0x24/+0x28
    // times the time and 0x0083ebf8); its last two lanes the title leaves uninitialized
    // (zeros here, and in --compare test processes on both sides).
    alignas(16) float c[20] = {};
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) c[r * 4 + k] = rows[k].v[r];
    const float time = title_global<float>(0x00901e90);
    const float scale = title_global<float>(0x0083ebf8);
    const float u = *reinterpret_cast<const float*>(material + 0x24) * time * scale;
    const float v = *reinterpret_cast<const float*>(material + 0x28) * time * scale;
    c[16] = u - static_cast<float>(static_cast<int32_t>(u));
    c[17] = v - static_cast<float>(static_cast<int32_t>(v));
    set_vertex_constants(6, c, 20);
    nglSetVertexShaderCached(shader_ref(0x0090225c));
    set_material_texture(node);
    nglSetPixelShaderProgramCached(shader_ref(0x00902264));
    fog_off();
    nglDrawMeshSection(node->section);
    if (z_enable != saved_z_enable) {
        xb::set_render_state(0x003e8210, saved_z_enable);
        z_enable = saved_z_enable;
    }
    if (z_write != saved_z_write) {
        xb::set_render_state_simple(0x0004035c, saved_z_write);
        z_write = saved_z_write;
    }
    reset_blend_cache();
}

void XB_THISCALL cdGlass_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetBlendEnable(cache, 1);
    nglStateCache_SetBlendOp(cache, 0x8006);
    nglStateCache_SetSrcBlend(cache, 1);
    nglStateCache_SetDestBlend(cache, 0x303);
    set_cull_mode(two_sided_cull(node->material, 0x28));
    reset_blend_cache();
    // Material +0x30: bit 0 lit (object light constants), bit 1 the second texture's
    // shader variant. The texture defaults to 0x00902700.
    const uint8_t* material = node->material;
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(material + 0x30);
    const uint32_t lit = flags & 1;
    const uint32_t variant = flags >> 1 & 1;
    NglTexture* texture = *reinterpret_cast<NglTexture* const*>(material + 0x1c);
    if (!texture) texture = title_global<NglTexture*>(0x00902700);
    nglSetTexture(0, texture, 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1;
    xb::set_d3d_state(0x003f5c68, 1);
    xb::set_d3d_state(0x003f5c6c, 1);
    if (NglTexture* second = *reinterpret_cast<NglTexture* const*>(node->material + 0x24)) {
        nglSetTexture(1, second, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 2;
        xb::set_d3d_state(0x003f5ce8, 1);
        xb::set_d3d_state(0x003f5cec, 1);
    }
    nglSetVertexShaderCached(shader_ref(0x00901fd8 + (variant + lit * 2) * 8));
    nglSetPixelShaderProgramCached(shader_ref(0x00901fd0 + variant * 4));
    NglMeshDrawNode* draw = draw_node(node);
    nglSelectMeshLightSet(draw);
    if (lit) nglSetObjectLightConstants(-0x4f, draw);

    // Constants 6..16: the matrix, the world-view rotation's rows (w 0, the last w 1),
    // the first object light's vector in object space (w 0), the camera in object
    // space (w 1), then the material's +0x2c in the first lane (the title leaves the
    // other three uninitialized: zeros here, and in --compare test processes).
    alignas(16) float c[44] = {};
    std::memcpy(c, draw->world_view_projection, 64);
    alignas(16) float buffer[16];
    const float* world_view = nglNode_GetWorldViewRotation(reinterpret_cast<const float*>(draw), buffer);
    const float w0 = title_global<float>(0x007b47c0);
    const float w1 = title_global<float>(0x007b47d0);
    for (int r = 0; r < 4; ++r) {
        std::memcpy(c + 16 + r * 4, world_view + r * 4, 12);
        c[16 + r * 4 + 3] = r < 3 ? w0 : w1;
    }
    alignas(16) float light_out[4];
    const float* light = nglEvalFirstObjectLight(light_out, draw->world + 12);
    const float* m = nglNode_GetWorldInverse(draw, buffer);
    Lanes direction = add(add(mul(lanes(m), light[0]), mul(lanes(m + 4), light[1])), mul(lanes(m + 8), light[2]));
    direction.v[3] = w0;
    std::memcpy(c + 32, direction.v, 16);
    m = nglNode_GetWorldInverse(draw, buffer);
    Lanes camera = transform_point(m, current_scene()->camera_position);
    camera.v[3] = w1;
    std::memcpy(c + 36, camera.v, 16);
    c[40] = *reinterpret_cast<const float*>(material + 0x2c);
    set_vertex_constants(6, c, 44);
    // Pixel shader constants 0 and 1: zero and 0.5 in every lane.
    const float half = title_global<float>(0x0083eb24);
    alignas(16) const float pixel[8] = {0, 0, 0, 0, half, half, half, half};
    xb::set_pixel_constants(0, pixel, 2);  // SetPixelShaderConstant
    set_transform_constants(-0x47, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

void XB_THISCALL nglDebugShader_Draw(const NglMeshNode* node) {
    set_cull_mode(0);
    nglSetBlendMode(state_cache(), 2, 0, 0);
    // Constants 6..10: the matrix and the colour - the instance's (a pointer to four
    // floats in slot 0x00902908), else white.
    alignas(16) float c[20];
    std::memcpy(c, draw_node(node)->world_view_projection, 64);
    const uint32_t slot = title_global<uint32_t>(0x00902908);
    const uint32_t* params = instance_params(node);
    if (has_param(params, slot)) {
        std::memcpy(c + 16, reinterpret_cast<const void*>(static_cast<uintptr_t>(params[2 + slot])), 16);
    } else {
        c[16] = c[17] = c[18] = c[19] = title_global<float>(0x0083eb20);
    }
    set_vertex_constants(6, c, 20);
    nglSetVertexShaderCached(shader_ref(0x00903d68));
    nglSetPixelShaderProgramCached(shader_ref(0x00903d64));
    nglDrawMeshSection(node->section);
}

namespace {

// 0 below zero, `max` above it (NaN stays), as the title's COMISS pairs test.
float clamp_to(float value, float max) {
    if (0.0f > value) return 0.0f;
    if (value > max) return max;
    return value;
}

// States the title sets inline through the cache: +0x74 depth writes (0x35c), +0x7c
// depth function (0x354), +0xa0 colour write mask (0x358).
template <typename T>
void set_inline_state(uint32_t address, uint32_t method, T value) {
    T& cached = title_global<T>(address);
    if (cached == value) return;
    xb::set_render_state_simple(method, value);  // SetRenderState_Simple
    cached = value;
}

void set_z_write(uint8_t enable) { set_inline_state<uint8_t>(0x00902f7c, 0x0004035c, enable); }
void set_z_func(uint32_t func) { set_inline_state<uint32_t>(0x00902f84, 0x00040354, func); }
void set_color_write(uint32_t mask) { set_inline_state<uint32_t>(0x00902fa8, 0x00040358, mask); }

float material_float(const uint8_t* material, uint32_t offset) {
    return *reinterpret_cast<const float*>(material + offset);
}

uint32_t material_word(const uint8_t* material, uint32_t offset) {
    return *reinterpret_cast<const uint32_t*>(material + offset);
}

}  // namespace

void XB_CDECL cdSimpleAlpha_ClampDebugPulse() {
    float* range = &title_global<float>(0x0089b020);
    for (int i = 0; i < 4; ++i) range[i] = clamp_to(range[i], 1.0f);
}

// Five stack arguments copied below the speed, the colour and out.
PLATFORM_ASM_ENTRY(cdSimpleAlpha_PulseColor_entry,
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(cdSimpleAlpha_PulseColor) "addl $32, %esp\n"
                   "ret")

float* XB_CDECL cdSimpleAlpha_PulseColor(float* out, const float* colour, float speed, float lo0, float hi0, float lo1,
                                         float hi1, float time) {
    const float angle = speed * time * title_global<float>(0x0083edac);  // 2 pi
    const float w = (scene_sine_poly(angle) + 1.0f) * title_global<float>(0x0083eb24);  // 0.5
    const float t0 = clamp_to((hi0 - lo0) * w + lo0, 1.0f);
    const Lanes c = lanes(colour);
    const float rest = 1.0f - t0;
    const Lanes mixed = add(mul(c, t0), Lanes{{rest, rest, rest, rest}});
    const float t1 = clamp_to((hi1 - lo1) * w + lo1, 1.0f);
    const Lanes scaled = mul(c, t1);
    platform_keep(mixed);
    platform_keep(scaled);
    out[0] = mixed.v[0];
    out[1] = mixed.v[1];
    out[2] = mixed.v[2];
    out[3] = scaled.v[3];
    return out;
}

void XB_THISCALL cdSimpleAlpha_Draw(const NglMeshNode* node) {
    uint8_t* cache = state_cache();
    const float time = title_global<float>(0x00901e90);
    // Constants 8..20: the matrix, the object lights (directions, colours + ambient),
    // the colour.
    alignas(16) float constants[52] = {};
    NglMeshDrawNode* draw = draw_node(node);
    nglSelectMeshLightSet(draw);
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    std::memcpy(constants, draw->world_view_projection, 64);

    alignas(16) float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t flags;      // 1 depth pre-pass, 2 colour, 4 pulse
    uint32_t blend;
    uint32_t two_sided;  // 2 = no culling
    int32_t alpha_ref;
    if (title_global<uint32_t>(0x0089aff0)) {
        // The debug override.
        const uint32_t use_colour = title_global<uint32_t>(0x0089b000);
        flags = use_colour * 2 | title_global<uint32_t>(0x0089affc);
        two_sided = title_global<uint32_t>(0x0089aff8) ? 0 : 2;
        int32_t& mode = title_global<int32_t>(0x0089aff4);
        if (mode >= 0x28) mode = 0x1e;
        blend = static_cast<uint32_t>(mode / 10);
        if (use_colour) {
            float* rgb = &title_global<float>(0x0089b008);
            for (int i = 0; i < 3; ++i) {
                rgb[i] = clamp_to(rgb[i], title_global<float>(0x007b4c64));  // 2
                colour[i] = rgb[i];
            }
        }
        float& alpha = title_global<float>(0x0089b014);
        alpha = clamp_to(alpha, 1.0f);
        colour[3] = alpha;
        if (title_global<uint32_t>(0x0089b004)) {
            cdSimpleAlpha_ClampDebugPulse();
            const float* range = &title_global<float>(0x0089b020);
            alignas(16) float pulsed[4];
            cdSimpleAlpha_PulseColor(pulsed, colour, title_global<float>(0x0089b01c), range[0], range[1], range[2],
                                     range[3], time);
            std::memcpy(colour, pulsed, 16);
        }
        alpha_ref = static_cast<int32_t>(title_global<float>(0x0089b018) * title_global<float>(0x0083eb8c));
    } else {
        const uint8_t* material = node->material;
        flags = material_word(material, 0x28);
        blend = material_word(material, 0x24);
        two_sided = material_word(material, 0x20);
        if (flags & 2)
            for (int i = 0; i < 3; ++i) colour[i] = material_float(material, 0x2c + 4 * i);
        colour[3] = material_float(material, 0x38);
        if (flags & 4) {
            const float lo0 = clamp_to(material_float(material, 0x44), 1.0f);
            const float hi0 = clamp_to(material_float(material, 0x48), 1.0f);
            const float lo1 = clamp_to(material_float(material, 0x4c), 1.0f);
            const float hi1 = clamp_to(material_float(material, 0x50), 1.0f);
            alignas(16) float pulsed[4];
            cdSimpleAlpha_PulseColor(pulsed, colour, material_float(material, 0x40), lo0, hi0, lo1, hi1, time);
            std::memcpy(colour, pulsed, 16);
        }
        alpha_ref = static_cast<int32_t>(material_float(material, 0x3c) * title_global<float>(0x0083eb8c));
    }

    // The instance's colour slot holds an alpha here: white at that alpha.
    const uint32_t slot = title_global<uint32_t>(0x00902908);
    const uint32_t* params = instance_params(node);
    if (has_param(params, slot)) {
        // The title tests the mask twice; the second test's failure (impossible) would
        // take the two-sided word.
        const float alpha = from_bits(has_param(params, slot) ? params[2 + slot] : two_sided);
        if (alpha == 1.0f) blend = 0;
        colour[0] = colour[1] = colour[2] = 1.0f;
        colour[3] = alpha;
    }
    if (colour[3] == 1.0f && blend == 0) {
        flags &= ~1u;
        blend = 3;
    }
    const Lanes scaled = mul(lanes(colour), title_global<float>(0x0083eb24));  // 0.5
    std::memcpy(constants + 48, scaled.v, 16);

    set_material_texture(node);
    set_transform_constants(-0x5a, node);
    set_scene_fog(0);
    set_cull_mode(two_sided == 2 ? 0 : 0x900);
    nglStateCache_SetAlphaTestEnable(cache, 1);
    nglStateCache_SetAlphaFunc(cache, 0x204);
    nglStateCache_SetAlphaRef(cache, static_cast<uint32_t>(alpha_ref));
    const uint32_t saved_color_write = title_global<uint32_t>(0x00902fa8);
    const uint32_t saved_z_func = title_global<uint32_t>(0x00902f84);
    const uint8_t saved_z_write = title_global<uint8_t>(0x00902f7c);
    const bool variant = R_GetDebugViewMode() == 1;

    if (flags & 1) {
        // Depth only, then less-or-equal against it.
        set_z_write(1);
        set_color_write(0);
        nglStateCache_SetBlendEnable(cache, 0);
        reset_blend_cache();
        nglSetVertexShaderCached(shader_ref(0x00902000));
        nglSetPixelShaderProgramCached(shader_ref(variant ? 0x00902018 : 0x00902010));
        set_vertex_constants(8, constants, 52);
        nglDrawMeshSection(node->section);
        set_z_func(0x203);
        set_z_write(0);
        set_color_write(0x01010101);
    }

    nglStateCache_SetAlphaTestEnable(cache, 0);
    nglStateCache_SetFogEnable(cache, 1);
    nglStateCache_SetAlphaRef(cache, 0);
    uint8_t blend_enable = 1;
    const auto blend_onto = [cache](uint32_t op) {
        nglStateCache_SetBlendOp(cache, op);
        nglStateCache_SetSrcBlend(cache, 0x302);
        nglStateCache_SetDestBlend(cache, 1);
    };
    switch (blend) {
    case 1:
        blend_onto(0x8006);
        break;
    case 2:
        blend_onto(0x800b);
        break;
    case 3:
        blend_enable = 0;
        set_z_write(1);
        nglStateCache_SetAlphaTestEnable(cache, 1);
        nglStateCache_SetFogEnable(cache, 1);
        nglStateCache_SetAlphaRef(cache, 0x80);
        break;
    default:
        nglStateCache_SetBlendOp(cache, 0x8006);
        nglStateCache_SetSrcBlend(cache, 0x302);
        nglStateCache_SetDestBlend(cache, 0x303);
        break;
    }
    nglStateCache_SetBlendEnable(cache, blend_enable);
    reset_blend_cache();
    nglSetVertexShaderCached(shader_ref(0x00902008));
    nglSetPixelShaderProgramCached(shader_ref(variant ? 0x0090201c : 0x00902014));
    set_vertex_constants(8, constants, 52);
    nglDrawMeshSection(node->section);
    set_z_write(saved_z_write);
    set_color_write(saved_color_write);
    set_z_func(saved_z_func);
}

float* XB_THISCALL nglNode_GetWorldViewRotation(const float* world, float* out) {
    // world * the current scene's view matrix, rows 0-2 as directions (no translation
    // term), row 3 as a point.
    const float* view = current_scene()->view_matrix;
    const Lanes v0 = lanes(view), v1 = lanes(view + 4), v2 = lanes(view + 8), v3 = lanes(view + 12);
    Lanes rows[4];
    for (int r = 0; r < 3; ++r) {
        const float* m = world + r * 4;
        rows[r] = add(add(mul(v0, m[0]), mul(v1, m[1])), mul(v2, m[2]));
    }
    rows[3] = add(add(add(v3, mul(v0, world[12])), mul(v1, world[13])), mul(v2, world[14]));
    for (int r = 0; r < 4; ++r) std::memcpy(out + r * 4, rows[r].v, 16);
    return out;
}

int32_t XB_THISCALL ShaderCache_RandomLevel(ShaderCache* self) {
    int32_t level = 0;
    int32_t pair;
    do {
        const int32_t bits = self->random_bits;
        pair = bits & 3;
        if (pair == 0) ++level;
        self->random_bits = bits >> 2;
        if (--self->bits_left == 0) {
            self->random_bits = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // rand
            self->bits_left = 7;
        }
    } while (pair == 0);
    return level > 15 ? 15 : level;
}

void XB_THISCALL ShaderCache_Init(ShaderCache* self) {
    auto** end = reinterpret_cast<ShaderCacheNode**>(self->unknown00);
    if (*end) return;
    // The end node: only its key is set, all ones (above every name).
    *end = static_cast<ShaderCacheNode*>(Mem_AlignedAlloc(0x2c, 4, 0));
    auto* key = reinterpret_cast<uint32_t*>(*end);
    for (int i = 0; i < 8; ++i) key[i] = 0xffffffff;
    self->random_bits = static_cast<int32_t>(title_function<uint32_t XB_CDECL()>(0x001efcc0)());  // _rand
    self->bits_left = 7;
    self->level = 0;
    self->head = static_cast<ShaderCacheNode*>(Mem_AlignedAlloc(0x6c, 4, 0));
    for (int i = 0; i < 16; ++i) self->head->next[i] = *end;
}

ShaderCacheNode* XB_THISCALL ShaderCache_FindOrAdd(ShaderCache* self, const NglHashedName* key, void* value) {
    // Keys compare as eight unsigned words, the first difference deciding.
    auto compare = [key](const ShaderCacheNode* node) {
        const uint32_t* a = reinterpret_cast<const uint32_t*>(key);
        const uint32_t* b = reinterpret_cast<const uint32_t*>(&node->key);
        for (int32_t i = 0; i < 8; ++i)
            if (a[i] != b[i]) return a[i] < b[i] ? 1 : -1;
        return 0;
    };
    ShaderCacheNode* update[17];
    ShaderCacheNode* x = self->head;
    ShaderCacheNode* node = nullptr;
    for (int32_t level = self->level; level >= 0; --level) {
        for (;;) {
            node = x->next[level];
            if (compare(node) >= 0) break;
            x = node;
        }
        update[level] = x;
    }
    if (compare(node) == 0) {
        ++node->references;
        return node;
    }
    int32_t level = ShaderCache_RandomLevel(self);
    if (level > self->level) {
        level = self->level + 1;
        self->level = level;
        update[level] = self->head;
    }
    node = static_cast<ShaderCacheNode*>(Mem_AlignedAlloc(level * 4 + 0x2c, 4, 0));
    node->key = *key;
    node->value = value;
    node->references = 1;
    for (; level >= 0; --level) {
        node->next[level] = update[level]->next[level];
        update[level]->next[level] = node;
    }
    return nullptr;
}

namespace {

// Register's common part: the next sort id (0x009028ec) and the shader cache entry.
// GetName runs twice; the first result is unused.
void register_class(NglShaderClass* self) {
    using GetName = NglHashedName* XB_THISCALL(const NglShaderClass*, NglHashedName*);
    self->id = title_global<uint32_t>(0x009028ec)++;
    GetName* get_name = reinterpret_cast<GetName*>(self->vtable[1]);
    NglHashedName name;
    get_name(self, &name);
    ShaderCache_FindOrAdd(&title_global<ShaderCache>(0x00902ac0), get_name(self, &name), self);
}

}  // namespace

void XB_THISCALL cdDebug_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00901f88), 0x00897ad0, title_global<uint32_t>(0x008996e4));
    nglPixelShader_Init(&title_global<uint32_t>(0x00901f90), title_global<uint32_t>(0x008997d8));
}

void XB_THISCALL ShaderPCUV_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00901f98), 0x00897c00, title_global<uint32_t>(0x008998d4));
    nglPixelShader_Init(&title_global<uint32_t>(0x00901fa0), title_global<uint32_t>(0x008999c8));
}

void XB_THISCALL cdGunSight_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00901fac), 0x00897d30, title_global<uint32_t>(0x00899b44));
    nglPixelShader_Init(&title_global<uint32_t>(0x00901fb4), title_global<uint32_t>(0x00899c38));
    nglPixelShader_Init(&title_global<uint32_t>(0x00901fa8), title_global<uint32_t>(0x00899d30));
}

void XB_THISCALL cdGun_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00901fc0), 0x00897e70, title_global<uint32_t>(0x00899eb4));
    nglPixelShader_Init(&title_global<uint32_t>(0x00901fbc), title_global<uint32_t>(0x00899fa8));
    nglPixelShader_Init(&title_global<uint32_t>(0x00901fc8), title_global<uint32_t>(0x0089a0a0));
}

void XB_THISCALL cdGlass_Register(NglShaderClass* self) {
    register_class(self);
    cdGlass_InitVertexShaders(0x00897fb0);
    cdGlass_InitPixelShaders();
    nglPixelShader_Init(&title_global<uint32_t>(0x00901ff8), title_global<uint32_t>(0x0089a9c8));
}

void XB_THISCALL cdSimpleAlpha_Register(NglShaderClass* self) {
    register_class(self);
    cdSimpleAlpha_InitVertexShaders(0x008980f0);
    cdSimpleAlpha_InitPixelShaders();
    cdSimpleAlpha_InitPixelShaders2();
}

void XB_THISCALL cdPropeller_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902024), 0x00898230, title_global<uint32_t>(0x0089b2a4));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902030), title_global<uint32_t>(0x0089b398));
    nglPixelShader_Init(&title_global<uint32_t>(0x0090202c), title_global<uint32_t>(0x0089b490));
}

void XB_THISCALL cdDecal_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902040), 0x00898370, title_global<uint32_t>(0x0089b594));
    nglPixelShader_Init(&title_global<uint32_t>(0x0090203c), title_global<uint32_t>(0x0089b688));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902038), title_global<uint32_t>(0x0089b780));
}

void XB_THISCALL cdChar_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902054), 0x008984b0, title_global<uint32_t>(0x0089baa4));
    nglPixelShader_Init(&title_global<uint32_t>(0x0090204c), title_global<uint32_t>(0x0089bb98));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902050), title_global<uint32_t>(0x0089bc90));
}

void XB_THISCALL cdPrelit_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902064), 0x008985f0, title_global<uint32_t>(0x0089bd94));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902060), title_global<uint32_t>(0x0089be88));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902070), title_global<uint32_t>(0x0089bf80));
    nglPixelShader_Init(&title_global<uint32_t>(0x0090206c), title_global<uint32_t>(0x0089c078));
}

void XB_THISCALL cdAirplaneMetal_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902080), 0x00898730, title_global<uint32_t>(0x0089c2a4));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902078), title_global<uint32_t>(0x0089c398));
    nglPixelShader_Init(&title_global<uint32_t>(0x0090207c), title_global<uint32_t>(0x0089c490));
}

void XB_THISCALL cdSimpleUVAnim_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902090), 0x00898870, title_global<uint32_t>(0x0089c624));
    nglPixelShader_Init(&title_global<uint32_t>(0x0090208c), title_global<uint32_t>(0x0089c718));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902098), title_global<uint32_t>(0x0089c810));
}

void XB_THISCALL cdSimplePrelit_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x009020a4), 0x008989b0, title_global<uint32_t>(0x0089c914));
    nglPixelShader_Init(&title_global<uint32_t>(0x009020a0), title_global<uint32_t>(0x0089ca08));
}

void XB_THISCALL cdSimple_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x009020b8), 0x00898af0, title_global<uint32_t>(0x0089cb84));
    nglPixelShader_Init(&title_global<uint32_t>(0x009020b4), title_global<uint32_t>(0x0089cc78));
    nglPixelShader_Init(&title_global<uint32_t>(0x009020b0), title_global<uint32_t>(0x0089cd70));
}

void XB_THISCALL cdRiver_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x009020e8), 0x00898c30, title_global<uint32_t>(0x0089d174));
    cdRiver_InitPixelShaders();
    nglPixelShader_Init(&title_global<uint32_t>(0x009020c4), title_global<uint32_t>(0x0089da08));
}

void XB_THISCALL cdOcean_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x00902118), 0x00898d70, title_global<uint32_t>(0x0089de04));
    cdOcean_InitPixelShaders();
    nglPixelShader_Init(&title_global<uint32_t>(0x00902114), title_global<uint32_t>(0x0089e698));
}

void XB_THISCALL cdWater_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x009021a4), 0x00898eb0, title_global<uint32_t>(0x0089e7f4));
    nglPixelShader_Init(&title_global<uint32_t>(0x009021a0), title_global<uint32_t>(0x0089e8e8));
    nglPixelShader_Init(&title_global<uint32_t>(0x009021ac), title_global<uint32_t>(0x0089e9e0));
}

void XB_THISCALL cdWorldBlend_Register(NglShaderClass* self) {
    register_class(self);
    cdWorldBlend_InitVertexShaders(0x00898ff0);
    cdWorldBlend_InitVertexShaders2(0x00898ff0);
    cdWorldBlend_InitPixelShaders();
    cdWorldBlend_InitPixelShaders2();
    nglPixelShader_Init(&title_global<uint32_t>(0x009021ec), title_global<uint32_t>(0x0089f738));
}

void XB_THISCALL cdWorldVertexLit_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x009021f8), 0x00899130, title_global<uint32_t>(0x0089f834));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902200), title_global<uint32_t>(0x0089f928));
    nglPixelShader_Init(&title_global<uint32_t>(0x009021f4), title_global<uint32_t>(0x0089fa20));
}

void XB_THISCALL cdWorld_Register(NglShaderClass* self) {
    register_class(self);
    cdWorld_InitVertexShaders(0x00899270);
    cdWorld_InitVertexShaders2(0x00899270);
    cdWorld_InitPixelShaders();
    cdWorld_InitPixelShaders2();
    nglPixelShader_Init(&title_global<uint32_t>(0x00902218), title_global<uint32_t>(0x008a0748));
}

void XB_THISCALL cdBackground_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x0090224c), 0x008993b0, title_global<uint32_t>(0x008a0844));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902248), title_global<uint32_t>(0x008a0938));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902254), title_global<uint32_t>(0x008a0a30));
}

void XB_THISCALL cdSky_Register(NglShaderClass* self) {
    register_class(self);
    nglVertexShader_Init(&title_global<uint32_t>(0x0090225c), 0x008994f0, title_global<uint32_t>(0x008a0ae4));
    nglPixelShader_Init(&title_global<uint32_t>(0x00902264), title_global<uint32_t>(0x008a0bd8));
}

void XB_CDECL R_SetDebugViewMode(uint32_t mode) { title_global<uint32_t>(0x00901e94) = mode; }

uint32_t XB_CDECL R_GetDebugViewMode() { return title_global<uint32_t>(0x00901e94); }

uint32_t XB_CDECL R_SetTextureMip(uint32_t mip) { return title_global<uint32_t>(0x00897964) = mip; }

uint32_t* XB_THISCALL cdDebug_InitMaterial(uint32_t* material) {
    material[0] = 0;
    material[1] = title_global<uint32_t>(0x00901ec4);  // the cdDebug shader class
    material[2] = material[3] = material[4] = material[5] = 0;
    return material;
}

uint8_t* XB_THISCALL ShaderPCUV_InitMaterial(uint8_t* material, uint32_t a, uint32_t b, uint32_t c) {
    auto* words = reinterpret_cast<uint32_t*>(material);
    words[0x1c / 4] = a;
    words[0x20 / 4] = b;
    words[0x24 / 4] = c;
    words[1] = title_global<uint32_t>(0x00901ecc);  // the PCUV shader class
    return material;
}

void XB_THISCALL cdSimpleInstance_InitList(CdSimpleInstanceList* self, uint8_t* mesh, uint32_t b) {
    self->mesh = mesh;
    self->unknown04 = b;
    self->unknown08 = 0;
    self->records = static_cast<uint8_t*>(Mem_AlignedAlloc(0xc000, 0x10, 0));
    self->count = 0;
}

void XB_THISCALL cdSimpleInstance_ShrinkList(CdSimpleInstanceList* self) {
    const uint32_t size = static_cast<uint32_t>(self->count) << 7;
    auto* records = static_cast<uint8_t*>(Mem_AlignedAlloc(size, 0x10, 0));
    std::memcpy(records, self->records, static_cast<uint32_t>(self->count) << 7);
    title_function<void XB_CDECL(void*)>(0x000939b0)(self->records);  // Mem_AlignedFree
    self->records = records;
}

CdSimpleInstanceList* XB_THISCALL cdSimpleInstance_AddInstance(CdSimpleInstanceList* self, const float* matrix,
                                                               float scale, const float* direction, const float* light) {
    std::memcpy(self->records + (static_cast<uint32_t>(self->count) << 7), matrix, 0x40);
    // Row 2 = row0 x row1 (packed: lane 3 is w*w - w*w).
    const Lanes a = lanes(matrix + 4);
    const Lanes b = lanes(matrix);
    const Lanes a_yzx = {{a.v[1], a.v[2], a.v[0], a.v[3]}};
    const Lanes b_yzx = {{b.v[1], b.v[2], b.v[0], b.v[3]}};
    const Lanes cross = sub(mul(b, a_yzx), mul(b_yzx, a));
    float* row2 = reinterpret_cast<float*>(self->records + (static_cast<uint32_t>(self->count) << 7) + 0x20);
    row2[0] = cross.v[1];
    row2[1] = cross.v[2];
    row2[2] = cross.v[0];
    row2[3] = cross.v[3];
    *reinterpret_cast<float*>(self->records + (static_cast<uint32_t>(self->count) << 7) + 0x40) = scale;
    std::memcpy(self->records + (static_cast<uint32_t>(self->count) << 7) + 0x50, direction, 0x10);
    std::memcpy(self->records + (static_cast<uint32_t>(self->count) << 7) + 0x60, light, 0x10);
    float* ambient = reinterpret_cast<float*>(self->records + (static_cast<uint32_t>(self->count) << 7) + 0x70);
    std::memcpy(ambient, light + 0xc, 0x10);
    const float quarter = from_bits(0x3e800000);  // 0.25 (0x0083ec24)
    Lanes sum = add(lanes(ambient), mul(lanes(light + 8), quarter));
    for (int i = 0; i < 4; ++i) ambient[i] = sum.v[i];
    sum = add(lanes(ambient), mul(lanes(light + 4), quarter));
    for (int i = 0; i < 4; ++i) ambient[i] = sum.v[i];
    ++self->count;
    return self;
}

// The original's frame (EBP, ESP aligned to 16, 0x54 bytes, three saved registers) with
// the instance block at its +0x30: nglListAddMesh copies all 0x30 bytes of it, and the
// original sets only the flags and the scale, so the rest is whatever its stack held.
PLATFORM_ASM_ENTRY(cdSimpleInstance_AddListToScene_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "andl $-16, %esp\n"
                   "subl $0x54, %esp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "leal 0x30(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(cdSimpleInstance_AddListToScene) "addl $8, %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

void XB_CDECL cdSimpleInstance_AddListToScene(CdSimpleInstanceList* self, uint32_t* block) {
    if (title_global<uint8_t>(0x00901e8e) & 2) return;
    for (int32_t i = 0; i < self->count; ++i) {
        uint8_t* record = self->records + i * 0x80;
        block[0] = 2;
        const float s = *reinterpret_cast<const float*>(record + 0x40);
        for (int j = 0; j < 4; ++j) std::memcpy(&block[4 + j], &s, 4);
        uintptr_t& arena = title_global<uintptr_t>(0x009025a8);
        auto* lights = reinterpret_cast<uint32_t*>((arena + 7) & ~uintptr_t{7});
        arena = reinterpret_cast<uintptr_t>(lights) + title_global<uint32_t>(0x00902904) * 4 + 8;
        lights[0] = 0;
        lights[1] = 0;
        NglLightSet* set = nglCreateLightSet();
        const uint32_t category = title_global<uint32_t>(0x00902914);
        lights[0] |= 1u << (category & 31);
        lights[1] |= 0;
        lights[category + 2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(set));
        nglAddDirectionalLight(0xffffffff, reinterpret_cast<const float*>(self->records + i * 0x80 + 0x50),
                               reinterpret_cast<const float*>(self->records + i * 0x80 + 0x60));
        const float* ambient = reinterpret_cast<const float*>(self->records + i * 0x80 + 0x70);
        nglSetAmbientColor(ambient[0], ambient[1], ambient[2]);
        const uint32_t light_ref = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lights));
        nglListAddMesh(self->mesh, reinterpret_cast<const float*>(self->records + i * 0x80), block, &light_ref);
    }
}

namespace {

// The vertex iterators' Clone (slot of every shader class's iterator vtable): a 12-byte copy
// (Z_Malloc) whose vtable is set to the base iterator's first, then to the class's own; null
// when the allocation fails.
uint32_t* clone_vertex_iter(const uint32_t* self, uint32_t base_vtable, uint32_t vtable) {
    uint32_t* copy = static_cast<uint32_t*>(title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(0xc));  // Z_Malloc
    if (!copy) return nullptr;
    copy[0] = base_vtable;
    copy[1] = self[1];
    copy[2] = self[2];
    copy[0] = vtable;
    return copy;
}

}  // namespace

uint32_t* XB_THISCALL cdGunSightVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b444c, 0x007b4430); }
uint32_t* XB_THISCALL cdGunVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b4724, 0x007b4708); }
uint32_t* XB_THISCALL cdGlassVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b49f4, 0x007b49d8); }
uint32_t* XB_THISCALL cdSimpleAlphaVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b4cdc, 0x007b4cc0); }
uint32_t* XB_THISCALL cdPropellerVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b4fb8, 0x007b4f9c); }
uint32_t* XB_THISCALL cdDecalVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b5284, 0x007b5268); }
uint32_t* XB_THISCALL cdCharVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b554c, 0x007b5530); }
uint32_t* XB_THISCALL cdPrelitVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b5828, 0x007b580c); }
uint32_t* XB_THISCALL cdAirplaneMetalVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b5afc, 0x007b5ae0); }
uint32_t* XB_THISCALL cdSimpleUVAnimVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b5ddc, 0x007b5dc0); }
uint32_t* XB_THISCALL cdSimplePrelitVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b60bc, 0x007b60a0); }
uint32_t* XB_THISCALL cdSimpleVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b6398, 0x007b637c); }
uint32_t* XB_THISCALL cdRiverVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b66c0, 0x007b66a4); }
uint32_t* XB_THISCALL cdOceanVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b6960, 0x007b6944); }
uint32_t* XB_THISCALL cdWaterVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b6c34, 0x007b6c18); }
uint32_t* XB_THISCALL cdWorldBlendVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b6f0c, 0x007b6ef0); }
uint32_t* XB_THISCALL cdWorldVertexLitVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b7200, 0x007b71e4); }
uint32_t* XB_THISCALL cdWorldVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b74d4, 0x007b74b8); }
uint32_t* XB_THISCALL cdBackgroundVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b77ac, 0x007b7790); }
uint32_t* XB_THISCALL cdSkyVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007b7a88, 0x007b7a6c); }
uint32_t* XB_THISCALL nglDebugMeshVertexIter_Clone(const uint32_t* self) { return clone_vertex_iter(self, 0x007be7c0, 0x007be7d8); }

namespace {

uint32_t** new_vertex_iter(const void* mesh, uint32_t** out, uint32_t vtable) {
    uint32_t iter[3] = {vtable, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mesh)), 0};
    using Clone = uint32_t* XB_THISCALL(const uint32_t*);
    *out = title_function<Clone>(title_global<uint32_t>(vtable + 4))(iter);
    return out;
}

}  // namespace

uint32_t** XB_THISCALL cdGunSight_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b4430); }
uint32_t** XB_THISCALL cdGun_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b4708); }
uint32_t** XB_THISCALL cdGlass_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b49d8); }
uint32_t** XB_THISCALL cdSimpleAlpha_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b4cc0); }
uint32_t** XB_THISCALL cdPropeller_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b4f9c); }
uint32_t** XB_THISCALL cdDecal_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b5268); }
uint32_t** XB_THISCALL cdChar_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b5530); }
uint32_t** XB_THISCALL cdPrelit_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b580c); }
uint32_t** XB_THISCALL cdAirplaneMetal_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b5ae0); }
uint32_t** XB_THISCALL cdSimpleUVAnim_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b5dc0); }
uint32_t** XB_THISCALL cdSimplePrelit_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b60a0); }
uint32_t** XB_THISCALL cdSimple_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b637c); }
uint32_t** XB_THISCALL cdRiver_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b66a4); }
uint32_t** XB_THISCALL cdWater_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b6c18); }
uint32_t** XB_THISCALL cdWorldBlend_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b6ef0); }
uint32_t** XB_THISCALL cdWorldVertexLit_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b71e4); }
uint32_t** XB_THISCALL cdWorld_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b74b8); }
uint32_t** XB_THISCALL cdBackground_NewVertexIter(const void* mesh, uint32_t** out) { return new_vertex_iter(mesh, out, 0x007b7790); }

void XB_CDECL cdSimpleInstance_Init() {}

void XB_THISCALL cdSimpleInstance_FreeList(CdSimpleInstanceList* self) {
    title_function<void XB_CDECL(void*)>(0x000939b0)(self->records);  // Mem_AlignedFree
}

void* XB_THISCALL cdDebug_GetSortInfo(const NglMeshNode* node, uint32_t* out) {
    // The instance block's light list (+0x8c: a 64-bit category mask, then one light set per
    // category): the current category's set (0x00902908), its fourth word as a float; 1.0 when
    // the category is not in the mask. 1.0 -> opaque list keyed by the class id, else translucent
    // by view depth. (The original tests the mask twice; the second test always passes.)
    const uint8_t* block = reinterpret_cast<const uint8_t*>(node->world_matrix);
    const uint32_t* lights = *reinterpret_cast<uint32_t* const*>(block + 0x8c);
    const uint32_t category = title_global<uint32_t>(0x00902908);
    const int32_t bit = static_cast<int32_t>(1u << (category & 31));
    const uint32_t low = static_cast<uint32_t>(bit);
    const uint32_t high = bit < 0 ? 0xffffffffu : 0;
    float w = 1.0f;
    if ((lights[0] & low) == low && (lights[1] & high) == high) {
        const uint32_t* set = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(lights[category + 2]));
        std::memcpy(&w, &set[3], 4);
    }
    if (w == 1.0f) {
        out[0] = 0;
        out[1] = title_global<NglShaderClass*>(0x00901ec4)->id;
        return out;
    }
    out[0] = 1;
    const float depth = nglNode_ViewDepth(node);
    std::memcpy(&out[1], &depth, 4);
    return out;
}

void XB_CDECL nglInitShaders() {
    // The screen-space shaders (title source strings; nglAssemblePixelShader /
    // nglAssembleVertexShader are variadic formats), then every shader class's Init.
    using AssemblePixel = void XB_CDECL(uint32_t, uint32_t, ...);
    using AssembleVertex = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, ...);
    title_function<AssemblePixel>(0x00076120)(0x00901e70, 0x007b38c8);
    title_function<AssemblePixel>(0x00076120)(0x00901e74, 0x007b37b0);
    title_function<AssemblePixel>(0x00076120)(0x00901e78, 0x007b3760);
    title_function<AssembleVertex>(0x00076220)(0x008979a0, 0x00901e84, 1, 0x007b3710);
    cdSky_Init();
    cdBackground_Init();
    cdWorld_Init();
    cdWorldVertexLit_Init();
    cdWorldBlend_Init();
    cdWater_Init();
    cdOcean_Init();
    cdRiver_Init();
    cdSimple_Init();
    cdSimplePrelit_Init();
    cdSimpleUVAnim_Init();
    cdSimpleInstance_Init();
    ShaderPCUV_Init();
    cdAirplaneMetal_Init();
    cdPrelit_Init();
    cdChar_Init();
    cdDecal_Init();
    cdPropeller_Init();
    cdSimpleAlpha_Init();
    cdGlass_Init();
    cdGun_Init();
    cdGunSight_Init();
    cdDebug_Init();
}

uint8_t XB_CDECL Str_EqualN(const char* a, int32_t length, const char* b, int32_t b_length) {
    if (length != b_length && b_length >= 0) return 0;
    for (; length; --length, ++a, ++b) {
        if (*a != *b) return 0;
    }
    return 1;
}

void XB_CDECL nglToggleShaderByName(const char* name) {
    // The shader class names (title strings) in the original's order; the first one equal
    // to the lowered name (at most 31 characters) toggles that class.
    struct Entry {
        uint32_t name;
        void(XB_CDECL* toggle)();
    };
    static const Entry kEntries[] = {
        {0x007b3a18, &cdWorld_Toggle},
        {0x007b3a04, &cdWorldVertexLit_Toggle},
        {0x007b39f4, &cdWorldBlend_Toggle},
        {0x007b39ec, &cdWater_Toggle},
        {0x007b39e4, &cdOcean_Toggle},
        {0x007b39dc, &cdRiver_Toggle},
        {0x007b39d0, &cdSimple_Toggle},
        {0x007b39bc, &cdSimpleInstance_Toggle},
        {0x007b39ac, &cdSimplePrelit_Toggle},
        {0x007b399c, &cdSimpleUVAnim_Toggle},
        {0x007b398c, &cdSimpleAlpha_Toggle},
        {0x007b39bc, &cdSimpleInstance_Toggle},  // compared a second time
        {0x007b3984, &cdChar_Toggle},
        {0x007b397c, &cdDebug_Toggle},
        {0x007b3974, &cdSky_Toggle},
        {0x007b396c, &cdDecal_Toggle},
        {0x007b395c, &cdBackground_Toggle},
        {0x007b3950, &cdPropeller_Toggle},
        {0x007b3940, &cdAirplaneMetal_Toggle},
        {0x007b3934, &cdPrelit_Toggle},
        {0x007b392c, &cdGun_Toggle},
        {0x007b3920, &cdGunSight_Toggle},
        {0x007b3918, &cdGlass_Toggle},
    };
    char buffer[0x20];
    int32_t length = 0;
    Q_strncpyLen(buffer, &length, 0x1f, name);
    const uint8_t length8 = static_cast<uint8_t>(length);
    title_function<char* XB_CDECL(char*)>(0x001fa3b2)(buffer);  // _strlwr
    for (const Entry& entry : kEntries) {
        if (Str_EqualN(buffer, length8, reinterpret_cast<const char*>(uintptr_t{entry.name}), -1)) {
            entry.toggle();
            return;
        }
    }
}

namespace {

// The original copies each 16-byte constant word by word from .rdata.
void copy_vec4(uint32_t* dst, uint32_t source) {
    for (int i = 0; i < 4; ++i) dst[i] = title_global<uint32_t>(source + i * 4);
}

void set_wave_pair(uint32_t index, uint32_t offset, uint32_t x, uint32_t y) {
    uint32_t* set = wave_set(index);
    set[offset / 4] = x;
    set[offset / 4 + 1] = y;
}

void set_wave_value(uint32_t index, uint32_t wave, uint32_t offset, uint32_t value) {
    title_global<uint32_t>(0x00901b30 + offset + (index * 0x34 + wave) * 4) = value;
}

}  // namespace

void XB_THISCALL cdOcean_WaveSet_SetDefaults(uint32_t* set) {
    const uint32_t half = title_global<uint32_t>(0x0083eb24);  // 0.5f
    set[0] = 0;
    set[1] = half;
    set[2] = half;
    set[3] = 0;
    copy_vec4(set + 0x04, 0x007b32d0);  // (0, 0, 0, 0)
    copy_vec4(set + 0x08, 0x007b32d0);
    const uint32_t eight_hundred = title_global<uint32_t>(0x0083f194);  // 800.0f
    for (int i = 0; i < 4; ++i) set[0x0c + i] = eight_hundred;
    for (int i = 0; i < 4; ++i) set[0x10 + i] = eight_hundred;
    copy_vec4(set + 0x14, 0x007b32d0);
    copy_vec4(set + 0x18, 0x007b32d0);
    copy_vec4(set + 0x1c, 0x007b32d0);
    copy_vec4(set + 0x20, 0x007b3290);  // (1, 1, 1, 1)
    copy_vec4(set + 0x24, 0x007b3290);
    copy_vec4(set + 0x28, 0x007b32d0);
    copy_vec4(set + 0x2c, 0x007b32d0);
    copy_vec4(set + 0x30, 0x007b32d0);
}

void XB_CDECL cdOcean_SetBaseHeight(uint32_t index, uint32_t height) { wave_set(index)[0] = height; }

void XB_CDECL cdOcean_SetWaveSetVec04(uint32_t index, uint32_t x, uint32_t y) { set_wave_pair(index, 0x04, x, y); }

void XB_CDECL cdOcean_SetWaveSetVec10(uint32_t index, uint32_t which, uint32_t x, uint32_t y) {
    if (which < 3) set_wave_pair(index, 0x10 + which * 8, x, y);
}

void XB_CDECL cdOcean_SetWaveSetVec30(uint32_t index, uint32_t which, uint32_t x, uint32_t y) {
    if (which < 3) set_wave_pair(index, 0x30 + which * 8, x, y);
}

void XB_CDECL cdOcean_SetWaveOrigin(uint32_t index, uint32_t wave, uint32_t x, uint32_t y) {
    if (wave < 4) set_wave_pair(index, 0x50 + wave * 8, x, y);
}

void XB_CDECL cdOcean_SetWaveDirection(uint32_t index, uint32_t wave, uint32_t value) {
    set_wave_value(index, wave, 0x70, value);
}

void XB_CDECL cdOcean_SetWaveFalloff(uint32_t index, uint32_t wave, uint32_t value) {
    set_wave_value(index, wave, 0x80, value);
}

void XB_CDECL cdOcean_SetWaveLength(uint32_t index, uint32_t wave, uint32_t value) {
    set_wave_value(index, wave, 0x90, value);
}

void XB_CDECL cdOcean_SetWaveAmplitude(uint32_t index, uint32_t wave, uint32_t value) {
    set_wave_value(index, wave, 0xa0, value);
}

void XB_CDECL cdOcean_SetWavePhase(uint32_t index, uint32_t wave, uint32_t value) {
    set_wave_value(index, wave, 0xb0, value);
}

void XB_CDECL cdOcean_SetWaveSpeed(uint32_t index, uint32_t wave, uint32_t value) {
    set_wave_value(index, wave, 0xc0, value);
}

void XB_CDECL cdOcean_ResetWaveSets() {
    for (uint32_t i = 0; i < 4; ++i) cdOcean_WaveSet_SetDefaults(wave_set(i));
}

void XB_CDECL nglShaderList_InitAll() {
    // Vtable slot 0 of every class on the list (no arguments), in list order.
    for (NglShaderClass* c = title_global<NglShaderClass*>(0x00909904); c; c = c->next) {
        title_function<void XB_THISCALL(NglShaderClass*)>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c->vtable[0])))(c);
    }
}

}  // namespace game
