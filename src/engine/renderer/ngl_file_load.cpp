// The NGL resource managers' Load / Release slots (vtable slots 7 and 8) for resources read from their own
// files (unreached by the recordings: the levels' packs hold everything; nglLoadAnimatedTextureList's .ifl
// frames are the one caller of the texture manager's Load).

#include <cstring>

#include "ngl.h"
#include "ngl_gfx.h"
#include "qcommon.h"
#include "title_crt.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& word(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(uintptr_t{address});
}

const char* title_string(uint32_t address) { return pointer<const char>(address); }

// Manager slot 3 (add) / slot 4 (remove), thiscall.
void manager_add(void* manager, void* object) {
    title_function<void XB_THISCALL(void*, void*)>(word<uint32_t>(pointer<void>(word<uint32_t>(manager, 0)), 0xc))(
        manager, object);
}

void manager_remove(void* manager, void* object) {
    title_function<void XB_THISCALL(void*, void*)>(word<uint32_t>(pointer<void>(word<uint32_t>(manager, 0)), 0x10))(
        manager, object);
}

// The resource's name (an 8-word hashed name; text from +4).
const char* key_text(const uint32_t* key) { return reinterpret_cast<const char*>(key + 1); }

// nglMeshFileManager_Load / nglMeshFileCache_Load: a mesh file object (0x148 bytes: name, +0x20 the folder,
// +0x120 references 1, +0x124 the file buffer, +0x130 = 0) read from "<folder><name><extension>".
uint8_t* load_mesh_file(void* manager, const uint32_t* key, uint32_t extension) {
    const char* folder = title_string(0x00902980);
    char path[0x100];
    title_snprintf(path, sizeof path, title_string(0x007bd0c0), folder, key_text(key), title_string(extension));  // "%s%s%s"
    auto* file = static_cast<uint8_t*>(Mem_AlignedAlloc(0x148, 4, 0));
    std::strcpy(reinterpret_cast<char*>(file + 0x20), folder);
    std::memcpy(file, key, 0x20);
    word<uint32_t>(file, 0x120) = 1;
    file[0x130] = 0;
    if (!nglReadFile(path, &word<uint32_t>(file, 0x124), 1, 0x10000)) {
        Mem_AlignedFree(file);
        return nullptr;
    }
    if (!nglParseMesh(key, file, title_string(extension))) {
        nglFreeFileBuffer(&word<uint32_t>(file, 0x124));
        Mem_AlignedFree(file);
        return nullptr;
    }
    manager_add(manager, file);
    return file;
}

// The DDS-like texture files: the texture keeps the file buffer (+0x40).
bool load_dds(NglTexture* texture, uint32_t* buffer) {
    bool loaded = false;
    if (nglLoadDDS(texture, pointer<uint8_t>(buffer[0]))) {
        nglTextureGetSurfaces(texture);
        texture->last_used_frame = -1;
        loaded = true;
    }
    std::memcpy(reinterpret_cast<uint8_t*>(texture) + 0x40, buffer, 12);
    return loaded;
}

}  // namespace

NglTexture* XB_THISCALL nglTextureManager_Load(void* manager, const uint32_t* key) {
    auto* texture = static_cast<NglTexture*>(Mem_AlignedAlloc(0x74, 4, 0));
    std::memset(texture, 0, 0x74);
    word<uint32_t>(texture, 0) = 1;
    std::memcpy(texture->name, key, 0x20);
    char path[0x100];
    std::strcpy(path, title_string(0x009025f8));
    std::strcat(path, key_text(key));
    char* extension = path + std::strlen(path);
    uint32_t buffer[3];
    bool buffered = false;  // the buffer belongs to the texture
    bool loaded;
    std::strcpy(extension, ".ifl");
    if (!title_global<uint8_t>(0x00902704) && nglReadFile(path, buffer, 4, 0)) {
        loaded = nglLoadAnimatedTextureList(texture, pointer<const char>(buffer[0]), static_cast<int32_t>(buffer[1]));
        nglFreeFileBuffer(buffer);
    } else {
        std::strcpy(extension, ".dds");
        if (nglReadFile(path, buffer, 0x80, 0x30000)) {
            loaded = load_dds(texture, buffer);
            buffered = true;
        } else {
            std::strcpy(extension, ".ddsmp");
            if (nglReadFile(path, buffer, 0x80, 0x30000)) {
                loaded = load_dds(texture, buffer);
                buffered = true;
            } else {
                std::strcpy(extension, ".tga");
                loaded = nglReadFile(path, buffer, 4, 0);
                if (loaded) {
                    title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*, uint32_t, uint8_t**)>(0x003acbbc)(
                        title_global<uint32_t>(0x00902874), pointer<uint8_t>(buffer[0]), buffer[1],
                        &texture->d3d_texture);  // D3DXCreateTextureFromFileInMemory
                    nglFreeFileBuffer(buffer);
                }
            }
        }
    }
    if (!loaded) {
        Mem_AlignedFree(texture);
        if (buffered) nglFreeFileBuffer(buffer);
        return title_global<NglTexture*>(0x009026f8);  // ngl_defaultTexture
    }
    manager_add(manager, texture);
    return texture;
}

uint8_t* XB_THISCALL nglMeshFileManager_Load(void* manager, const uint32_t* key) {
    return load_mesh_file(manager, key, 0x008386b8);  // ".xbmesh"
}

uint8_t* XB_THISCALL nglMeshFileCache_Load(void* manager, const uint32_t* key) {
    return load_mesh_file(manager, key, 0x00838668);  // ".xbmorph"
}

