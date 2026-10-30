#include "r_init.h"

#include <cstring>

#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The base constructor's part: vtable 0x00838220, the object added to the registry.
void register_resource_manager(uint8_t* self) {
    field<uint32_t>(self, 0) = 0x00838220;
    uint32_t& count = title_global<uint32_t>(0x009719f8);
    if (static_cast<int32_t>(count) < 20) {
        title_global<uint32_t>(0x009719fc + count * 4) = address_of(self);
        ++count;
    }
}

// A manager link {vtable, previous, 0, 1, 0}.
void init_link(uint8_t* link, uint32_t vtable, uint32_t previous) {
    field<uint32_t>(link, 0) = vtable;
    field<uint32_t>(link, 4) = previous;
    field<uint8_t>(link, 8) = 0;
    field<uint8_t>(link, 9) = 1;
    field<uint8_t>(link, 0xa) = 0;
}

}  // namespace

void XB_CDECL PakRender_Shutdown() {
    const uint32_t state = title_global<uint32_t>(0x0095f638);
    if (state) {
        Cmd_RemoveCommand("PakRender");
        const uint32_t list = title_global<uint32_t>(0x0095f608);
        title_global<uint32_t>(0x0095f60c) = 0;
        title_global<uint32_t>(0x0095f608) = 0;
        if (list) title_function<void XB_CDECL(uint32_t)>(0x000428d0)(list);  // Z_Free
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(state);            // Z_Free
    }
    title_global<uint32_t>(0x0095f638) = 0;
}

void XB_THISCALL ResourceManager_Destruct(uint8_t* self) {
    field<uint32_t>(self, 0) = 0x00838220;
    uint32_t count = title_global<uint32_t>(0x009719f8);
    // The search runs over slot addresses up to registry + count * 4, wrapping as the
    // original's pointer does.
    const uint32_t end = 0x009719fc + count * 4;
    uint32_t slot = 0x009719fc;
    while (slot != end && title_global<uint32_t>(slot) != address_of(self)) slot += 4;
    if (slot == end) return;
    const uint32_t index = (slot - 0x009719fc) >> 2;
    if (static_cast<int32_t>(count) > 1 && static_cast<int32_t>(index) < static_cast<int32_t>(count)) {
        const int32_t last = static_cast<int32_t>(count) - 1;
        title_global<uint32_t>(0x009719fc + index * 4) = title_global<uint32_t>(0x009719fc + (last > 0 ? last : 0) * 4);
        count = title_global<uint32_t>(0x009719f8);
    }
    if (count) title_global<uint32_t>(0x009719f8) = count - 1;
}

PLATFORM_ASM_ENTRY(ResourceManager_Construct_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ResourceManager_Construct) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL ResourceManager_Construct(uint8_t* self) {
    register_resource_manager(self);
    field<uint32_t>(self, 0) = 0x00838300;
    init_link(self + 0x4, 0x0083813c, address_of(nglGetTextureManager()));
    nglSetTextureManager(self + 0x4);
    init_link(self + 0x10, 0x00838184, address_of(nglGetMeshFileManager()));
    nglSetMeshFileManager(self + 0x10);
    init_link(self + 0x1c, 0x008382b8, address_of(nglGetMeshManager()));
    nglSetMeshManager(self + 0x1c);
    // Four list heads: each link keeps the previous head and becomes the head.
    static const uint32_t kLists[][3] = {
        {0x28, 0x0083824c, 0x00909830},
        {0x34, 0x00838118, 0x00909828},
        {0x40, 0x008382dc, 0x0090982c},
        {0x4c, 0x008381a8, 0x00909838},
    };
    for (const auto& list : kLists) {
        init_link(self + list[0], list[1], title_global<uint32_t>(list[2]));
        title_global<uint32_t>(list[2]) = address_of(self + list[0]);
    }
    field<uint32_t>(self, 0x58) = 0;
    std::memset(self + 0x5c, 0, 0x63 * 4);
    return self;
}

