#include "shaders.h"

#include <cstring>

#include "lanes.h"
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
    title_function<void XB_STDCALL(uint32_t)>(0x003e80f0)(mode);
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
        title_function<void XB_STDCALL(uint32_t)>(0x003e8090)(color);  // SetRenderState_FogColor
        cached_color = color;
    }
    uint8_t& fog_enabled = title_global<uint8_t>(0x00902f93);
    if (fog_enabled != enable) {
        title_global<uint32_t>(0x003f8320) |= 0x2000;
        title_global<uint32_t>(0x003f8498) = enable;
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
    title_function<SetConstants>(0x003e7490)(6, draw_node(node)->world_view_projection, 16);  // NotInlineFast
}

// The material's texture (+0x1c) on stage 0, and stage 0's texture states marked dirty.
void set_material_texture(const NglMeshNode* node) {
    nglSetTexture(0, *reinterpret_cast<NglTexture* const*>(node->material + 0x1c), 2, 3);
    title_global<uint32_t>(0x003f8320) |= 1;
    title_global<uint32_t>(0x003f5c68) = 1;
    title_global<uint32_t>(0x003f5c6c) = 1;
}

const NglShaderRef* shader_ref(uint32_t address) { return &title_global<NglShaderRef>(address); }

// The pixel program variant: 0x00045620 returns the flag at 0x00901e94.
const NglShaderRef* pick_shader(uint32_t if_one, uint32_t otherwise) {
    return shader_ref(title_function<uint32_t XB_CDECL()>(0x00045620)() == 1 ? if_one : otherwise);
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
    title_function<void XB_CDECL(NglMeshDrawNode*)>(0x0006fe00)(draw);  // nglSelectMeshLightSet
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    std::memcpy(constants, draw->world_view_projection, 64);
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    title_function<SetConstants>(0x003e7490)(6, constants, 48);  // NotInlineFast
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
        title_global<uint32_t>(0x003f5ce8) = 3;
        title_global<uint32_t>(0x003f5cec) = 3;
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
    title_global<uint32_t>(0x003f5c68) = (flags & 0x40) ? 3 : 1;
    title_global<uint32_t>(0x003f5c6c) = (flags & 0x80) ? 3 : 1;
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
    title_function<void XB_CDECL(NglMeshDrawNode*)>(0x0006fe00)(draw);  // nglSelectMeshLightSet
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    std::memcpy(constants, draw->world_view_projection, 64);
}

void set_vertex_constants(uint32_t first, const float* constants, uint32_t count) {
    using SetConstants = void XB_FASTCALL(uint32_t, const float*, uint32_t);
    title_function<SetConstants>(0x003e7490)(first, constants, count);  // NotInlineFast
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
    title_function<void XB_CDECL(NglMeshDrawNode*)>(0x0006fe00)(draw);  // nglSelectMeshLightSet
    nglSetTextureAnimFrame(node);
    set_material_texture(node);
    nglSetVertexShaderCached(shader_ref(0x00902054));
    std::memcpy(constants, draw->world_view_projection, 64);
    nglComputeObjectLights(draw, constants + 16, constants + 32);
    nglSetPixelShaderProgramCached(pick_shader(0x00902050, 0x0090204c));
    set_vertex_constants(6, constants, 48);
    using SetSkinning = void XB_CDECL(int32_t, NglMeshDrawNode*, const NglMeshSection*);
    title_function<SetSkinning>(0x00076620)(-0x4c, draw, node->section);  // nglSetSkinningConstants
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
        title_global<uint32_t>(0x003f8498) = 0;
        fog_enabled = 0;
    }
    set_world_view_projection(node);
    using SetConstant1 = void XB_FASTCALL(uint32_t, const float*);
    title_function<SetConstant1>(0x003e7440)(0xc, color);  // SetVertexShaderConstant1Fast
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
    title_global<uint32_t>(0x003f5ce8) = 3;
    title_global<uint32_t>(0x003f5cec) = 3;
    nglSetVertexShaderCached(shader_ref(0x009021a4));
    const bool reflective = *reinterpret_cast<NglTexture* const*>(node->material + 0x24) != nullptr &&
                            title_function<uint32_t XB_CDECL()>(0x00045620)() != 1;
    nglSetPixelShaderProgramCached(shader_ref(reflective ? 0x009021a0 : 0x009021ac));
    set_transform_constants(-0x56, node);
    set_scene_fog();
    nglDrawMeshSection(node->section);
}

