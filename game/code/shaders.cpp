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

}  // namespace game