PLATFORM_ASM_ENTRY(SceneFX_Init_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(SceneFX_Init) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL SceneFX_Init(uint8_t* self) {
    register_resource_manager(self);
    field<uint32_t>(self, 0) = 0x00832634;
    field<uint32_t>(self, 0x190) = 0;
    field<uint32_t>(self, 0x194) = 0;
    field<uint32_t>(self, 0x198) = 0;
    field<uint32_t>(self, 0x19c) = 0;
    field<float>(self, 0x1a0) = 0.0f;
    field<uint8_t>(self, 0x1a4) = 0;
    uint32_t& count = title_global<uint32_t>(0x00988780);
    if (static_cast<int32_t>(count) < 0x20) {
        title_global<uint32_t>(0x00988784 + count * 4) = 0x0001b9a0;
        ++count;
    }
    Cmd_AddCommand("SceneFX", 0x002afcb0);
    Cmd_AddCommand("RenderEnts", 0x002afc90);
    std::memset(self + 4, 0, 0x63 * 4);
    return self;
}

uint32_t XB_REGS2 GetRefAPI(const uint32_t* imports, uint32_t version) {
    std::memcpy(&title_global<uint32_t>(0x0095e630), imports, 0x22 * 4);  // ri
    uint32_t* const exports = &title_global<uint32_t>(0x0098f220);
    std::memset(exports, 0, 0x28 * 4);
    if (version != 14) {
        title_function<void XB_CDECL(uint32_t, const char*, ...)>(title_global<uint32_t>(0x0095e630))(  // ri.Printf
            0, "Mismatched REF_API_VERSION: expected %i, got %i\n", 14, version);
        return 0;
    }
    // (word index, function)
    static const uint32_t kExports[][2] = {
        {0x00, 0x002372e0},  // RE_Shutdown
        {0x01, 0x0023f0c0},  // RE_BeginRegistration
        {0x02, 0x00243fb0},  // RE_RegisterModel
        {0x05, 0x00236230},  // RE_LoadWorldMap
        {0x0a, 0x0023ba80},
        {0x09, 0x00235e90},  // RE_GetFarCullDist
        {0x18, 0x0023bd90},  // RE_BeginFrame
        {0x19, 0x00235fe0},  // RE_EndFrame
        {0x1a, 0x00235fd0},
        {0x0b, 0x00234cc0},  // RE_ClearScene
        {0x0c, 0x0023ee60},  // RE_AddPolyToScene
        {0x0e, 0x00235dc0},  // RE_SetFarCullDist
        {0x0f, 0x002476b0},  // RE_RenderScene
        {0x11, 0x002361a0},  // RE_SetColor
        {0x12, 0x002360f0},  // RE_StretchPic
        {0x14, 0x00240590},  // RE_StretchPicRotate
        {0x15, 0x00235ff0},  // RE_DrawQuadPic
        {0x1e, 0x00235c60},
        {0x1f, 0x00235c50},
        {0x20, 0x00235f70},
        {0x21, 0x00235f60},
        {0x22, 0x0023fc70},  // RE_TextWidth
        {0x23, 0x00237770},  // RE_TextHeight
        {0x24, 0x0023fc30},  // RE_DrawText
        {0x25, 0x00241a30},  // RE_TextWidthWithIcons
        {0x26, 0x00241860},  // RE_DrawTextWithIcons
        {0x27, 0x0023f570},  // RE_DrawTextExt
    };
    for (const auto& entry : kExports) exports[entry[0]] = entry[1];
    return address_of(exports);
}

namespace {

// _vsprintf / _sprintf / XGAssembleShader (stdcall: source file name, text, length, flags,
// constants, compiled shader, error log, listing, include callback and context, shader type)
// run at the original's ESP: the assembler keeps addresses of the caller's text and locals in
// globals (0x0095f734, 0x00419560).
constexpr uint32_t kVsprintf = 0x001eea95;
constexpr uint32_t kSprintf = 0x001ee901;
constexpr uint32_t kXGAssembleShader = 0x004020aa;

// Assembles the text at `source`; the compiled buffer pointer lands in the word at `top`.
uint32_t assemble_at(uint32_t top, uint32_t source, const TitleRegs& entry) {
    const uint32_t length = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(uintptr_t{source})));
    const uint32_t args[] = {0x0082dfeb, source, length, 0x20000, 0, top, 0, 0, 0, 0, 0};
    const TitleRegs regs{length, entry.ebx, top, source, entry.esi, entry.edi};
    title_call_at_regs(top, kXGAssembleShader, &regs, 11, args);
    return *reinterpret_cast<const uint32_t*>(uintptr_t{top});
}

}  // namespace

