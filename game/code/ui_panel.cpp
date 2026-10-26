#include "ui_panel.h"

#include <cstring>

#include "fe.h"
#include "ngl.h"
#include "qcommon.h"
#include "script_string.h"
#include "title_data.h"
#include "tr_main.h"

PLATFORM_ASM_ENTRY(ui_call_regs,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 20(%ebp), %edx\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "pushl (%edx,%ecx,4)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 4(%eax), %ebx\n"
                   "movl 8(%eax), %ecx\n"
                   "movl 12(%eax), %edx\n"
                   "movl 16(%eax), %esi\n"
                   "movl 20(%eax), %edi\n"
                   "movl (%eax), %eax\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename F>
F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

void call(uint8_t* object, uint32_t offset) { method<void XB_THISCALL(uint8_t*)>(object, offset)(object); }
void call(uint8_t* object, uint32_t offset, uint32_t a) {
    method<void XB_THISCALL(uint8_t*, uint32_t)>(object, offset)(object, a);
}

void* z_malloc(uint32_t size) { return title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(size); }
void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }

// PtrVector (data, capacity, count): Resize (0x00013970: vector in ESI, count in EBX), PushBack (0x000138c0: vector
// in ESI, a pointer to the value on the stack).
void ptrvector_resize(uint8_t* vector, uint32_t count) {
    TitleRegs regs{};
    regs.esi = address_of(vector);
    regs.ebx = count;
    ui_call_regs(0x00013970, &regs, 0, nullptr);
}
void ptrvector_push(uint8_t* vector, uint8_t* const* value) {
    TitleRegs regs{};
    regs.esi = address_of(vector);
    const uint32_t args[1] = {address_of(value)};
    ui_call_regs(0x000138c0, &regs, 1, args);
}

// Little-endian reads at data + *offset, advancing it.
uint8_t read_u8(const uint8_t* data, uint32_t* offset) {
    const uint8_t v = data[*offset];
    *offset += 1;
    return v;
}
int16_t read_i16(const uint8_t* data, uint32_t* offset) {
    const uint32_t o = *offset;
    *offset = o + 2;
    return int16_t(uint16_t(data[o] | (data[o + 1] << 8)));
}
uint32_t read_u32(const uint8_t* data, uint32_t* offset) {
    const uint32_t o = *offset;
    *offset = o + 4;
    return uint32_t(data[o]) | (uint32_t(data[o + 1]) << 8) | (uint32_t(data[o + 2]) << 16) |
           (uint32_t(data[o + 3]) << 24);
}

// 0x00316480 (offset in ECX, data in EAX, out in EDI): three floats (x, y, z).
void read_vec3(const uint8_t* data, uint32_t* offset, float* out) {
    TitleRegs regs{};
    regs.eax = address_of(data);
    regs.ecx = address_of(offset);
    regs.edi = address_of(out);
    ui_call_regs(0x00316480, &regs, 0, nullptr);
}

float bits_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t float_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

}  // namespace

PLATFORM_ASM_ENTRY(Panel_Draw_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Panel_Draw) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Panel_Draw(uint8_t* panel) {
    for (uint32_t i = 0; i < field<uint32_t>(panel, 8); ++i) {
        if (i == field<uint32_t>(panel, 0x18)) continue;
        call(field<uint8_t**>(panel, 0)[i], 0x44);
    }
    if (panel[0x1c]) return;
    for (uint32_t i = 0; i < field<uint32_t>(panel, 0x14); ++i) {
        uint8_t* const text = field<uint8_t**>(panel, 0xc)[i];
        if (method<uint8_t XB_THISCALL(uint8_t*)>(text, 0x44)(text)) continue;
        call(field<uint8_t**>(panel, 0xc)[i], 0x3c);
    }
}

PLATFORM_ASM_ENTRY(Panel_SetLayer_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Panel_SetLayer) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL Panel_SetLayer(uint8_t* panel, uint32_t layer) {
    using GetDepth = float XB_THISCALL(uint8_t*);
    using SetDepthLayer = void XB_THISCALL(uint8_t*, float, uint32_t);
    for (uint32_t i = 0; i < field<uint32_t>(panel, 8); ++i) {
        uint8_t* const element = field<uint8_t**>(panel, 0)[i];
        const float depth = method<GetDepth>(element, 0x28)(element);
        method<SetDepthLayer>(element, 0x1c)(element, depth, layer);
    }
    for (uint32_t i = 0; i < field<uint32_t>(panel, 0x14); ++i) {
        uint8_t* const element = field<uint8_t**>(panel, 0xc)[i];
        const float depth = method<GetDepth>(element, 0x28)(element);
        method<SetDepthLayer>(element, 0x1c)(element, depth, layer);
    }
}