uint32_t XB_THISCALL nglMeshFileCache_Release(void* manager, uint8_t* file, uint32_t force, uint32_t) {
    if (!file) return 0;
    if (!force) {
        const uint32_t count = --word<uint32_t>(file, 0x120);
        if (count) return count;
    }
    if (static_cast<int32_t>(word<uint32_t>(file, 0x144)) + 1 >= title_global<int32_t>(0x009028e0)) {  // ngl_frameCount
        title_global<uint32_t>(0x00903c88) = 0;
        title_global<uint32_t>(0x00903c8c) = 0;
        title_global<uint32_t>(0x00903c90) = 0;
        xb::set_vertex_input(0, 0, 0);  // SetVertexShaderInputDirect
        nglUnbindTextures();
        title_function<void XB_CDECL()>(0x003e6200)();  // D3DDevice_BlockUntilIdle
    }
    if (file[4]) manager_remove(manager, file);
    for (uint8_t* mesh = pointer<uint8_t>(word<uint32_t>(file, 0x138)); mesh;
         mesh = pointer<uint8_t>(word<uint32_t>(mesh, 0x38))) {
        for (uint32_t i = 0; i < word<uint32_t>(mesh, 8); ++i) {
            uint8_t* section = pointer<uint8_t>(word<uint32_t>(pointer<uint8_t>(word<uint32_t>(mesh, 0xc)), i * 8 + 4));
            nglVertexBuffer_Free(&word<uint32_t>(section, 0x44), 4);
            if (word<uint32_t>(section, 0x38)) {
                title_function<void XB_STDCALL(uint32_t)>(0x003e8000)(word<uint32_t>(section, 0x34));  // D3DResource_BlockUntilNotBusy
                Mem_AlignedFree(pointer<void>(word<uint32_t>(section, 0x34)));
                Mem_AlignedFree(pointer<void>(word<uint32_t>(section, 0x38)));
                word<uint32_t>(section, 0x34) = 0;
                word<uint32_t>(section, 0x38) = 0;
                word<uint32_t>(section, 0x3c) = 0;
            }
        }
        manager_remove(title_global<void*>(0x00902a80), mesh);  // ngl_meshManager
    }
    for (uint8_t* record = pointer<uint8_t>(word<uint32_t>(file, 0x13c)); record;
         record = pointer<uint8_t>(word<uint32_t>(record, 0xc)))
        if (void* const owner = pointer<void>(word<uint32_t>(record, 4))) manager_remove(owner, record);
    for (uint8_t* record = pointer<uint8_t>(word<uint32_t>(file, 0x140)); record;
         record = pointer<uint8_t>(word<uint32_t>(record, 0x10)))
        manager_remove(title_global<void*>(0x00902a88), record);
    if (!file[0x130]) nglFreeFileBuffer(&word<uint32_t>(file, 0x124));
    Mem_AlignedFree(file);
    return 0;
}

uint8_t* XB_THISCALL nglPaletteManager_Load(void* manager, const uint32_t* key) {
    const char* folder = title_string(0x009025f8);
    char path[0x100];
    title_snprintf(path, sizeof path, title_string(0x007c04c8), folder, key_text(key));  // "%s%s.ddsp"
    auto* palette = static_cast<uint8_t*>(Mem_AlignedAlloc(0x140, 4, 0));
    word<uint32_t>(palette, 0x124) = 1;
    palette[0x120] = 0;
    std::strcpy(reinterpret_cast<char*>(palette + 0x20), folder);
    std::memcpy(palette, key, 0x20);
    if (!nglReadFile(path, &word<uint32_t>(palette, 0x128), 0x40, 0x30000)) {
        Mem_AlignedFree(palette);
        return nullptr;
    }
    if (!nglParseDDSPalette(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key)), palette)) {
        nglFreeFileBuffer(&word<uint32_t>(palette, 0x128));
        Mem_AlignedFree(palette);
        return nullptr;
    }
    manager_add(manager, palette);
    return palette;
}

int32_t XB_THISCALL nglPaletteManager_Release(void* manager, uint8_t* palette, int32_t mode, uint32_t) {
    if (!palette) return 0;
    if (mode == 0) {
        int32_t& references = word<int32_t>(palette, 0x124);
        if (--references > 0) return references;
    }
    if (palette[4]) manager_remove(manager, palette);
    const uint8_t* header = pointer<uint8_t>(word<uint32_t>(palette, 0x134));
    for (int32_t i = static_cast<int32_t>(word<uint16_t>(const_cast<uint8_t*>(header), 8)) - 1; i >= 0; --i)
        nglFreePalette(pointer<uint32_t>(pointer<uint32_t>(word<uint32_t>(palette, 0x13c))[i]));
    Mem_AlignedFree(pointer<void>(word<uint32_t>(palette, 0x13c)));
    word<uint32_t>(palette, 0x13c) = 0;
    if (!palette[0x120]) nglFreeFileBuffer(&word<uint32_t>(palette, 0x128));
    Mem_AlignedFree(palette);
    return 0;
}

NglFont* XB_THISCALL nglFontManager_Load(void* manager, const uint32_t* key) {
    char path[0x100];
    title_snprintf(path, sizeof path, title_string(0x007bd0c0), title_string(0x009025f8), key_text(key),
                   title_string(0x008386d4));  // "%s%s%s", ".fdf"
    uint32_t buffer[3];
    if (!nglReadFile(path, buffer, 4, 0)) return nullptr;
    NglFont* font = nglLoadFontFile(reinterpret_cast<const NglHashedName*>(key), pointer<char>(buffer[0]));
    nglFreeFileBuffer(buffer);
    manager_add(manager, font);
    return font;
}

}  // namespace game