// Entries for the assemblers: the C frame 0x20000 below, the entry ESP and EBX/ESI/EDI passed on.
PLATFORM_ASM_ENTRY(nglAssemblePixelShader_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "leal 4(%ebp), %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(nglAssemblePixelShader_body) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL nglAssemblePixelShader_body(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi) {
    // The original's frame: buffer pointer at L, source text at L + 4, formatted text at L + 0x1000.
    const uint32_t* args = reinterpret_cast<const uint32_t*>(uintptr_t{entry + 4});  // code, format, ...
    const uint32_t frame = entry - 0x2004;
    const uint32_t body = frame + 0x1000;
    const uint32_t print_args[] = {body, args[1], entry + 12};
    const TitleRegs print_regs{entry + 12, ebx, args[1], body, esi, edi};
    title_call_at_regs(frame, kVsprintf, &print_regs, 3, print_args);
    char* const source = reinterpret_cast<char*>(uintptr_t{frame + 4});
    std::strcpy(source, "xps.1.1\n");
    std::strcat(source, reinterpret_cast<const char*>(uintptr_t{body}));
    const uint32_t buffer = assemble_at(frame, frame + 4, TitleRegs{0, ebx, 0, 0, esi, edi});
    *reinterpret_cast<uint32_t*>(uintptr_t{args[0]}) = *reinterpret_cast<const uint32_t*>(uintptr_t{buffer + 4});
    if (const auto loaded = renderer_pc_hooks().shader_loaded)
        loaded(args[0], 'p', args[1], *reinterpret_cast<const uint32_t*>(uintptr_t{args[0]}), 0);
    return buffer;
}

PLATFORM_ASM_ENTRY(nglAssembleVertexShader_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "leal 4(%ebp), %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(nglAssembleVertexShader_body) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL nglAssembleVertexShader_body(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi) {
    // The original's frame: buffer pointer at L, fixup text at L + 0x10, source at L + 0x90,
    // formatted text at L + 0x1090; EBX/ESI/EDI saved below L (calls 12 bytes lower).
    const uint32_t* args = reinterpret_cast<const uint32_t*>(uintptr_t{entry + 4});  // tag, out, no_fixup, format, ...
    const uint32_t frame = entry - 0x2084;
    const uint8_t no_fixup = static_cast<uint8_t>(args[2]);
    const uint32_t bx = (ebx & ~0xffu) | no_fixup;
    const uint32_t fixup = frame + 0x10;
    if (no_fixup) {
        *reinterpret_cast<uint8_t*>(uintptr_t{fixup}) = 0;
    } else {
        const uint32_t sprint_args[] = {fixup, 0x007bf69c};
        const TitleRegs sprint_regs{fixup, bx, 0, 0, esi, edi};
        title_call_at_regs(frame - 12, kSprintf, &sprint_regs, 2, sprint_args);
    }
    const uint32_t body = frame + 0x1090;
    const uint32_t print_args[] = {body, args[3], entry + 0x14};
    const TitleRegs print_regs{body, bx, entry + 0x14, args[3], esi, edi};
    title_call_at_regs(frame - 12, kVsprintf, &print_regs, 3, print_args);
    char* const source = reinterpret_cast<char*>(uintptr_t{frame + 0x90});
    std::strcpy(source, "xvs.1.1\n#pragma screenspace\n");
    std::strcat(source, reinterpret_cast<const char*>(uintptr_t{body}));
    if (!no_fixup) std::strcat(source, reinterpret_cast<const char*>(uintptr_t{fixup}));
    const uint32_t buffer = assemble_at(frame, frame + 0x90, TitleRegs{0, ebx, 0, 0, esi, edi});
    uint32_t* const out = reinterpret_cast<uint32_t*>(uintptr_t{args[1]});
    out[0] = *reinterpret_cast<const uint32_t*>(uintptr_t{buffer + 4});
    out[1] = args[0];
    if (const auto loaded = renderer_pc_hooks().shader_loaded) loaded(args[1], 'v', args[3], out[0], args[0]);
    return args[1];
}

namespace {

uint32_t dump_file() { return title_global<uint32_t>(0x00903d5c); }

// "  ROW1".."  ROW4" of a 4x3 matrix (rows 0x10 apart), w printed as 0 0 0 1.
void dump_rows(const float* m) {
    static const uint32_t kRow[] = {0x007c07ec, 0x007c07d8, 0x007c07c4};
    for (uint32_t row = 0; row < 3; ++row)
        nglDumpPrintf(dump_file(), reinterpret_cast<const char*>(uintptr_t{kRow[row]}), double{m[row * 4]},
                      double{m[row * 4 + 1]}, double{m[row * 4 + 2]}, 0.0);
    nglDumpPrintf(dump_file(), "  ROW4 %f %f %f %f\n", double{m[12]}, double{m[13]}, double{m[14]}, 1.0);
}

}  // namespace