PLATFORM_ASM_ENTRY(Panel_TrianglePairToQuad_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Panel_TrianglePairToQuad) "addl $16, %esp\n"
                   "ret")

int32_t XB_CDECL Panel_TrianglePairToQuad(int32_t a, const int16_t* indices, int32_t b, int32_t* out) {
    uint8_t used[3] = {0, 0, 0};
    int32_t count = 0;
    for (int32_t i = 0; i < 3; ++i) {
        bool shared = false;
        for (int32_t j = 0; j < 3; ++j) {
            if (used[j]) continue;
            const int16_t corner = indices[a * 3 + i];
            if (corner != indices[b * 3 + j]) continue;
            shared = true;
            ++count;
            used[j] = 1;
            out[count] = corner;
        }
        if (!shared) out[0] = indices[a * 3 + i];
    }
    for (int32_t j = 0; j < 3; ++j) {
        if (!used[j]) out[3] = indices[b * 3 + j];
    }
    return count;
}

PLATFORM_ASM_ENTRY(Panel_ReadString_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Panel_ReadString) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Panel_ReadString(uint32_t* offset, const uint8_t* data, ScriptString* string) {
    const int32_t length = read_i16(data, offset);
    for (int32_t n = length; n > 0; --n) {
        const uint8_t c = read_u8(data, offset);
        title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011eb90)(string, c);  // ScriptString_AppendChar
    }
}

void XB_REGS1 XB_CDECL Panel_FreeElements(uint8_t* panel) {
    for (int32_t i = 0; i < field<int32_t>(panel, 8); ++i) {
        uint8_t* const picture = field<uint8_t**>(panel, 0)[i];
        if (picture) call(picture, 0, 1);
    }
    ptrvector_resize(panel, 0);
    for (int32_t i = 0; i < field<int32_t>(panel, 0x14); ++i) {
        uint8_t* const text = field<uint8_t**>(panel, 0xc)[i];
        if (text) call(text, 0, 1);
    }
    ptrvector_resize(panel + 0xc, 0);
}

void Panel_Delete(uint8_t* panel) {
    Panel_FreeElements(panel);
    if (void* const texts = field<void*>(panel, 0xc)) {
        Mem_AlignedFree(texts);
        field<uint32_t>(panel, 0xc) = 0;
    }
    if (void* const pictures = field<void*>(panel, 0)) {
        Mem_AlignedFree(pictures);
        field<uint32_t>(panel, 0) = 0;
    }
    z_free(panel);
}

PLATFORM_ASM_ENTRY(Panel_GetDefaultPicture_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Panel_GetDefaultPicture) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Panel_GetDefaultPicture(uint8_t* root) {
    if (!field<uint8_t*>(root, 0x34)) {
        uint8_t* picture = static_cast<uint8_t*>(z_malloc(0x40));
        if (picture) picture = UIPicture_Init(picture, reinterpret_cast<const char*>(uintptr_t{0x0082d26c}));  // "default_PQ"
        const float points[12] = {0, 0, 0, 100, 0, 0, 100, 100, 0, 0, 100, 0};
        const uint32_t colors[4] = {0xff80ff80, 0xff80ff80, 0xff80ff80, 0xff80ff80};
        field<uint8_t*>(root, 0x34) = picture;
        using AddQuad = void XB_THISCALL(uint8_t*, const float*, const uint32_t*, uint32_t, uint32_t, const char*);
        method<AddQuad>(picture, 0x3c)(picture, points, colors, 0, 0,
                                       reinterpret_cast<const char*>(uintptr_t{0x0082dfeb}));  // ""
    }
    return field<uint8_t*>(root, 0x34);
}

