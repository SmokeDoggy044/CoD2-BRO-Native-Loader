#include "shaders.h"

#include <cstring>

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

}  // namespace game