void XB_CDECL nglDumpCamera(const float* matrix) {
    nglDumpPrintf(dump_file(), "CAMERA\n");
    dump_rows(matrix);
    nglDumpPrintf(dump_file(), "ENDCAMERA\n");
}

void XB_CDECL nglDumpSceneMeshes(const uint8_t* instance, const float* matrix, const uint8_t* skin) {
    if (field<uint32_t>(const_cast<uint8_t*>(instance), 4) & 0x20000) return;
    nglDumpPrintf(dump_file(), "\n");
    const uint32_t mesh = field<uint32_t>(const_cast<uint8_t*>(instance), 0x34);
    nglDumpPrintf(dump_file(), "MESHFILE %s  // Path: %s\n", reinterpret_cast<const char*>(uintptr_t{mesh + 4}),
                  reinterpret_cast<const char*>(uintptr_t{mesh + 0x20}));
    nglDumpPrintf(dump_file(), "\n");
    nglDumpPrintf(dump_file(), "MODEL %s\n",
                  reinterpret_cast<const char*>(uintptr_t{field<uint32_t>(const_cast<uint8_t*>(instance), 0) + 4}));
    const auto f = [&](uint32_t offset) { return double{*reinterpret_cast<const float*>(skin + offset)}; };
    if (skin && (skin[0] & 2)) nglDumpPrintf(dump_file(), "  SCALE %f %f %f\n", f(0x10), f(0x14), f(0x18));
    dump_rows(matrix);
    if (skin && (skin[0] & 0x3c)) {
        const uint32_t count = *reinterpret_cast<const uint32_t*>(skin + 4);
        nglDumpPrintf(dump_file(), "  NBONES %d\n", count);
        for (uint32_t i = 0; i < *reinterpret_cast<const uint32_t*>(skin + 4); ++i) {
            const float* b =
                reinterpret_cast<const float*>(uintptr_t{*reinterpret_cast<const uint32_t*>(skin + 8) + i * 0x40});
            nglDumpPrintf(dump_file(), "  BONE %d %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f\n", i, double{b[0]},
                          double{b[1]}, double{b[2]}, 0.0, double{b[4]}, double{b[5]}, double{b[6]}, 0.0, double{b[8]},
                          double{b[9]}, double{b[10]}, 0.0, double{b[12]}, double{b[13]}, double{b[14]}, 1.0);
        }
    }
    nglDumpPrintf(dump_file(), "ENDMODEL\n");
}