void XB_REGS1 XB_STDCALL Panel_CreateText(uint32_t* offset, uint8_t* panel, const uint8_t* data,
                                          const float* matrix, const char* name, uint32_t align) {
    ScriptString font_name;
    ScriptString_FromRep(&font_name, nullptr);
    Panel_ReadString(offset, data, &font_name);
    const uint8_t* const rgba = data + *offset;
    const uint32_t colour = uint32_t(rgba[2]) | (uint32_t(rgba[1]) << 8) | (uint32_t(rgba[0]) << 16) |
                            (uint32_t(rgba[3]) << 24);
    *offset += 4;
    const uint8_t alignment = read_u8(data, offset);
    const int32_t width = int32_t(read_u32(data, offset));
    const float scale = bits_float(read_u32(data, offset));
    const int32_t mode = read_i16(data, offset);
    const uint32_t value = read_u32(data, offset);
    const uint32_t zero = title_global<uint32_t>(0x00922eb8);
    float position[3];
    float size[3];
    std::memcpy(&position[0], &zero, 4);
    std::memcpy(&position[1], &zero, 4);
    std::memcpy(&position[2], &zero, 4);
    std::memcpy(&size[0], &zero, 4);
    std::memcpy(&size[1], &zero, 4);
    read_vec3(data, offset, position);
    read_vec3(data, offset, size);
    const int32_t font = R_FindFont(font_name.rep ? font_name.rep->text : nullptr, 0);

    uint32_t horz = 0;
    switch (alignment & 3) {
    case 0: horz = 0x10; break;
    case 3: horz = 0x20; break;
    default: break;
    }
    uint32_t vert = 0;
    switch (alignment & 0xc) {
    case 0: vert = 0x40; break;
    case 0xc: vert = 0x80; break;
    default: break;
    }
    float x = position[0];
    if (horz == 0x10) {
        x = x - size[0];
    } else if (horz == 0x20) {
        x = x + size[0];
    }
    float y = position[1];
    if (vert == 0x40) {
        y = y - size[1];
    } else if (vert == 0x80) {
        y = y + size[1];
    }
    const float* const t = matrix + 12;
    const float sx = t[0] + x;
    const float sy = y + t[1];
    const float sz = position[2] + t[2];

    uint8_t* text;
    if (mode == 1) {
        text = static_cast<uint8_t*>(z_malloc(0x60));
        if (text) {
            text = UIText_Init(text, font, reinterpret_cast<const char*>(uintptr_t{0x0082dfeb}), float_bits(sx),
                               float_bits(sy), int32_t(sz), 8, float_bits(scale), uint8_t(horz), uint8_t(vert),
                               colour);
        }
    } else {
        text = static_cast<uint8_t*>(z_malloc(0x98));
        if (text) {
            text = UIText_Construct(text, font, float_bits(sx), float_bits(sy), int32_t(sz), 8, float_bits(scale),
                                    colour, vert, horz);
        }
        call(text, 0x13c, uint32_t(mode));
        call(text, 0x130, 1);
        const int32_t wrap = scale == 0.0f ? width : int32_t(float(width) / scale);
        call(text, 0xe4, uint32_t(wrap));
        call(text, 0x11c, uint32_t(int32_t(size[0] * 2.0f)));
    }
    call(text, 0x68, address_of(name));
    call(text, 0x18, align);
    field<float>(text, 0x20) = x;
    field<float>(text, 0x24) = y;
    field<uint32_t>(text, 0x28) = 0;
    call(text, 0x8c, value);
    ptrvector_push(panel + 0xc, &text);
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&font_name);  // ScriptString_Release
}