void XB_THISCALL cdOcean_Draw(const NglMeshNode* node) {
    title_global<uint32_t>(0x009026fc) = current_scene()->frame_counter;
    set_cull_mode(0x900);
    // Alpha test and blending off, D3D's own render state copies too.
    uint8_t* cache = state_cache();
    if (cache[0x50] != 0) {
        title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(0x00040300, 0);  // SetRenderState_Simple
        title_global<uint32_t>(0x003f8418) = 0;
        cache[0x50] = 0;
    }
    if (cache[0x5c] != 0) {
        title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(0x00040304, 0);
        title_global<uint32_t>(0x003f8414) = 0;
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
    title_global<uint32_t>(0x003f5c68) = 1;
    title_global<uint32_t>(0x003f5c6c) = 1;
    if (NglTexture* t = *reinterpret_cast<NglTexture* const*>(node->material + 0x28); t && (stages & 1)) {
        nglSetTexture(1, t, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 2;
        title_global<uint32_t>(0x003f5ce8) = 1;
        title_global<uint32_t>(0x003f5cec) = 1;
    } else {
        stages &= ~1u;
    }
    if (NglTexture* t = *reinterpret_cast<NglTexture* const*>(node->material + 0x30); t && (stages & 2)) {
        nglSetTexture(2, t, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 4;
        title_global<uint32_t>(0x003f5d68) = 1;
        title_global<uint32_t>(0x003f5d6c) = 1;
    } else {
        stages &= ~2u;
    }
    if (NglTexture* t = *reinterpret_cast<NglTexture* const*>(node->material + 0x38); t && (stages & 4)) {
        nglSetTexture(3, t, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 8;
        title_global<uint32_t>(0x003f5de8) = 3;
        title_global<uint32_t>(0x003f5dec) = 3;
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
        title_function<uint32_t XB_CDECL()>(0x00045620)() == 1
            ? reinterpret_cast<GetShader*>(vtable[4])(node)
            : reinterpret_cast<GetPixelShader*>(vtable[3])(node, stages & 1, stages >> 1 & 1, stages >> 2 & 1);
    nglSetPixelShaderProgramCached(pixel_shader);
    title_function<void XB_THISCALL(const NglMeshNode*)>(0x0004f810)(node);  // cdOcean_SetWaveConstants
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
    title_global<uint32_t>(0x003f5c68 + stage * 0x80) = 4;
    title_global<uint32_t>(0x003f5c6c + stage * 0x80) = 4;
    title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e8ba0)(stage, 0);  // SetTextureState_BorderColor
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
    title_global<uint32_t>(0x003f5c68) = 3;
    title_global<uint32_t>(0x003f5c6c) = 3;
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
    title_function<void XB_CDECL(NglMeshDrawNode*)>(0x0006fe00)(draw);  // nglSelectMeshLightSet

    // The mesh's bounding-sphere centre (+0x20) in world space.
    const auto world_center = [draw] { return transform_point(draw->world, reinterpret_cast<const float*>(draw->mesh + 0x20)); };
    int32_t light_count = 0;
    for (NglLight* light = title_global<NglLightSet*>(0x00902e44)->object_lights;
         light != reinterpret_cast<NglLight*>(title_global<NglLightSet*>(0x00902e44));) {
        alignas(16) float center[4];
        alignas(16) float out[4];
        std::memcpy(center, world_center().v, 16);
        const float* v = nglLightVectorWorld(out, light, center);
        std::memcpy(lights + light_count * 4, v, 12);
        std::memcpy(lights + light_count * 4 + 3, &light->flags, 4);
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
    if (title_function<uint32_t XB_CDECL()>(0x00045620)() == 1) lightmap = false;
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
            for (uint32_t i = 0; i < 3; ++i) title_global<uint32_t>(0x003f5c68 + stage * 0x80 + i * 4) = 3;
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
        title_global<uint32_t>(0x003f5ce8) = 3;
        title_global<uint32_t>(0x003f5cec) = 3;
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
    title_global<uint32_t>(0x003f5ce8) = 1;
    title_global<uint32_t>(0x003f5cec) = 1;
    if (NglTexture* third = *reinterpret_cast<NglTexture* const*>(node->material + 0x2c)) {
        nglSetTexture(2, third, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 4;
        title_global<uint32_t>(0x003f5d68) = 3;
        title_global<uint32_t>(0x003f5d6c) = 3;
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
    title_global<uint32_t>(0x003f5ce8) = 1;
    title_global<uint32_t>(0x003f5cec) = 1;
    nglSetVertexShaderCached(shader_ref(0x00902080));
    nglSetPixelShaderProgramCached(shader_ref(0x00902078));
    NglMeshDrawNode* draw = draw_node(node);
    title_function<void XB_CDECL(NglMeshDrawNode*)>(0x0006fe00)(draw);  // nglSelectMeshLightSet
    title_function<void XB_CDECL(int32_t, NglMeshDrawNode*)>(0x000765c0)(-0x53, draw);  // nglSetObjectLightConstants
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
    title_function<void XB_STDCALL(uint32_t, const float*, uint32_t)>(0x003e7a50)(0, shine, 1);  // SetPixelShaderConstant
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
            title_global<uint32_t>(0x003f8498) = 0;
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
        title_function<void XB_STDCALL(uint32_t)>(0x003e8210)(0);  // SetRenderState_ZEnable
        z_enable = 0;
    }
    if (z_write != 0) {
        title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(0x0004035c, 0);  // SetRenderState_Simple
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
        title_function<void XB_STDCALL(uint32_t)>(0x003e8210)(saved_z_enable);
        z_enable = saved_z_enable;
    }
    if (z_write != saved_z_write) {
        title_function<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(0x0004035c, saved_z_write);
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
    title_global<uint32_t>(0x003f5c68) = 1;
    title_global<uint32_t>(0x003f5c6c) = 1;
    if (NglTexture* second = *reinterpret_cast<NglTexture* const*>(node->material + 0x24)) {
        nglSetTexture(1, second, 2, 3);
        title_global<uint32_t>(0x003f8320) |= 2;
        title_global<uint32_t>(0x003f5ce8) = 1;
        title_global<uint32_t>(0x003f5cec) = 1;
    }
    nglSetVertexShaderCached(shader_ref(0x00901fd8 + (variant + lit * 2) * 8));
    nglSetPixelShaderProgramCached(shader_ref(0x00901fd0 + variant * 4));
    NglMeshDrawNode* draw = draw_node(node);
    title_function<void XB_CDECL(NglMeshDrawNode*)>(0x0006fe00)(draw);  // nglSelectMeshLightSet
    if (lit) title_function<void XB_CDECL(int32_t, NglMeshDrawNode*)>(0x000765c0)(-0x4f, draw);  // nglSetObjectLightConstants

    // Constants 6..16: the matrix, the world-view rotation's rows (w 0, the last w 1),
    // the first object light's vector in object space (w 0), the camera in object
    // space (w 1), then the material's +0x2c in the first lane (the title leaves the
    // other three uninitialized: zeros here, and in --compare test processes).
    alignas(16) float c[44] = {};
    std::memcpy(c, draw->world_view_projection, 64);
    alignas(16) float buffer[16];
    using GetMatrix = const float* XB_THISCALL(NglMeshDrawNode*, float*);
    const float* world_view = title_function<GetMatrix>(0x00049280)(draw, buffer);  // nglNode_GetWorldViewRotation
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
    title_function<void XB_STDCALL(uint32_t, const float*, uint32_t)>(0x003e7a50)(0, pixel, 2);  // SetPixelShaderConstant
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

}  // namespace game