void XB_CDECL nglCreateNormalCubeMap(uint32_t size) {
    NglTexture*& cube = title_global<NglTexture*>(0x00902cc8);
    uint32_t& cube_size = title_global<uint32_t>(0x00902ccc);
    if (cube_size == size) return;
    if (cube) {
        nglFreeTexture(cube);
        cube = nullptr;
        cube_size = 0;
    }
    NglTexture* const texture = nglCreateTexture(0x10000101, size, size, 0, 0);
    uint8_t hashed[0x20];
    title_function<uint8_t* XB_THISCALL(uint8_t*, const char*)>(0x00011060)(hashed, "nglNormalCubeMapTex");  // HashStr_Init
    std::memcpy(texture->name, hashed, sizeof hashed);
    uint32_t* const pixels = static_cast<uint32_t*>(Mem_AlignedAlloc(size * size * 4, 4, 0));
    const float last = platform_x87_unsigned_mul_mul(size - 1, 1.0f, 1.0f);
    // (index / (size - 1)) * 2 - 1 on the x87.
    const auto coordinate = [&](uint32_t index) {
        return platform_x87_add(platform_x87_div_mul(platform_x87_unsigned_mul_mul(index, 1.0f, 1.0f), last, 2.0f),
                                -1.0f);
    };
    float v[4] = {};
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t* out = pixels;
        for (uint32_t row = 0; row < size; ++row) {
            const float s = coordinate(row);
            for (uint32_t column = 0; column < size; ++column) {
                const float t = coordinate(column);
                switch (face) {  // (x, y, z, w) of the face's direction
                case 0: v[0] = 1.0f, v[1] = 0.0f - s, v[2] = 0.0f - t, v[3] = 1.0f; break;
                case 1: v[0] = -1.0f, v[1] = 0.0f - s, v[2] = t, v[3] = -1.0f; break;
                case 2: v[0] = t, v[1] = 1.0f, v[2] = s, v[3] = t; break;
                case 3: v[0] = t, v[1] = -1.0f, v[2] = 0.0f - s, v[3] = t; break;
                case 4: v[0] = t, v[1] = 0.0f - s, v[2] = 1.0f, v[3] = t; break;
                case 5: v[0] = 0.0f - t, v[1] = 0.0f - s, v[2] = -1.0f, v[3] = 0.0f - t; break;
                }
                const float length2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                float scale = 0.0f;
                if (!(length2 == 0.0f)) {
                    // One Newton step on RSQRTSS.
                    uint32_t bits;
                    std::memcpy(&bits, &length2, 4);
                    bits &= 0x7fffffffu;
                    float a;
                    std::memcpy(&a, &bits, 4);
                    const float r = platform_rsqrt_estimate(a);
                    scale = (3.0f - r * r * a) * r * 0.5f;
                }
                for (float& lane : v) lane *= scale;
                const auto channel = [](float x) { return static_cast<uint32_t>(platform_ftol((x + 1.0f) * 127.5f)); };
                *out++ = (((channel(v[0]) | 0xffffff00u) << 8 | channel(v[1])) << 8) | channel(v[2]);
            }
        }
        const uint32_t surface = title_function<uint32_t XB_STDCALL(uint8_t*, uint32_t, uint32_t)>(0x003ebd70)(
            texture->d3d_texture, face, 0);  // D3DCubeTexture_GetCubeMapSurface2
        uint32_t locked[2] = {};             // D3DLOCKED_RECT {pitch, bits}
        title_function<void XB_STDCALL(uint32_t, uint32_t*, uint32_t, uint32_t)>(0x003ebe50)(surface, locked, 0,
                                                                                            0);  // D3DSurface_LockRect
        title_function<void XB_STDCALL(const void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                       uint32_t)>(0x003fa203)(pixels, 0, 0, locked[1], size, size, 0,
                                                              4);  // XGSwizzleRect
        title_function<uint32_t XB_STDCALL(uint32_t)>(0x003e7ef0)(surface);  // D3DResource_Release
    }
    Mem_AlignedFree(pixels);
    cube = texture;
    cube_size = size;
}

void XB_CDECL nglAllocFrameMemory() {
    // nglSetFrameMemorySize inlined five times, each for a size not set yet.
    if (!title_global<uint32_t>(0x009025a4)) nglSetFrameMemorySize(0, 0x40000, 1);   // scene memory
    if (!title_global<uint32_t>(0x00903c44)) nglSetFrameMemorySize(1, 0x2000, 1);    // block 1
    if (!title_global<uint32_t>(0x00903c18)) nglSetFrameMemorySize(3, 0x20000, 1);   // vertex buffers
    if (!title_global<uint32_t>(0x00903c14)) nglSetFrameMemorySize(2, 0x10000, 1);   // index buffers
    if (!title_global<uint32_t>(0x00903c2c)) nglSetFrameMemorySize(4, 0x180000, 1);  // block 2
}