void XB_REGS1 XB_STDCALL Panel_CreatePicture(uint32_t* offset, uint8_t* panel, const uint8_t* data,
                                             const float* matrix, const char* name, uint32_t align) {
    const int32_t material_count = read_i16(data, offset);
    uint8_t* const block = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c30)(
        uint32_t(material_count) * 16 + 4);  // Heap_Alloc16
    uint8_t* materials = nullptr;
    if (block) {
        field<int32_t>(block, 0) = material_count;
        materials = block + 4;
        for (int32_t i = 0; i < material_count; ++i) {
            field<uint32_t>(materials, i * 16 + 8) = 0;
            field<uint32_t>(materials, i * 16) = 0;
        }
    }
    for (int32_t i = 0; i < material_count; ++i) {
        uint8_t* const record = materials + i * 16;
        field<uint32_t>(record, 0) = 0;
        TitleRegs regs{};
        regs.eax = address_of(data);
        regs.edi = address_of(offset);
        const uint32_t args[1] = {address_of(record)};
        ui_call_regs(0x0032c0b0, &regs, 1, args);  // the material record
    }
    const int32_t mesh_count = read_i16(data, offset);
    for (int32_t i = 0; i < mesh_count; ++i) {
        uint8_t* picture = static_cast<uint8_t*>(z_malloc(0x40));
        if (picture) {
            const uint32_t one = title_global<uint32_t>(0x0083eb20);  // 1.0
            const uint32_t zero = title_global<uint32_t>(0x00922eb8);
            field<uint32_t>(picture, 4) = one;
            picture[0x10] = 4;
            field<uint32_t>(picture, 0) = 0x0082a2c0;  // UIPicture vtable
            field<uint32_t>(picture, 0x14) = zero;
            field<uint32_t>(picture, 0x18) = zero;
            field<uint32_t>(picture, 0x1c) = zero;
            field<uint32_t>(picture, 0x20) = 0;
            field<uint32_t>(picture, 0x24) = 0;
            field<uint32_t>(picture, 0x28) = 0;
            ScriptString_FromRep(reinterpret_cast<ScriptString*>(picture + 0x3c), nullptr);
            field<float>(picture, 0x30) = 0.0f;
            field<uint32_t>(picture, 0x34) = one;
            field<uint32_t>(picture, 0x38) = one;
            field<uint32_t>(picture, 0x2c) = 0;
        }
        title_function<void XB_THISCALL(uint8_t*, const char*)>(0x0011dd20)(picture + 0x3c,
                                                                            name);  // ScriptString_AssignCString
        call(picture, 0x18, align);
        using LoadMesh = void XB_THISCALL(uint8_t*, const uint8_t*, const uint8_t*, uint32_t*, const float*);
        method<LoadMesh>(picture, 0x40)(picture, materials, data, offset, matrix);
        ptrvector_push(panel, &picture);
    }
    if (materials) {
        const int32_t count = field<int32_t>(materials, -4);
        for (int32_t i = count - 1; i >= 0; --i) {
            void* const texture = field<void*>(materials, i * 16);
            if (texture) nglReleaseTexture(texture);
        }
        z_free(materials - 4);
    }
}

void XB_THISCALL Panel_LoadNode_r(uint8_t* panel, const uint8_t* data, uint32_t* offset, const float* matrix) {
    const uint8_t type = read_u8(data, offset);
    TitleRegs regs{};
    regs.eax = address_of(offset);
    regs.ecx = address_of(data);
    const char* const name = reinterpret_cast<const char*>(
        uintptr_t{ui_call_regs(0x00316530, &regs, 0, nullptr)});  // the name, made a C string in place
    alignas(16) float local[16];
    regs = TitleRegs{};
    regs.esi = address_of(offset);
    const uint32_t args[2] = {address_of(local), address_of(data)};
    ui_call_regs(0x00321430, &regs, 2, args);  // the node transform (four rows)

    // Each row of the parent times the node's transform; the translation row adds the node's.
    alignas(16) float out[16];
    for (int32_t r = 0; r < 4; ++r) {
        const float* const a = matrix + r * 4;
        for (int32_t c = 0; c < 4; ++c) {
            const float px = local[c] * a[0];
            const float py = local[4 + c] * a[1];
            const float pz = local[8 + c] * a[2];
            out[r * 4 + c] = r < 3 ? (px + py) + pz : ((local[12 + c] + px) + py) + pz;
        }
    }

    const uint16_t align = read_u8(data, offset);
    const int32_t children = int32_t(read_u32(data, offset));
    for (int32_t i = children; i > 0; --i) Panel_LoadNode_r(panel, data, offset, out);
    if (type == 0x91) {
        Panel_CreatePicture(offset, panel, data, out, name, align);
    } else if (type == 0x94) {
        Panel_CreateText(offset, panel, data, out, name, align);
    }
}

PLATFORM_ASM_ENTRY(Panel_Load_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Panel_Load) "addl $12, %esp\n"
                   "ret $4")

uint8_t XB_CDECL Panel_Load(const uint8_t* data, uint8_t* panel, const char* name) {
    if (!data) return 0;
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(reinterpret_cast<char*>(panel + 0x1d),
                                                                           name, 0x40);  // _strncpy
    const float s = title_global<float>(0x0082e140);
    alignas(16) float matrix[16];
    for (int32_t c = 0; c < 4; ++c) {
        matrix[c] = title_global<float>(0x0082e160 + c * 4) * s;
        matrix[4 + c] = title_global<float>(0x0082e170 + c * 4) * s;
        matrix[8 + c] = title_global<float>(0x0082e180 + c * 4) * s;
    }
    std::memcpy(&matrix[12], &title_global<uint32_t>(0x0082e290), 16);
    uint32_t offset = 0;
    const int32_t count = int32_t(read_u32(data, &offset));
    for (int32_t i = count; i > 0; --i) Panel_LoadNode_r(panel, data, &offset, matrix);
    return 1;
}

}  // namespace game