void XB_CDECL nglInit() {
    using ControlFp = uint32_t XB_CDECL(uint32_t, uint32_t);
    title_function<ControlFp>(0x001f0a40)(0x300, 0x300);      // __controlfp: all x87 exceptions masked
    title_function<ControlFp>(0x001f0a40)(0x20000, 0x30000);  // 24-bit precision
    platform_mxcsr_or(0x6000);                                // SSE: round toward zero
    title_global<uint32_t>(0x00902870) = title_function<uint32_t XB_STDCALL(uint32_t)>(0x003ebb20)(0);
    title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e9d70)(  // D3D_SetPushBufferSize
        title_global<uint32_t>(0x008a34d0), title_global<uint32_t>(0x008a34d4));
    if (!title_global<uint8_t>(0x0090289b)) {
        const uint32_t modes[] = {4, 3, 2, 1, 0, 0x10000, 0x10001, 0x10002};
        nglSelectVideoMode(modes, 8);
    }
    // D3DPRESENT_PARAMETERS at 0x00902750.
    uint32_t* const present = &title_global<uint32_t>(0x00902750);
    std::memset(present, 0, 0x11 * 4);
    present[0] = title_global<uint32_t>(0x00902890);  // width
    present[1] = title_global<uint32_t>(0x00902894);  // height
    present[2] = 0x12;                                // back buffer format
    present[3] = 1;                                   // back buffer count
    present[4] = 0x11;                                // multisample type
    present[5] = 2;                                   // swap effect
    present[8] = 1;                                   // auto depth stencil
    present[9] = 0x2a;                                // its format
    present[10] = (title_global<uint8_t>(0x0090289a) ? 0x100 : 0) | (title_global<uint8_t>(0x00902898) ? 0x40 : 0x20) |
                  (title_global<uint8_t>(0x00902899) ? 0x10 : 0);  // flags (ngl_widescreen: 0x10)
    present[12] = 0x80000002;                                       // presentation interval
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x003e9cc0)(
        0, 1, 0, 0x40, present, 0x00902874);                                                  // Direct3D_CreateDevice
    title_function<void XB_STDCALL(uint32_t, uint32_t)>(0x003e5770)(0, 0x008a29b0);           // D3DDevice_SetGammaRamp
    title_function<void XB_STDCALL(uint32_t)>(0x003e5090)(0x00902798);                        // D3DDevice_GetDeviceCaps
    nglAllocFrameMemory();
    const float sx = platform_x87_unsigned_mul_mul(title_global<uint32_t>(0x00902890), 0x1.99999ap-10f /* 0x3acccccd, 1/640 */, 1.0f);
    title_global<float>(0x009028c4) = sx;
    const float sy = platform_x87_unsigned_mul_mul(title_global<uint32_t>(0x00902894), 0x1.111112p-9f /* 0x3b088889, 1/480 */, 1.0f);
    title_global<float>(0x009028c8) = sy;
    if (!(sx == 1.0f) || !(sy == 1.0f)) title_global<uint8_t>(0x009028c0) = 1;
    nglResetFrameStats();
    nglInitMeshManagers();
    nglInitRenderStates(&title_global<uint8_t>(0x00902f08));
    // Texture stage states of the four stages (D3D's state block, 0x80 bytes per stage).
    uint32_t dirty = title_global<uint32_t>(0x003f8320);
    for (uint32_t stage = 0; stage < 4; ++stage) {
        uint32_t* const state = &title_global<uint32_t>(0x003f5ca8 + stage * 0x80);
        state[-4] = 1;
        state[0] = 1;
        state[5] = 0;
        state[-12] = 2;
        state[-13] = 2;
        state[-11] = 2;
        dirty |= (1u << stage) | 0xc00;
    }
    title_global<uint32_t>(0x003f8320) = dirty;
    nglInitShaderConstants(1);
    nglInitTextureManager();
    title_function<void XB_CDECL()>(0x00093be0)();
    nglInitFrameBuffers();
    for (uint32_t i = 0; i < title_global<uint32_t>(0x0090275c) + 1; ++i) {
        nglSetRenderTargetCached(nglGetBackBufferTexture(), 0, nullptr, 6);
        title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, float, uint32_t)>(0x003ead90)(
            0, 0, 0xf3, 0, 1.0f, 0);                                  // D3DDevice_Clear
        title_function<void XB_STDCALL(uint32_t)>(0x003eabe0)(0);  // D3DDevice_Swap
    }
    title_function<void XB_STDCALL(uint32_t)>(0x003e6260)(0x000643e0);  // D3DDevice_SetVerticalBlankCallback
    nglInitMultisample(0x11);
    title_global<uint32_t>(0x0090286c) = title_function<uint32_t XB_STDCALL()>(0x003e68f0)();  // D3DDevice_InsertFence
    nglCreateFogTextures(0x100, 0x100, 0x100, 0x40);
    nglDynamicVB_BeginFrame(&title_global<uint32_t>(0x00902938));
    title_function<void XB_CDECL()>(0x0006da20)();  // nglInitDebugMeshes
    nglDynamicVB_EndFrame(&title_global<uint32_t>(0x00902938));
}

uint8_t* XB_THISCALL SceneFX_DeletingDtor(uint8_t* self, uint32_t flags) {
    field<uint32_t>(self, 0) = 0x00832634;  // SceneFX vtable
    ResourceManager_Destruct(self);
    if (flags & 1) title_function<void XB_CDECL(void*)>(0x000428d0)(self);  // Z_Free
    return self;
}

}  // namespace game
