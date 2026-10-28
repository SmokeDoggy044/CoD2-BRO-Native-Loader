#include "stream.h"

#include <cstring>

#include "actor.h"
#include "cg_ents.h"
#include "cm.h"
#include "fx.h"
#include "fx_load.h"
#include "nal_anim.h"
#include "lanes.h"
#include "ngl.h"
#include "qcommon.h"
#include "r_init.h"
#include "shaders.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t address_of(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

uint32_t vtable_slot(const uint32_t* object, int slot) {
    return reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(object[0]))[slot];
}

void read(uint32_t* stream, void* out, int32_t count) {
    title_function<void XB_THISCALL(uint32_t*, void*, int32_t)>(vtable_slot(stream, 1))(stream, out, count);
}

// Four bytes from the stream, swapped for big-endian data.
uint32_t read_word(uint32_t* stream) {
    uint8_t b[4];
    const uint32_t unset = address_of(stream);
    std::memcpy(b, &unset, 4);
    read(stream, b, 4);
    if (title_global<uint8_t>(0x00902269)) {
        const uint8_t b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3];
        b[3] = b0;
        b[0] = b3;
        b[1] = b2;
        b[2] = b1;
    }
    uint32_t value;
    std::memcpy(&value, b, 4);
    return value;
}

}  // namespace

MemStream* XB_THISCALL MemStream_Init(MemStream* stream, const uint8_t* data, int32_t size) {
    stream->data = data;
    stream->vtable = 0x007bad98;
    stream->size = size;
    stream->position = 0;
    stream->flag = 0;
    return stream;
}

void XB_THISCALL MemStream_Read(MemStream* stream, uint8_t* out, int32_t count) {
    const uint32_t* self = reinterpret_cast<const uint32_t*>(stream);
    if (title_function<uint8_t XB_THISCALL(MemStream*)>(vtable_slot(self, 2))(stream)) return;
    if (stream->position + count > stream->size) return;
    std::memcpy(out, stream->data + stream->position, static_cast<uint32_t>(count));
    stream->position += count;
}

uint32_t XB_THISCALL Stream_ReadInt(uint32_t* stream) { return read_word(stream); }

uint32_t XB_THISCALL Stream_ReadUInt(uint32_t* stream) { return read_word(stream); }

float XB_THISCALL Stream_ReadFloat(uint32_t* stream) {
    const uint32_t bits = read_word(stream);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void XB_THISCALL Stream_ReadString(uint32_t* stream, char* out, int32_t* length, int32_t capacity) {
    *length = static_cast<int32_t>(read_word(stream));
    // The byte goes into the original's `length` argument slot: a read that writes
    // nothing leaves the pointer's low byte, or the previous byte.
    char c = static_cast<char>(address_of(length));
    int32_t i = 0;
    for (; i < *length; ++i) {
        read(stream, &c, 1);
        if (i < capacity) out[i] = c;
    }
    if (i < capacity) out[i] = 0;
}

void XB_THISCALL MemStream_Destroy(MemStream* stream) {
    stream->vtable = 0x007bad98;
    if (stream->flag && stream->data)
        title_function<void XB_CDECL(const uint8_t*)>(0x000428d0)(stream->data);  // Z_Free
    stream->vtable = 0x007bad80;
}

MemStream* XB_THISCALL MemStream_DeletingDestroy(MemStream* stream, uint32_t flags) {
    MemStream_Destroy(stream);
    if (flags & 1) title_function<void XB_CDECL(MemStream*)>(0x000428d0)(stream);  // Z_Free
    return stream;
}

void XB_CDECL Pack_DecodeRelocWord(uint32_t word, uint32_t bits, uint32_t* delta, uint32_t* next) {
    const uint32_t shift = 31 - bits;
    if (word & 0x80000000) {
        if (!(word & 0x40000000)) word &= 0x7fffffff;
        *delta = word;
        *next = 0;
        return;
    }
    // SHL/SHR use the low five bits of the count.
    const uint32_t sign = ~((1u << ((shift - 1) & 31)) - 1);
    uint32_t low = ((1u << (shift & 31)) - 1) & word;
    if (low & sign) low |= sign;
    *delta = low;
    *next = (word & 0x7fffffff) >> (shift & 31);
}

PLATFORM_ASM_ENTRY(Pack_RelocateChain_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Pack_RelocateChain) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Pack_RelocateChain(uint32_t* word, uint32_t bits) {
    uint32_t next;
    do {
        uint32_t delta = 0;
        next = 0;
        Pack_DecodeRelocWord(*word, bits, &delta, &next);
        *word = delta + address_of(word);
        word = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(word) + next);
    } while (next != 0);
}

uint32_t XB_THISCALL Pack_RelocateTable(PackRelocTable* self, uint32_t base) {
    self->entries += base;
    const uint32_t bits = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(self->entries));
    for (uint32_t i = 1; i < self->count; ++i)
        Pack_RelocateChain(reinterpret_cast<uint32_t*>(
                               static_cast<uintptr_t>(reinterpret_cast<const uint32_t*>(self->entries)[i] + base)),
                           bits);
    return self->count;
}

namespace {

// The zone's name maps: a UintMap at +offset (hash -> index), the count at +8 and the
// entries (8 bytes each, the value at +4) at +0xc after it.
uint32_t zone_map_lookup(uint32_t offset, uint32_t hash, const char* name, const uint8_t* zone) {
    if (!hash && name) hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x0001d830)(name);  // HashStr_Hash
    const uint32_t* index =
        UintMap_Find(reinterpret_cast<const uint32_t*>(zone + offset), &hash);
    if (!index || *index == 0xffffffff) return 0;
    uint32_t i = *index;
    if (i >= *reinterpret_cast<const uint32_t*>(zone + offset + 8)) i = 0;
    return *reinterpret_cast<const uint32_t*>(zone + offset + 0xc) + i * 8 + 4;
}

}  // namespace

#define ZONE_MAP_LOOKUP(name, offset)                                                                         \
    PLATFORM_ASM_ENTRY(name##_entry, "pushl %edi\n"                                                          \
                                     "pushl %eax\n"                                                          \
                                     "pushl %ecx\n" PLATFORM_ASM_CALL(name) "addl $12, %esp\n"               \
                                     "ret")                                                                  \
    uint32_t XB_CDECL name(uint32_t hash, const char* key, const uint8_t* zone) {                           \
        return zone_map_lookup(offset, hash, key, zone);                                                    \
    }

ZONE_MAP_LOOKUP(ZoneTextureMap_Lookup, 0x08)
ZONE_MAP_LOOKUP(ZoneFontMap_Lookup, 0x18)
ZONE_MAP_LOOKUP(ZoneMap28_Lookup, 0x28)
ZONE_MAP_LOOKUP(ZoneMeshFileMap_Lookup, 0x38)
ZONE_MAP_LOOKUP(ZoneAnimMap_Lookup, 0x88)
ZONE_MAP_LOOKUP(ZoneMap98_Lookup, 0x98)
ZONE_MAP_LOOKUP(ZoneMapA8_Lookup, 0xa8)
ZONE_MAP_LOOKUP(ZoneSkelMap_Lookup, 0xb8)
ZONE_MAP_LOOKUP(ZoneEffectMap_Lookup, 0xc8)

// --- Memory-context id stack (+0x374 count, +0x378 ids, at most 0x80; -1 = a frame mark) ---

namespace {

uint32_t& context_word(uint8_t* context, uint32_t offset) { return *reinterpret_cast<uint32_t*>(context + offset); }

uint8_t* global_context() { return title_global<uint8_t*>(0x0095f638); }

}  // namespace

PLATFORM_ASM_ENTRY(MemContext_Push_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(MemContext_Push) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL MemContext_Push(uint8_t* context, uint32_t id) {
    uint32_t& count = context_word(context, 0x374);
    auto* ids = reinterpret_cast<uint32_t*>(context + 0x378);
    if (static_cast<int32_t>(count) < 0x80) ids[count++] = 0xffffffff;
    bool listed = false;
    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
        if (ids[i] == id) {
            listed = true;
            break;
        }
    }
    if (!listed && context_word(context, 0x18)) {
        const uint32_t found =
            title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(0x0028e550)(context, id);  // MemContext_FindById
        if (!found) return 0;
        return title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(0x0028bc50)(context, found);  // Pak_PushZoneIds_r
    }
    const uint32_t old = count;
    if (static_cast<int32_t>(count) < 0x80) ids[count++] = id;
    return old;
}

PLATFORM_ASM_ENTRY(MemContext_Pop_entry,
                   "pushl %edx\n" PLATFORM_ASM_CALL(MemContext_Pop) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL MemContext_Pop(uint8_t* context) {
    uint32_t& count = context_word(context, 0x374);
    const auto* ids = reinterpret_cast<const uint32_t*>(context + 0x378);
    const auto top = [&count]() {
        const int32_t i = static_cast<int32_t>(count) - 1;
        return i > 0 ? i : 0;
    };
    const uint32_t last = ids[top()];
    do {
        if (count) --count;
    } while (ids[top()] != 0xffffffff);
    if (count) --count;
    return last;
}

PLATFORM_ASM_ENTRY(MemContext_ListActive_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(MemContext_ListActive) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL MemContext_ListActive(uint8_t* context, uint32_t* out) {
    MemContext_Push(global_context(), context_word(context, 0x28));
    MemContext_Push(global_context(), context_word(context, 0x2c));
    uint32_t seen[4] = {};
    const auto* ids = reinterpret_cast<const uint32_t*>(context + 0x378);
    for (int32_t i = static_cast<int32_t>(context_word(context, 0x374)) - 1; i >= 0; --i) {
        const uint32_t id = ids[i];
        if (id == 0xffffffff) continue;
        const uint32_t bit = 1u << (id & 31);
        uint32_t& word = seen[static_cast<int32_t>(id) >> 5];
        if (word & bit) continue;
        word |= bit;
        if (static_cast<int32_t>(out[0]) < 0x20) out[1 + out[0]++] = id;
    }
    MemContext_Pop(global_context());
    return MemContext_Pop(global_context());
}

PLATFORM_ASM_ENTRY(Stream_ListSearchZones_entry,
                   "pushl %esi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Stream_ListSearchZones) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Stream_ListSearchZones(uint32_t zone, uint32_t* out) {
    uint8_t* context = global_context();
    if (!context) {
        const uint32_t old = out[0];
        if (static_cast<int32_t>(old) < 0x20) out[1 + out[0]++] = zone;
        return old;
    }
    uint32_t seen[4] = {};
    title_function<void XB_THISCALL(uint8_t*, uint32_t, uint32_t*, uint32_t*)>(0x0028f850)(  // Stream_CollectSearchZones_r
        context, zone, out, seen);
    context = global_context();
    for (uint32_t offset = 0x28; offset <= 0x2c; offset += 4) {
        const uint32_t id = context_word(context, offset);
        if (id == 0xffffffff || (seen[static_cast<int32_t>(id) >> 5] & (1u << (id & 31)))) continue;
        if (static_cast<int32_t>(out[0]) < 0x20) out[1 + out[0]++] = id;
    }
    return context_word(context, 0x2c);
}

namespace {

// Stream_FindTexture/FindMeshFile: the zones to search (the active contexts' when `active`,
// else the zone's search list), each zone's map looked up with the hashed name.
uint32_t stream_find(uint32_t streams, uint32_t zone, const uint32_t* hashed, uint8_t active,
                     uint32_t (*lookup)(uint32_t, const char*, const uint8_t*)) {
    uint32_t list[0x21];
    list[0] = 0;
    uint32_t count = 0;
    if (active) {
        if (global_context()) {
            MemContext_ListActive(global_context(), list);
            count = list[0];
        }
    } else {
        Stream_ListSearchZones(zone, list);
        count = list[0];
    }
    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
        const uint32_t id = list[1 + i];
        if (id == 0xffffffff) continue;
        const uint32_t zone_data = *reinterpret_cast<const uint32_t*>(uintptr_t{streams + 0x5c + id * 4});
        if (!zone_data) continue;
        const uint32_t entry = lookup(hashed[0], reinterpret_cast<const char*>(hashed + 1),
                                      reinterpret_cast<const uint8_t*>(uintptr_t{zone_data}));
        if (entry) return *reinterpret_cast<const uint32_t*>(uintptr_t{entry});
    }
    return 0;
}

uint32_t texture_lookup(uint32_t hash, const char* key, const uint8_t* zone) {
    return ZoneTextureMap_Lookup(hash, key, zone);
}

uint32_t mesh_file_lookup(uint32_t hash, const char* key, const uint8_t* zone) {
    return ZoneMeshFileMap_Lookup(hash, key, zone);
}

}  // namespace

uint32_t XB_STDCALL Stream_FindTexture(uint32_t streams, uint32_t zone, const char* name, uint32_t active,
                                       uint32_t) {
    if (title_global<uint8_t>(0x0095f634)) return 0;
    uint32_t hashed[8];
    Stream_HashFileName(name + 4, hashed);
    return stream_find(streams, zone, hashed, static_cast<uint8_t>(active), texture_lookup);
}

PLATFORM_ASM_ENTRY(Stream_FindMeshFile_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_FindMeshFile) "addl $20, %esp\n"
                   "ret $16")

uint32_t XB_CDECL Stream_FindMeshFile(const uint32_t* hashed, uint32_t streams, uint32_t zone, uint32_t active,
                                      uint32_t) {
    if (title_global<uint8_t>(0x0095f635)) return 0;
    return stream_find(streams, zone, hashed, static_cast<uint8_t>(active), mesh_file_lookup);
}

namespace {

uint32_t anim_lookup(uint32_t hash, const char* key, const uint8_t* zone) { return ZoneAnimMap_Lookup(hash, key, zone); }
uint32_t a8_lookup(uint32_t hash, const char* key, const uint8_t* zone) { return ZoneMapA8_Lookup(hash, key, zone); }

// Pak_CollectLoadedZoneIds (0x0028ccf0): appends the loaded zones' ids to the list in ESI.
extern "C" PLATFORM_HIDDEN void XB_CDECL Pak_CollectLoadedZoneIds_call(uint32_t* list);
PLATFORM_ASM_ENTRY(Pak_CollectLoadedZoneIds_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "movl $0x0028ccf0, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

uint32_t search_list(uint32_t streams, const uint32_t* list, uint32_t count, const uint32_t* hashed,
                     uint32_t (*lookup)(uint32_t, const char*, const uint8_t*)) {
    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
        const uint32_t id = list[1 + i];
        if (id == 0xffffffff) continue;
        const uint32_t zone_data = *reinterpret_cast<const uint32_t*>(uintptr_t{streams + 0x5c + id * 4});
        if (!zone_data) continue;
        const uint32_t entry = lookup(hashed[0], reinterpret_cast<const char*>(hashed + 1),
                                      reinterpret_cast<const uint8_t*>(uintptr_t{zone_data}));
        if (entry) return *reinterpret_cast<const uint32_t*>(uintptr_t{entry});
    }
    return 0;
}

}  // namespace

#define STREAM_FIND_EBX_ENTRY(name)                                                                  \
    PLATFORM_ASM_ENTRY(name##_entry, "pushl 16(%esp)\n"                                             \
                                     "pushl 16(%esp)\n"                                             \
                                     "pushl 16(%esp)\n"                                             \
                                     "pushl 16(%esp)\n"                                             \
                                     "pushl %ebx\n" PLATFORM_ASM_CALL(name) "addl $20, %esp\n"      \
                                     "ret $16")

STREAM_FIND_EBX_ENTRY(Stream_FindAnim)
uint32_t XB_CDECL Stream_FindAnim(const uint32_t* hashed, uint32_t streams, uint32_t zone, uint32_t active, uint32_t) {
    if (title_global<uint8_t>(0x0095f636)) return 0;
    return stream_find(streams, zone, hashed, static_cast<uint8_t>(active), anim_lookup);
}

STREAM_FIND_EBX_ENTRY(Stream_FindMapA8)
uint32_t XB_CDECL Stream_FindMapA8(const uint32_t* hashed, uint32_t streams, uint32_t zone, uint32_t active,
                                   uint32_t) {
    if (title_global<uint8_t>(0x0095f636)) return 0;
    return stream_find(streams, zone, hashed, static_cast<uint8_t>(active), a8_lookup);
}

STREAM_FIND_EBX_ENTRY(Stream_FindEffect)
uint32_t XB_CDECL Stream_FindEffect(const uint32_t* hashed, uint32_t streams, uint32_t zone, uint32_t active,
                                    uint32_t) {
    uint32_t list[0x21];
    list[0] = 0;
    Pak_CollectLoadedZoneIds_call(list);
    if (static_cast<uint8_t>(active)) {
        if (global_context()) MemContext_ListActive(global_context(), list);
    } else {
        Stream_ListSearchZones(zone, list);
    }
    return search_list(streams, list, list[0], hashed,
                       [](uint32_t hash, const char* key, const uint8_t* z) { return ZoneEffectMap_Lookup(hash, key, z); });
}

PLATFORM_ASM_ENTRY(Stream_FindSkel_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_FindSkel) "addl $20, %esp\n"
                   "ret $16")

uint32_t XB_CDECL Stream_FindSkel(uint32_t active, uint32_t streams, uint32_t zone, const uint32_t* hashed, uint32_t) {
    uint32_t list[0x21];
    list[0] = 0;
    bool collect = false;
    if (static_cast<uint8_t>(active)) {
        if (global_context()) {
            MemContext_ListActive(global_context(), list);
            collect = static_cast<int32_t>(list[0]) <= 2;
        } else {
            collect = true;
        }
    } else {
        Stream_ListSearchZones(zone, list);
        collect = static_cast<int32_t>(list[0]) <= 2;
    }
    if (collect) {
        list[0] = 0;
        Pak_CollectLoadedZoneIds_call(list);
    }
    return search_list(streams, list, list[0], hashed,
                       [](uint32_t hash, const char* key, const uint8_t* z) { return ZoneSkelMap_Lookup(hash, key, z); });
}

uint32_t XB_CDECL Stream_LoadMeshFile(const char* name, uint8_t* data, uint32_t size, uint32_t context_id) {
    uint8_t& loading = title_global<uint8_t>(0x0095f5d0);
    uint8_t saved = 0;
    if (context_id != 0xffffffff) {
        saved = loading;
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = 1;
        MemContext_Push(global_context(), context_id);
    }
    uint32_t hashed[8];
    Stream_HashFileName(name, hashed);
    title_function<void XB_STDCALL(uint8_t*, uint32_t, uint32_t)>(0x001e76aa)(data, size, 0x400);  // XPhysicalProtect
    const uint8_t* file = nglLoadMeshFile(hashed, address_of(data));
    uint32_t result = 0;
    for (uint32_t mesh = *reinterpret_cast<const uint32_t*>(file + 0x138); mesh;
         mesh = *reinterpret_cast<const uint32_t*>(uintptr_t{mesh + 0x38}))
        *reinterpret_cast<uint32_t*>(uintptr_t{mesh + 4}) |= 0x1000000;
    if (context_id != 0xffffffff) {
        loading = saved;
        title_global<uint8_t>(0x0095f5d1) = 0;
        result = MemContext_Pop(global_context());
    }
    return result;
}

PLATFORM_ASM_ENTRY(Stream_RegisterFont_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_RegisterFont) "addl $16, %esp\n"
                   "ret $12")

uint32_t XB_CDECL Stream_RegisterFont(const char* name, uint32_t streams, char* text, uint32_t zone_id) {
    uint32_t hashed[8];
    Stream_HashFileName(name, hashed);
    NglFont* font = nglLoadFont(reinterpret_cast<const NglHashedName*>(hashed), text);
    if (!font) return 0;
    const auto* zone = reinterpret_cast<const uint8_t*>(uintptr_t{
        *reinterpret_cast<const uint32_t*>(uintptr_t{streams + 0x5c + zone_id * 4})});
    *reinterpret_cast<uint32_t*>(uintptr_t{ZoneFontMap_Lookup(hashed[0], reinterpret_cast<const char*>(hashed + 1),
                                                                zone)}) = address_of(font);
    char short_name[0x20];
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(short_name, name, 0x1f);  // _strncpy
    short_name[0x1f] = 0;
    using FindFont = int32_t XB_REGS1 XB_STDCALL(const char*, uint32_t);
    const int32_t index = title_function<FindFont>(0x0030f2b0)(short_name, 1);  // R_FindFont
    if (index != 8) {
        title_global<uint32_t>(0x0095f8d4 + index * 4) = address_of(font);
        title_global<uint8_t>(0x0095f90c + index) = 1;
    }
    return static_cast<uint32_t>(index);
}

// --- File-type load handlers (name, data, size, zone or context id) ---

uint32_t XB_CDECL Stream_LoadIfl(char* name, uint8_t* data, uint32_t size, uint32_t zone) {
    return Stream_LoadTexture(name, data, size, zone, 3);
}

uint32_t XB_CDECL Stream_LoadDdsmp(char* name, uint8_t* data, uint32_t size, uint32_t zone) {
    return Stream_LoadTexture(name, data, size, zone, 1);
}

uint32_t XB_CDECL Stream_LoadDds(char* name, uint8_t* data, uint32_t size, uint32_t zone) {
    return Stream_LoadTexture(name, data, size, zone, 0);
}

uint32_t XB_CDECL Stream_LoadFdf(char* name, uint8_t* data, uint32_t, uint32_t zone) {
    return Stream_RegisterFont(name, title_global<uint32_t>(0x0095f630), reinterpret_cast<char*>(data), zone);
}

uint32_t XB_CDECL Stream_LoadStb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    return title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*, uint32_t)>(0x002963f0)(  // StringTable_Load
        title_global<uint32_t>(0x0095f5d8), data, zone);
}

namespace {

// The pack's relocation table at +table_offset (stored relative to the data) made absolute
// and applied.
void relocate_pack(uint8_t* data, uint32_t table_offset) {
    uint32_t& table = *reinterpret_cast<uint32_t*>(data + table_offset);
    table += address_of(data);
    Pack_RelocateTable(reinterpret_cast<PackRelocTable*>(uintptr_t{table}), address_of(data));
}

}  // namespace

uint32_t XB_CDECL Stream_LoadDb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x18);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f5a8) + 4 + zone * 4) = address_of(data);
    return zone;
}

uint32_t XB_CDECL Stream_LoadIb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0xe8);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f630) + 0x5c + zone * 4) = address_of(data);
    return zone;
}

namespace {

// Stream_Add*ToZone: the object (its hashed name at +name_offset) stored in the zone map
// at +map_offset of the zone on top of the global context's stack (-1 when empty); returns
// the entry's previous value.
uint32_t add_to_zone(uint32_t streams, uint32_t object, uint32_t name, uint32_t map_offset, bool checked) {
    uint8_t* context = global_context();
    const uint32_t count = context_word(context, 0x374);
    uint32_t id = 0xffffffff;
    if (count) {
        const int32_t top = static_cast<int32_t>(count) - 1;
        id = context_word(context, 0x378 + (top > 0 ? top : 0) * 4);
    }
    const uint32_t zone = *reinterpret_cast<const uint32_t*>(uintptr_t{streams + 0x5c + id * 4});
    uint32_t hash = *reinterpret_cast<const uint32_t*>(uintptr_t{name});
    if (!hash)
        hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x0001d830)(  // HashStr_Hash
            reinterpret_cast<const char*>(uintptr_t{name + 4}));
    const uint32_t* index = UintMap_Find(reinterpret_cast<const uint32_t*>(uintptr_t{zone + map_offset}), &hash);
    volatile uint32_t* slot = nullptr;
    if (index && *index != 0xffffffff) {
        uint32_t i = *index;
        if (i >= *reinterpret_cast<const uint32_t*>(uintptr_t{zone + map_offset + 8})) i = 0;
        slot = reinterpret_cast<uint32_t*>(
            uintptr_t{*reinterpret_cast<const uint32_t*>(uintptr_t{zone + map_offset + 0xc}) + i * 8 + 4});
    }
    if (checked && !slot) return 0;
    const uint32_t old = *slot;  // unchecked (mesh files): the original reads address 0 as well
    *slot = object;
    return old;
}

}  // namespace

uint32_t XB_STDCALL Stream_AddTextureToZone(uint32_t streams, uint32_t texture) {
    return add_to_zone(streams, texture, texture + 0x54, 0x08, true);
}

uint32_t XB_STDCALL Stream_AddMeshToZone(uint32_t streams, uint32_t mesh) {
    return add_to_zone(streams, mesh, *reinterpret_cast<const uint32_t*>(uintptr_t{mesh}), 0x28, false);
}

PLATFORM_ASM_ENTRY(Stream_AddAnimToZone_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_AddAnimToZone) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Stream_AddAnimToZone(uint32_t anim, uint32_t streams) {
    return add_to_zone(streams, anim, anim + 0x10, 0x88, false);
}

PLATFORM_ASM_ENTRY(Stream_AddSkelToZone_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_AddSkelToZone) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Stream_AddSkelToZone(uint32_t skel, uint32_t streams) {
    return add_to_zone(streams, skel, skel + 8, 0xb8, false);
}

namespace {

// Zone*Manager_Add (vtable slot, thiscall): disabled (+9 clear) -> the object back; no
// active zone on the global context -> the wrapped manager's Add (+4, slot 3); else the
// zone's own map.
uint32_t zone_manager_add(const uint8_t* self, uint32_t object, uint32_t (*add)(uint32_t)) {
    if (!self[9]) return object;
    uint8_t* context = global_context();
    const uint32_t count = context_word(context, 0x374);
    if (count) {
        const int32_t top = static_cast<int32_t>(count) - 1;
        if (context_word(context, 0x378 + (top > 0 ? top : 0) * 4) != 0xffffffff) return add(object);
    }
    const uint32_t inner = *reinterpret_cast<const uint32_t*>(self + 4);
    return title_function<uint32_t XB_THISCALL(uint32_t, uint32_t)>(
        *reinterpret_cast<const uint32_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(uintptr_t{inner}) + 0xc}))(
        inner, object);
}

uint32_t streams() { return title_global<uint32_t>(0x0095f630); }

}  // namespace

uint32_t XB_THISCALL ZoneTextureManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddTextureToZone(streams(), o); });
}

uint32_t XB_THISCALL ZoneMeshFileManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddMeshFileToZone(streams(), o); });
}

uint32_t XB_THISCALL ZoneMeshManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddMeshToZone(streams(), o); });
}

uint32_t XB_THISCALL ZoneAnimManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddAnimToZone(o, streams()); });
}

uint32_t XB_THISCALL ZoneSkelManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddSkelToZone(o, streams()); });
}

uint32_t XB_STDCALL Stream_AddMeshFileToZone(uint32_t streams, uint32_t file) {
    return add_to_zone(streams, file, file, 0x38, false);
}

uint32_t XB_CDECL Stream_LoadAitb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x18);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f718) + 4 + zone * 4) = address_of(data);
    return zone;
}

uint8_t* XB_CDECL Stream_LoadLgrid(char*, uint8_t* data, uint32_t, uint32_t zone) {
    auto* words = reinterpret_cast<uint32_t*>(data);
    for (int i = 0; i <= 6; i += 2) words[i] += address_of(data);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f6a0) + 4 + zone * 4) = address_of(data);
    return data;
}

uint32_t XB_CDECL Stream_LoadBmips(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x14);
    *reinterpret_cast<uint32_t*>(data + 8) = zone;
    title_global<uint32_t>(title_global<uint32_t>(0x0095f630) + 0x58) = address_of(data);
    return zone;
}

namespace {

// 0x0023cfb0: data in EBX, the zone on the stack (stdcall).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Xmb_Register_call(uint8_t* data, uint32_t zone);
PLATFORM_ASM_ENTRY(Xmb_Register_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "pushl 12(%esp)\n"
                   "movl $0x0023cfb0, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

// Path_InitAfterLoad (0x002c0b80): the path node blocks in ESI.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Path_InitAfterLoad_call(uint32_t blocks);
PLATFORM_ASM_ENTRY(Path_InitAfterLoad_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "movl $0x002c0b80, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

}  // namespace

uint32_t XB_CDECL Stream_LoadXmb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x18);
    const uint32_t models = title_global<uint32_t>(0x0095f6ac);
    const uint32_t result = Xmb_Register_call(data, zone);
    title_global<uint32_t>(models + 4 + zone * 4) = address_of(data);
    return result;
}

uint32_t XB_CDECL Stream_LoadLpth(char*, uint8_t* data, uint32_t, uint32_t) {
    auto* words = reinterpret_cast<uint32_t*>(data);
    for (int i = 4; i < 8; ++i) words[i] += address_of(data);
    if (words[0] != 9) return address_of(data);
    const uint32_t blocks = title_global<uint32_t>(0x0095f4f4);  // g_pathNodeBlocks
    title_global<uint32_t>(blocks + 0x194) = address_of(data);
    return Path_InitAfterLoad_call(blocks);
}

uint32_t XB_CDECL Stream_LoadXbskel(char*, uint8_t* data, uint32_t, uint32_t context_id) {
    uint8_t& loading = title_global<uint8_t>(0x0095f5d0);
    uint8_t saved = 0;
    if (context_id != 0xffffffff) {
        saved = loading;
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = 1;
        MemContext_Push(global_context(), context_id);
    }
    const uint32_t result = title_function<uint32_t XB_CDECL(uint8_t*)>(0x0008e130)(data);
    if (context_id == 0xffffffff) return result;
    loading = saved;
    title_global<uint8_t>(0x0095f5d1) = 0;
    return MemContext_Pop(global_context());
}

uint32_t XB_CDECL Stream_LoadXbanim(char* name, uint8_t* data, uint32_t, uint32_t context_id) {
    uint8_t& loading = title_global<uint8_t>(0x0095f5d0);
    uint8_t saved = 0;
    if (context_id != 0xffffffff) {
        saved = loading;
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = 1;
        MemContext_Push(global_context(), context_id);
    }
    ++title_global<uint32_t>(0x008d508c);
    uint32_t hashed[8];
    Stream_HashFileName(name, hashed);
    uint32_t result = title_function<uint32_t XB_CDECL(const uint32_t*, uint8_t*)>(0x0008dbb0)(hashed, data);
    if (context_id != 0xffffffff) {
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = saved;
        result = MemContext_Pop(global_context());
    }
    return result;
}

uint32_t XB_CDECL Stream_LoadZbb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    return title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*, uint32_t)>(0x00291610)(  // Stream_ParseZbb
        title_global<uint32_t>(0x0095f640), data, zone);
}

// The effect loader copies stack locals it never sets (stale stack): the C frame runs
// 128 KB below the entry and the calls run where the original makes them (entry - 0x14
// for MemContext_Push after its push esi, entry - 0x10 for FX_LoadEffectFromStream).
PLATFORM_ASM_ENTRY(Stream_LoadBpe_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_LoadBpe) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL Stream_LoadBpe(uint32_t entry, char* name, uint8_t* data, uint32_t size, uint32_t context_id) {
    uint8_t& loading = title_global<uint8_t>(0x0095f5d0);
    uint8_t saved = 0;
    if (context_id != 0xffffffff) {
        saved = loading;
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = 1;
        const TitleRegs push{0, 0, 0, 0, address_of(global_context()), context_id};
        title_call_at_regs(entry - 0x14, 0x0028f6c0, &push, 0, nullptr);  // MemContext_Push
    }
    const TitleRegs load{size, context_id, address_of(data), 0, 0, context_id};
    const uint32_t args[2] = {title_global<uint32_t>(0x0095f630), address_of(name)};
    const uint32_t result = title_call_at_regs(entry - 0x10, 0x00291140, &load, 2, args);  // FX_LoadEffectFromStream
    if (context_id == 0xffffffff) return result;
    loading = saved;
    title_global<uint8_t>(0x0095f5d1) = 0;
    return MemContext_Pop(global_context());
}

PLATFORM_ASM_ENTRY(Stream_LoadTexture_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_LoadTexture) "addl $20, %esp\n"
                   "ret")

uint32_t XB_CDECL Stream_LoadTexture(char* name, uint8_t* data, uint32_t size, uint32_t context_id, uint32_t kind) {
    uint8_t& loading = title_global<uint8_t>(0x0095f5d0);
    uint8_t saved = 0;
    if (context_id != 0xffffffff) {
        saved = loading;
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = 1;
        MemContext_Push(global_context(), context_id);
    }
    if (!title_function<int XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0)(name, "no_", 3))  // _strncmp
        name[0] = 'p';
    uint32_t hashed[8];
    Stream_HashFileName(name, hashed);
    uint32_t& once = title_global<uint32_t>(0x0098bcb8);
    if (!(once & 1)) {
        once |= 1;
        title_function<void XB_THISCALL(uint32_t, const char*)>(0x00011060)(0x0098bc98, "fogramp");  // HashStr_Init
    }
    uint32_t result = address_of(nglLoadTexture(reinterpret_cast<const uint8_t*>(hashed), kind, data, size));
    if (context_id != 0xffffffff) {
        title_global<uint8_t>(0x0095f5d1) = 0;
        loading = saved;
        result = MemContext_Pop(global_context());
    }
    return result;
}

PLATFORM_ASM_ENTRY(FixedStr512_BaseName_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FixedStr512_BaseName) "addl $12, %esp\n"
                   "ret $4")

char* XB_CDECL FixedStr512_BaseName(const char* src, char* dst, uint32_t flag) {
    char stem[0x200];
    int32_t stem_length;
    *reinterpret_cast<uint16_t*>(stem + 0x1fe) = 0;
    stem[0] = 0;
    title_function<void XB_CDECL(char*, int32_t*, const char*, uint32_t, uint32_t)>(0x00044500)(  // Path_BaseName
        stem, &stem_length, src, *reinterpret_cast<const uint16_t*>(src + 0x1fe), flag);
    const uint16_t length = static_cast<uint16_t>(stem_length);
    *reinterpret_cast<uint16_t*>(stem + 0x1fe) = length;
    int32_t copied;
    title_function<void XB_CDECL(char*, int32_t*, int32_t, const char*, uint32_t)>(0x00044370)(  // FixedStr_Copy
        dst, &copied, 0x1fe, stem, length);
    *reinterpret_cast<uint16_t*>(dst + 0x1fe) = static_cast<uint16_t>(copied);
    return dst;
}

PLATFORM_ASM_ENTRY(Stream_HashFileName_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_HashFileName) "addl $8, %esp\n"
                   "ret")

uint32_t* XB_CDECL Stream_HashFileName(const char* name, uint32_t* out) {
    char text[0x200];
    int32_t length = 0;
    title_function<void XB_CDECL(char*, int32_t*, int32_t, const char*)>(0x000442e0)(text, &length, 0x1fe,
                                                                                     name);  // Q_strncpyLen
    *reinterpret_cast<uint16_t*>(text + 0x1fe) = static_cast<uint16_t>(length);
    char base[0x200];
    FixedStr512_BaseName(text, base, 1);
    uint32_t hashed[8];
    title_function<void XB_THISCALL(uint32_t*, const char*)>(0x00011060)(hashed, base);  // HashStr_Init
    for (int i = 0; i < 8; ++i) out[i] = hashed[i];
    return out;
}

namespace {

template <typename T>
T& word_at(const uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(const_cast<uint8_t*>(base) + offset);
}

uint32_t wrap_index(uint32_t index, uint32_t count) { return index < count ? index : 0; }

uint32_t bits_of(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// Zone_SetDistance_r (stream system in ECX, stdcall (zone, distance, 0)).
void set_zone_distance(uint32_t zone, float distance) {
    TitleRegs regs{};
    regs.ecx = title_global<uint32_t>(0x0095f638);
    const uint32_t args[3] = {zone, bits_of(distance), 0};
    cg_call_regs(0x0028ca10, &regs, args, 3);
}

bool connect_paths() {
    const uint8_t* const cvar = Cvar_Get(&title_global<const char>(0x00833a4c), &title_global<const char>(0x0082e098), 0);
    return word_at<int32_t>(cvar, 0x20) != 0;  // g_connectpaths
}

}  // namespace

uint32_t XB_REGS3 XB_CDECL Stream_BoundsContainPoint(const float* point, uint32_t, const float* bounds) {
    const float* const low = bounds;
    const float* const high = bounds + 4;
    if (!(low[0] <= point[0])) return 0;  // CMPLESS, lane 0
    // CMPLTPS over all four lanes (MXCSR), lanes 1 and 2 tested
    const bool below[4] = {point[0] < low[0], point[1] < low[1], point[2] < low[2], point[3] < low[3]};
    platform_keep(below);
    if (below[1] || below[2]) return 0;
    if (!(point[0] <= high[0])) return 0;
    const bool above[4] = {high[0] < point[0], high[1] < point[1], high[2] < point[2], high[3] < point[3]};
    platform_keep(above);
    if (above[1] || above[2]) return 0;
    return 1;
}

PLATFORM_ASM_ENTRY(Stream_NearestPoint_entry, "pushl %esi\n"
                                              "pushl %edx\n" PLATFORM_ASM_CALL(Stream_NearestPoint) "addl $8, %esp\n"
                                              "ret")

const float* XB_CDECL Stream_NearestPoint(const float* point, const uint8_t* volume) {
    const uint32_t count = word_at<uint32_t>(volume, 0x24);
    float best = title_global<float>(0x0082eed0);  // FLT_MAX
    const float* nearest = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        const float* const candidate =
            word_at<const float**>(volume, 0x28)[wrap_index(i, word_at<uint32_t>(volume, 0x24))];
        Lanes d = sub(lanes(candidate), lanes(point));
        d = mul(d, d);
        platform_keep(d);
        const float distance = d.v[0] + d.v[1] + d.v[2];
        if (best > distance) {
            best = distance;
            nearest = candidate;
        }
    }
    return nearest;
}

PLATFORM_ASM_ENTRY(Stream_PickVolumePoint_entry, "pushl 4(%esp)\n"
                                                 "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_PickVolumePoint) "addl $8, %esp\n"
                                                 "ret $4")

const float* XB_CDECL Stream_PickVolumePoint(const float* point, const uint8_t* volumes) {
    const float* picked = nullptr;
    for (uint32_t i = 0; i < word_at<uint32_t>(volumes, 0x24); ++i) {
        const uint8_t* const volume =
            word_at<const uint8_t**>(volumes, 0x28)[wrap_index(i, word_at<uint32_t>(volumes, 0x24))];
        if (Stream_BoundsContainPoint(point, 0, reinterpret_cast<const float*>(volume)) & 0xff)
            picked = Stream_NearestPoint(point, volume);
    }
    return picked;
}

PLATFORM_ASM_ENTRY(Stream_PointInAnyBox_entry, "pushl 4(%esp)\n"
                                               "pushl %edi\n" PLATFORM_ASM_CALL(Stream_PointInAnyBox) "addl $8, %esp\n"
                                               "ret $4")

uint8_t XB_CDECL Stream_PointInAnyBox(const uint8_t* set, const float* point) {
    const uint32_t count = word_at<uint32_t>(set, 4);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* const box = reinterpret_cast<const float*>(word_at<const uint8_t*>(set, 8) + wrap_index(i, count) * 32);
        if (Stream_BoundsContainPoint(point, 0, box) & 0xff) return 1;
    }
    return 0;
}

PLATFORM_ASM_ENTRY(Stream_SetZoneDistancesFromMask_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_SetZoneDistancesFromMask) "addl $8, %esp\n"
                   "ret $0xc")

uint32_t XB_CDECL Stream_SetZoneDistancesFromMask(const uint8_t* selector, const uint32_t* args) {
    uint32_t listed = 0;
    for (uint32_t i = 0; i < word_at<uint32_t>(selector, 0x10); ++i) {
        float distance;
        if (args[i >> 5] & (1u << (i & 31))) {
            const auto* const list = reinterpret_cast<const uint8_t*>(uintptr_t{args[2]});
            distance = word_at<const float*>(list, 4)[wrap_index(listed, word_at<uint32_t>(list, 0))];
            ++listed;
        } else {
            distance = title_global<float>(0x0082eed0);  // FLT_MAX
        }
        const uint8_t* const zone =
            word_at<const uint8_t**>(selector, 0x14)[wrap_index(i, word_at<uint32_t>(selector, 0x10))];
        const uint32_t target = word_at<uint32_t>(zone, 0x24);
        if (connect_paths()) distance = title_global<float>(0x0083eb20);  // 1.0
        set_zone_distance(target, distance);
    }
    return word_at<uint32_t>(selector, 0x10);
}

void XB_STDCALL Stream_UpdateZoneDistances(uint8_t* manager) {
    TitleRegs reset{};
    reset.esi = title_global<uint32_t>(0x0095f638);
    const uint32_t zero = 0;
    cg_call_regs(0x0028ca90, &reset, &zero, 1);  // Pak_ResetZoneDistances (stream system in ESI)
    for (int32_t index = word_at<int32_t>(manager, 0x1cc); index != -1;
         index = word_at<int32_t>(word_at<uint8_t*>(manager, 4 + uint32_t(index) * 4), 0x3c)) {
        const uint8_t* const selector = word_at<uint8_t*>(manager, 4 + uint32_t(index) * 4);
        if (const uint8_t* const picked = word_at<uint8_t*>(selector, 0x44)) {
            const uint32_t args[3] = {word_at<uint32_t>(picked, 0x18), word_at<uint32_t>(picked, 0x1c),
                                      uint32_t(uintptr_t(picked + 0x20))};
            Stream_SetZoneDistancesFromMask(selector, args);
        }
        for (uint32_t j = 0; j < word_at<uint32_t>(selector, 0x34); ++j) {
            const uint8_t* const flag =
                word_at<const uint8_t**>(selector, 0x38)[wrap_index(j, word_at<uint32_t>(selector, 0x34))];
            if (!word_at<uint32_t>(flag, 0)) continue;
            const uint32_t args[3] = {word_at<uint32_t>(flag, 0xc), word_at<uint32_t>(flag, 0x10),
                                      uint32_t(uintptr_t(flag + 0x14))};
            Stream_SetZoneDistancesFromMask(selector, args);
            for (uint32_t m = 0; m < word_at<uint32_t>(flag, 0x1c); ++m) {
                const uint8_t* const link = word_at<const uint8_t*>(flag, 0x20) + wrap_index(m, word_at<uint32_t>(flag, 0x1c)) * 12;
                const float listed = word_at<float>(link, 8);
                uint8_t* const zone = word_at<uint8_t*>(link, 4);
                float value = connect_paths() ? title_global<float>(0x0083eb20) : listed;
                float& current = word_at<float>(zone, 0x30);
                if (value > current) continue;
                if (value > current) value = current;  // MSVC's second COMISS
                current = value;
                const float distance = value == title_global<float>(0x0083eb34) ? title_global<float>(0x0082eed0) : value;
                for (uint32_t n = 0; n < word_at<uint32_t>(zone, 0x1c); ++n)
                    set_zone_distance(word_at<const uint32_t*>(zone, 0x20)[wrap_index(n, word_at<uint32_t>(zone, 0x1c))],
                                      distance);
            }
        }
    }
}

void XB_STDCALL Stream_SetViewPosition(uint8_t* manager, uint32_t cell, const float* origin, uint32_t force) {
    if (cell == 0xffffffff) return;
    std::memcpy(manager + 0x1b0, origin, 16);
    // The original keeps this flag in the low byte of `manager`'s argument slot.
    bool changed = word_at<uint32_t>(manager, 0x1c8) != word_at<uint32_t>(manager, 0x1d0);
    const int32_t first = word_at<int32_t>(manager, 0x1cc);
    word_at<uint32_t>(manager, 0x1c4) = cell;
    word_at<uint32_t>(manager, 0x1c8) = word_at<uint32_t>(manager, 0x1d0);
    for (int32_t index = first; index != -1;
         index = word_at<int32_t>(word_at<uint8_t*>(manager, 4 + uint32_t(index) * 4), 0x3c)) {
        uint8_t* const selector = word_at<uint8_t*>(manager, 4 + uint32_t(index) * 4);
        const uint32_t old = word_at<uint32_t>(selector, 0x44);
        const uint8_t* const volumes =
            word_at<const uint8_t**>(selector, 0x20)[wrap_index(cell, word_at<uint32_t>(selector, 0x1c))];
        word_at<const float*>(selector, 0x44) = Stream_PickVolumePoint(origin, volumes);
        for (uint32_t j = 0; j < word_at<uint32_t>(selector, 0x34); ++j) {
            uint32_t* const flag = word_at<uint32_t**>(selector, 0x38)[wrap_index(j, word_at<uint32_t>(selector, 0x34))];
            const uint8_t inside = Stream_PointInAnyBox(reinterpret_cast<const uint8_t*>(flag), origin);
            const uint8_t was = *flag != 0;
            *flag = inside;
            if (inside != was) changed = true;
        }
        if (old != word_at<uint32_t>(selector, 0x44)) changed = true;
    }
    if (changed || (force & 0xff)) Stream_UpdateZoneDistances(manager);
}

void XB_REGS3 XB_STDCALL Zone_SetDistance_r(uint32_t, uint32_t, uint32_t system, uint8_t* zone, float distance,
                                            uint32_t force) {
    float& current = word_at<float>(zone, 0x30);
    if (!(force & 0xff) && distance > current) return;
    if (distance > current) distance = current;
    current = distance;
    const float handed = distance == title_global<float>(0x0083eb34) ? title_global<float>(0x0082eed0) : distance;
    std::memcpy(&force, &handed, 4);  // the original's slot for it
    for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x1c); ++i)
        Zone_SetDistance_r(0, 0, system, word_at<uint8_t**>(zone, 0x20)[wrap_index(i, word_at<uint32_t>(zone, 0x1c))],
                           handed, 0);
}

PLATFORM_ASM_ENTRY(Pak_ResetZoneDistances_entry, "pushl 4(%esp)\n"
                                                 "pushl %esi\n" PLATFORM_ASM_CALL(Pak_ResetZoneDistances) "addl $8, %esp\n"
                                                 "ret $4")

int32_t XB_CDECL Pak_ResetZoneDistances(const uint8_t* system, uint32_t both) {
    const uint8_t* const zones = word_at<const uint8_t*>(system, 0x18) + 0x10;  // count, list
    int32_t i = 0;
    for (; i < word_at<int32_t>(zones, 0); ++i) {
        const uint32_t count = word_at<uint32_t>(zones, 0);
        word_at<float>(word_at<uint8_t**>(zones, 4)[wrap_index(uint32_t(i), count)], 0x30) = title_global<float>(0x0082eed0);
        if (both & 0xff)
            word_at<float>(word_at<uint8_t**>(zones, 4)[wrap_index(uint32_t(i), count)], 0x34) =
                title_global<float>(0x0082eed0);
    }
    return i;
}

void XB_REGS3 XB_STDCALL Zone_Release_r(uint32_t, uint32_t, uint32_t passed, uint8_t* zone) {
    --word_at<int32_t>(zone, 0x2c);
    for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x1c); ++i)
        Zone_Release_r(0, 0, passed, word_at<uint8_t**>(zone, 0x20)[wrap_index(i, word_at<uint32_t>(zone, 0x1c))]);
}

void XB_REGS3 XB_STDCALL Zone_AddRef_r(uint32_t, uint32_t, uint32_t passed, uint8_t* zone) {
    ++word_at<int32_t>(zone, 0x2c);
    for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x1c); ++i)
        Zone_AddRef_r(0, 0, passed, word_at<uint8_t**>(zone, 0x20)[wrap_index(i, word_at<uint32_t>(zone, 0x1c))]);
}

PLATFORM_ASM_ENTRY(Zone_FindUnloadedChild_entry, "pushl 4(%esp)\n"
                                                 "pushl %esi\n" PLATFORM_ASM_CALL(Zone_FindUnloadedChild) "addl $8, %esp\n"
                                                 "ret $4")

uint8_t* XB_CDECL Zone_FindUnloadedChild(const uint8_t* zone, const uint8_t* pak) {
    const uint32_t count = word_at<uint32_t>(zone, 0x1c);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* const child = word_at<uint8_t**>(zone, 0x20)[wrap_index(i, count)];
        const int32_t slot = word_at<int32_t>(child, 0x28);
        if (slot == -1) return child;
        const uint8_t* const loaded = word_at<const uint8_t*>(pak, 0x38 + uint32_t(slot) * 4);
        if (!loaded || word_at<uint32_t>(loaded, 0xdc) == 3) return child;
    }
    return nullptr;
}

PLATFORM_ASM_ENTRY(StreamMem_CountUsed_entry, "pushl %edi\n"
                                              "pushl %ecx\n" PLATFORM_ASM_CALL(StreamMem_CountUsed) "addl $8, %esp\n"
                                              "ret")

uint32_t XB_CDECL StreamMem_CountUsed(const uint8_t* memory, const uint32_t* bits) {
    uint32_t used = 0;
    for (uint32_t i = 0; i < word_at<uint32_t>(memory, 0x10); ++i) {
        const uint32_t bit = 1u << (i & 31);
        const uint32_t word = uint32_t(int32_t(i) >> 5);
        if ((bits[word] & bit) || (bits[word + 2] & bit)) ++used;
    }
    return used;
}

uint8_t* XB_REGS1 XB_STDCALL Zone_FindByHash(const uint8_t* table, uint32_t hash) {
    for (int32_t i = 0; i < 0x63; ++i) {
        const uint8_t* const map = word_at<const uint8_t*>(table, 4 + uint32_t(i) * 4);
        if (!map) continue;
        const uint32_t key = hash;
        const uint32_t* const found = UintMap_Find(reinterpret_cast<const uint32_t*>(map + 8), &key);
        if (!found) continue;
        uint8_t* const zone = word_at<uint8_t**>(map, 0x14)[wrap_index(*found, word_at<uint32_t>(map, 0x10))];
        if (zone) return zone;
    }
    return nullptr;
}

// StreamMem_Alloc keeps a value in XMM3 across the call (the original never writes it).
PLATFORM_ASM_ENTRY(StreamMem_CountFree_entry, "subl $16, %esp\n"
                                              "movups %xmm3, (%esp)\n"
                                              "pushl 20(%esp)\n"
                                              "pushl %edx\n" PLATFORM_ASM_CALL(StreamMem_CountFree) "addl $8, %esp\n"
                                              "movups (%esp), %xmm3\n"
                                              "addl $16, %esp\n"
                                              "ret $4")

float* XB_CDECL StreamMem_CountFree(const uint32_t* memory, float* out) {
    float free = 0.0f;
    bool half = false;
    for (uint32_t i = 0; i < memory[4]; ++i) {
        const uint32_t bit = 1u << (i & 31);
        const uint32_t word = uint32_t(int32_t(i) >> 5);
        const bool first = (memory[word] & bit) != 0;
        const bool second = (memory[word + 2] & bit) != 0;
        if (first && second) {
            free += title_global<float>(0x0083eb20);  // 1.0
        } else if (first != second && !half) {
            half = true;
            free += title_global<float>(0x0083eb24);  // 0.5
        }
    }
    out[0] = 0.0f;
    out[1] = free;
    out[2] = 0.0f;
    out[3] = 0.0f;
    return out;
}

PLATFORM_ASM_ENTRY(StreamMem_GetBlock_entry, "pushl 12(%esp)\n"
                                             "pushl 12(%esp)\n"
                                             "pushl 12(%esp)\n"
                                             "pushl %edi\n" PLATFORM_ASM_CALL(StreamMem_GetBlock) "addl $16, %esp\n"
                                             "ret $0xc")

uint32_t* XB_CDECL StreamMem_GetBlock(const uint32_t* bits, const uint8_t* memory, uint32_t* out, int32_t n) {
    const uint32_t count = word_at<uint32_t>(memory, 0x10);
    int32_t slot = -1;  // the matches counted so far, then the slot found
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i) {
        const uint32_t bit = 1u << (i & 31);
        const uint32_t word = uint32_t(int32_t(i) >> 5);
        if ((bits[word] & bit) && (bits[word + 2] & bit) && ++slot == n) {
            slot = int32_t(i);
            found = true;
        }
    }
    for (uint32_t i = 0; i < count && !found; ++i) {
        const uint32_t bit = 1u << (i & 31);
        const uint32_t word = uint32_t(int32_t(i) >> 5);
        if (((bits[word + 2] & bit) != 0) != ((bits[word] & bit) != 0) && ++slot == n) {
            slot = int32_t(i);
            found = true;
        }
    }
    const uint32_t bit = 1u << (uint32_t(slot) & 31);
    const int32_t word = slot >> 5;
    const uint32_t first = bits[word] & bit;
    const uint32_t size = word_at<uint32_t>(memory, 0x14);
    const uint32_t offset = first ? 0 : size >> 1;
    out[0] = size * uint32_t(slot) + word_at<uint32_t>(memory, 0x18) + offset;
    const bool half = ((bits[word + 2] & bit) != 0) != (first != 0);
    out[1] = size >> (half ? 1 : 0);
    return out;
}

PLATFORM_ASM_ENTRY(Zone_UpdatePriority_entry, "pushl %ecx\n"
                                              "subl $16, %esp\n"
                                              "movups %xmm4, (%esp)\n"
                                              "pushl %ecx\n" PLATFORM_ASM_CALL(Zone_UpdatePriority) "fstps (%esp)\n"
                                              "movss (%esp), %xmm0\n"
                                              "addl $4, %esp\n"
                                              "movups (%esp), %xmm4\n"
                                              "addl $16, %esp\n"
                                              "popl %ecx\n"
                                              "ret")

float XB_CDECL Zone_UpdatePriority(uint8_t* zone) {
    const uint32_t frame = title_global<uint32_t>(0x008d9d38);
    if (word_at<uint32_t>(zone, 0x40) == frame) return word_at<float>(zone, 0x3c);
    const float largest = title_global<float>(0x0082eed0);  // FLT_MAX
    const float none = title_global<float>(0x0083eb34);     // -1
    float priority = word_at<float>(zone, 0x34) == largest ? word_at<float>(zone, 0x30) : word_at<float>(zone, 0x34);
    if (word_at<float>(zone, 0x34) == none) return largest;
    if (word_at<float>(zone, 0x30) == none && word_at<float>(zone, 0x34) == largest) return largest;
    static constexpr uint8_t kCase[8] = {0, 0, 1, 0, 1, 1, 1, 0};  // byte table 0x0028c904
    const uint32_t state = word_at<uint32_t>(zone, 0);
    if (state <= 7 && kCase[state] == 0 && word_at<int32_t>(zone, 0x28) != -1) priority = 0.0f;
    word_at<uint32_t>(zone, 0x40) = frame;
    word_at<float>(zone, 0x3c) = priority;
    return priority;
}

// --- Streaming module push (2026-10-28) ---

namespace {

// Zone*Manager deleting destructors: `derived` is set first, the previous manager handed back, then the base vtable.
uint8_t* zone_manager_destruct(uint8_t* self, uint32_t flags, uint32_t derived, uint32_t base,
                               void (*restore)(uint32_t previous)) {
    const uint32_t previous = word_at<uint32_t>(self, 4);
    word_at<uint32_t>(self, 0) = derived;
    restore(previous);
    word_at<uint32_t>(self, 0) = base;
    if (flags & 1) Mem_AlignedFree(self);
    return self;
}

void* as_pointer(uint32_t value) { return reinterpret_cast<void*>(uintptr_t{value}); }

}  // namespace

uint8_t* XB_THISCALL ZoneTextureManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x0083813c, 0x008381cc,
                                 [](uint32_t previous) { nglSetTextureManager(as_pointer(previous)); });
}

uint8_t* XB_THISCALL ZoneMeshFileManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x00838184, 0x00838270,
                                 [](uint32_t previous) { nglSetMeshFileManager(as_pointer(previous)); });
}

uint8_t* XB_THISCALL ZoneMeshManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x008382b8, 0x00838228,
                                 [](uint32_t previous) { nglSetMeshManager(as_pointer(previous)); });
}

uint8_t* XB_THISCALL ZoneClipManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x00838160, 0x00838160,
                                 [](uint32_t previous) { title_global<uint32_t>(0x00909830) = previous; });
}

uint8_t* XB_THISCALL ZoneClipSetManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x008380f4, 0x008380f4,
                                 [](uint32_t previous) { title_global<uint32_t>(0x00909828) = previous; });
}

uint8_t* XB_THISCALL ZoneAnimManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x008381f0, 0x008381f0,
                                 [](uint32_t previous) { title_global<uint32_t>(0x0090982c) = previous; });
}

uint8_t* XB_THISCALL ZoneSkelManager_Destruct(uint8_t* self, uint32_t flags) {
    return zone_manager_destruct(self, flags, 0x00838294, 0x00838294,
                                 [](uint32_t previous) { title_global<uint32_t>(0x00909838) = previous; });
}

namespace {

// StringMap_Find (0x000249a0): map in EBX, a pointer to the key's pointer on the stack (stdcall); a pointer to the
// entry's index or null.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL Stream_StringMapFind_call(const uint8_t* map, const char* const* key);
PLATFORM_ASM_ENTRY(Stream_StringMapFind_call,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "pushl 12(%esp)\n"
                   "movl $0x000249a0, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

uint32_t* XB_REGS2 XB_STDCALL ZoneStringMap_Lookup(uint32_t, uint32_t zone_set, const uint8_t* zones, uint32_t* out,
                                                   const char* key, uint32_t, uint32_t* out_id) {
    if (zone_set == 0xffffffff) {
        *out = 0;
        return out;
    }
    uint32_t list[0x21];
    list[0] = 0;
    Stream_ListSearchZones(zone_set, list);
    for (int32_t i = 0; i < static_cast<int32_t>(list[0]); ++i) {
        const uint32_t id = list[1 + i];
        if (id == 0xffffffff) continue;
        const uint8_t* const zone = word_at<const uint8_t*>(zones, 4 + id * 4);
        if (!zone) continue;
        const uint32_t* const found = Stream_StringMapFind_call(zone + 8, &key);
        if (!found) continue;
        if (out_id) *out_id = id;
        uint32_t index = *found;
        if (index >= word_at<uint32_t>(zone, 0x10)) index = 0;
        *out = word_at<const uint32_t*>(zone, 0x14)[index];
        return out;
    }
    *out = 0;
    return out;
}

uint8_t XB_REGS2 XB_STDCALL StreamMem_HasSpace(uint32_t, const uint32_t* memory, uint32_t, float needed, uint32_t,
                                               uint32_t) {
    float free[4];
    StreamMem_CountFree(memory, free);
    if (needed > free[1]) return 0;
    return 1;
}

void XB_REGS3 XB_CDECL Stream_SetLoadProgress(uint32_t, int32_t value, uint8_t* record) {
    const int32_t total = word_at<int32_t>(record, 4) + value;
    word_at<int32_t>(record, 0) = value;
    const int32_t kilobytes = total / 1024;
    const float range = (0.0f - word_at<float>(record, 8)) + word_at<float>(record, 0xc);
    title_global<int32_t>(0x0095f61c) = kilobytes;
    if (range == 0.0f) {
        title_global<float>(0x0095f618) = 0.0f;
        return;
    }
    // FILD (+2^32 when negative), FDIV, FSTP: the x87 under the title's precision control (the * 1 is exact).
    title_global<float>(0x0095f618) =
        platform_x87_div_mul(platform_x87_unsigned_to_float(static_cast<uint32_t>(kilobytes)), range, 1.0f);
}

uint32_t XB_CDECL Stream_LoadCfgstr(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x18);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f5d4) + 4 + zone * 4) = address_of(data);
    return zone;
}

uint32_t XB_CDECL Stream_LoadDlg(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x18);
    title_global<uint32_t>(title_global<uint32_t>(0x0095f5ac) + 4 + zone * 4) = address_of(data);
    return zone;
}

void XB_CDECL Stream_LoadCg(char*, uint8_t* data, uint32_t, uint32_t zone) {
    CM_LoadWorldTree(title_global<uint8_t*>(0x0095f4fc), data, zone);
}

PLATFORM_ASM_ENTRY(Stream_UnrelocateZone_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_UnrelocateZone) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Stream_UnrelocateZone(uint8_t* image) {
    const uint32_t base = address_of(image);
    uint32_t i = 0;
    if (word_at<uint32_t>(image, 0xc) == 0) {
        word_at<uint32_t>(image, 8) -= base;
        return 0;
    }
    uint32_t offset = 0;
    do {
        uint8_t* const group = word_at<uint8_t*>(image, 8) + offset;
        for (int32_t j = 0; j < int32_t(word_at<uint16_t>(group, 0xe)); ++j) {
            uint32_t* const sub = reinterpret_cast<uint32_t*>(word_at<uint8_t*>(group, 0x14) + j * 0x14);
            sub[0] -= base;
            sub[1] = sub[1] ? sub[1] - base : 0;
        }
        word_at<uint32_t>(group, 0x10) -= base;
        word_at<uint32_t>(group, 0x14) -= base;
        ++i;
        offset += 0x18;
    } while (i < word_at<uint32_t>(image, 0xc));
    word_at<uint32_t>(image, 8) -= base;
    return i;
}

uint32_t XB_REGS2 XB_STDCALL Stream_RelocateZone(uint32_t, uint8_t* image, uint32_t skip) {
    const uint32_t base = address_of(image);
    word_at<uint32_t>(image, 8) += base;
    if (word_at<uint32_t>(image, 0xc) == 0) return 0;
    uint32_t i = 0;
    uint32_t offset = 0;
    do {
        uint8_t* const group = word_at<uint8_t*>(image, 8) + offset;
        const uint32_t first = word_at<uint32_t>(group, 0x10) + base;
        const uint32_t second = word_at<uint32_t>(group, 0x14) + base;
        word_at<uint32_t>(group, 0x10) = first;
        word_at<uint32_t>(group, 0x14) = second;
        if (static_cast<uint8_t>(skip)) {
            word_at<uint32_t>(group, 0x14) = 0;
        } else {
            for (int32_t j = 0; j < int32_t(word_at<uint16_t>(group, 0xe)); ++j) {
                uint32_t* const sub = reinterpret_cast<uint32_t*>(word_at<uint8_t*>(group, 0x14) + j * 0x14);
                sub[0] += base;
                sub[1] = sub[1] ? sub[1] + base : 0;
            }
        }
        ++i;
        offset += 0x18;
    } while (i < word_at<uint32_t>(image, 0xc));
    return i;
}

PLATFORM_ASM_ENTRY(Stream_AddClipToZone_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_AddClipToZone) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Stream_AddClipToZone(uint32_t clip, uint32_t streams) {
    return add_to_zone(streams, clip, clip + 8, 0x98, false);
}

PLATFORM_ASM_ENTRY(Stream_AddClipSetToZone_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_AddClipSetToZone) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Stream_AddClipSetToZone(uint32_t set, uint32_t streams) {
    NAL_ClipSetPlayer_Create(reinterpret_cast<uint8_t*>(uintptr_t{set}), 0, 0);
    return add_to_zone(streams, set, set + 0x10, 0xa8, false);
}

uint32_t XB_THISCALL ZoneClipManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddClipToZone(o, streams()); });
}

uint32_t XB_THISCALL ZoneClipSetManager_Add(const uint8_t* self, uint32_t object) {
    return zone_manager_add(self, object, [](uint32_t o) { return Stream_AddClipSetToZone(o, streams()); });
}

namespace {

// The value word of entry i of a zone map's entry list (8 bytes each, +list), the index wrapped to 0 at +count.
uint32_t& zone_entry_value(uint8_t* zone, uint32_t count, uint32_t list, uint32_t i) {
    return word_at<uint32_t>(word_at<uint8_t*>(zone, list), wrap_index(i, word_at<uint32_t>(zone, count)) * 8 + 4);
}

uint8_t* zone_of(const uint8_t* streams, uint32_t id) { return word_at<uint8_t*>(streams, 0x5c + id * 4); }

}  // namespace

void XB_REGS3 XB_CDECL Stream_ReleaseFonts(const uint8_t* streams, uint32_t, uint32_t id) {
    uint8_t* const zone = zone_of(streams, id);
    if (!zone) return;
    for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x20); ++i) {
        nglReleaseFont(as_pointer(zone_entry_value(zone, 0x20, 0x24, i)));
        zone_entry_value(zone, 0x20, 0x24, i) = 0;
    }
}

void XB_REGS3 XB_CDECL Stream_ReleaseEffects(const uint8_t* streams, uint32_t, uint32_t id) {
    uint8_t* const zone = zone_of(streams, id);
    if (!zone) return;
    for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0xd0); ++i) {
        uint8_t* const effect = static_cast<uint8_t*>(as_pointer(zone_entry_value(zone, 0xd0, 0xd4, i)));
        if (effect) {
            FxEffectDef_Release(effect);
            title_function<void XB_CDECL(void*)>(0x000428d0)(effect);  // Z_Free
        }
        zone_entry_value(zone, 0xd0, 0xd4, i) = 0;
    }
}

void XB_REGS1 XB_STDCALL Stream_ReleaseZoneSkeletons(uint32_t id, uint8_t* streams) {
    streams[0x54] = 1;
    uint8_t* const zone = zone_of(streams, id);
    if (zone) {
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0xc0); ++i) {
            NAL_Skeleton_Release(zone_entry_value(zone, 0xc0, 0xc4, i));
            zone_entry_value(zone, 0xc0, 0xc4, i) = 0;
        }
    }
    streams[0x54] = 0;
}

void XB_REGS1 XB_STDCALL Stream_ReleaseTextures(uint32_t id, uint8_t* streams) {
    streams[0xc] = 1;
    uint8_t* const zone = zone_of(streams, id);
    if (zone) {
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x10); ++i) {
            uint8_t* const texture = static_cast<uint8_t*>(as_pointer(zone_entry_value(zone, 0x10, 0x14, i)));
            if (texture && (texture[0x28] & 1)) {
                const bool pending = word_at<uint32_t>(texture, 0xc) == 3;
                if (pending) {
                    streams[0xc] = 0;
                    streams[0xe] = 1;
                }
                nglReleaseTexture(texture);
                if (pending) streams[0xc] = 1;
            }
            zone_entry_value(zone, 0x10, 0x14, i) = 0;
        }
    }
    streams[0xc] = 0;
}

void XB_STDCALL Stream_ReleaseMeshFiles(uint8_t* streams, uint32_t id) {
    streams[0x24] = 1;
    streams[0x18] = 1;
    uint8_t* const zone = zone_of(streams, id);
    if (zone) {
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x40); ++i) {
            const uint32_t file = zone_entry_value(zone, 0x40, 0x44, i);
            if (!file) continue;
            nglReleaseMeshFile(as_pointer(file));
            zone_entry_value(zone, 0x40, 0x44, i) = 0;
        }
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x30); ++i) zone_entry_value(zone, 0x30, 0x34, i) = 0;
    }
    streams[0x24] = 0;
    streams[0x18] = 0;
}

uint32_t* XB_REGS2 XB_STDCALL Stream_ForEachAssetWithPrefix(uint32_t, uint32_t zone_set, const uint8_t* zones,
                                                            uint32_t* out, const char* prefix, uint32_t length,
                                                            uint32_t callback, uint32_t) {
    uint32_t list[0x21];
    list[0] = 0;
    Stream_ListSearchZones(zone_set, list);
    for (uint32_t i = 0; i < list[0]; ++i) {
        const uint32_t id = list[1 + i];
        if (id == 0xffffffff) continue;
        const uint8_t* const zone = word_at<const uint8_t*>(zones, 4 + id * 4);
        if (!zone) continue;
        for (uint32_t j = 0; j < word_at<uint32_t>(zone, 0x10); ++j) {
            const uint8_t* const asset =
                word_at<const uint8_t* const*>(zone, 0x14)[wrap_index(j, word_at<uint32_t>(zone, 0x10))];
            const char* const name = word_at<const char*>(asset, 0);
            if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0)(  // _strncmp
                    name, prefix, length) != 0)
                continue;
            title_function<void XB_CDECL(const char*, const uint8_t*)>(callback)(
                word_at<const char*>(asset, 0) + length + 1, asset);
        }
    }
    *out = 0;
    return out;
}

uint32_t XB_REGS3 XB_CDECL Stream_SeekNextItem(uint32_t, uint32_t, uint8_t* request) {
    const uint32_t group_offset = word_at<uint32_t>(request, 0x80) * 0x18;
    const uint32_t item = word_at<uint32_t>(request, 0xd4);
    const uint8_t* const image = word_at<const uint8_t*>(request, 0x7c);
    if (item == word_at<uint16_t>(word_at<const uint8_t*>(image, 8), group_offset + 0xe)) {
        word_at<uint32_t>(request, 0xe0) = 3;
        word_at<uint32_t>(request, 0xd8) = 0;
        return group_offset;
    }
    const uint8_t* const group = word_at<const uint8_t*>(image, 8) + group_offset;
    const uint8_t* const entry = word_at<const uint8_t*>(group, 0x14) + item * 0x14;
    const uint8_t* const table = word_at<const uint8_t*>(group, 0x10);
    const uint32_t address = word_at<uint32_t>(table, word_at<uint32_t>(entry, 0xc) * 0x1c + 0x14) +
                             word_at<uint32_t>(entry, 0x10);
    word_at<uint32_t>(request, 0xd8) = address;
    return address;
}

uint32_t XB_REGS3 XB_STDCALL Stream_FindInZones(uint32_t active, uint32_t, const uint32_t* hashed, uint32_t streams,
                                                uint32_t zone, uint32_t) {
    return stream_find(streams, zone, hashed, static_cast<uint8_t>(active),
                       [](uint32_t hash, const char* key, const uint8_t* z) { return ZoneMap28_Lookup(hash, key, z); });
}

PLATFORM_ASM_ENTRY(Stream_RelocateAnimTrees_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_RelocateAnimTrees) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL Stream_RelocateAnimTrees(uint8_t* data, uint8_t* table) {
    relocate_pack(data, 8);
    word_at<uint32_t>(table, 4) = address_of(data);
    for (uint32_t i = 0; i < word_at<uint32_t>(data, 0); ++i) {
        uint8_t* const group = word_at<uint8_t*>(data, 4) + wrap_index(i, word_at<uint32_t>(data, 0)) * 12;
        for (uint32_t j = 0; j < word_at<uint32_t>(group, 4); ++j) {
            uint8_t* const node = word_at<uint8_t*>(group, 8) + wrap_index(j, word_at<uint32_t>(group, 4)) * 0x38;
            word_at<uint32_t>(node, 0x28) = 0;
            word_at<uint32_t>(node, 0x24) = 0;
        }
    }
}

void XB_CDECL Stream_LoadAtrb(char*, uint8_t* data, uint32_t, uint32_t) {
    Stream_RelocateAnimTrees(data, title_global<uint8_t*>(0x0095f524));
}

uint32_t XB_REGS1 XB_STDCALL Stream_RelocateDcg(uint8_t* data, uint8_t* table, uint32_t zone) {
    uint32_t cursor = 8;
    Dcg_RelocateModels(&cursor, reinterpret_cast<uint32_t*>(data), address_of(data));
    word_at<uint32_t>(table, 4 + zone * 4) = address_of(data);
    uint32_t i = 0;
    if (word_at<uint32_t>(data, 0) == 0) return 0;
    uint32_t offset = 0;
    do {
        uint8_t* const model = word_at<uint8_t*>(data, 4) + offset;
        for (uint32_t j = 0; j < word_at<uint32_t>(model, 0); ++j) {
            uint8_t* const brush = word_at<uint8_t*>(model, 4) + j * 0x40;
            const uint32_t flags = word_at<uint32_t>(brush, 8);
            word_at<uint32_t>(brush, 0) = 0x008325ec;  // CM_Brush_vtable
            if (flags & 0x1000000) word_at<uint32_t>(brush, 8) = flags & ~0x1000000u;
            const uint32_t moved = word_at<uint32_t>(brush, 8);
            if (moved & 0x4000) word_at<uint32_t>(brush, 8) = (moved & ~0x4000u) | 0x1000000;
        }
        ++i;
        offset += 0x50;
    } while (i < word_at<uint32_t>(data, 0));
    return i;
}

void XB_CDECL Stream_LoadDcg(char*, uint8_t* data, uint32_t, uint32_t zone) {
    Stream_RelocateDcg(data, title_global<uint8_t*>(0x0095f500), zone);
}

PLATFORM_ASM_ENTRY(Stream_AddFreeBlock_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_AddFreeBlock) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Stream_AddFreeBlock(uint32_t start, uint8_t* context, uint32_t size) {
    if (size <= 0x2000 || word_at<int32_t>(context, 0x8c) >= 0xb) return 0;
    const uint32_t heap = title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00044850)(  // SmallHeap_Alloc
        title_global<uint32_t>(0x0095f628), 0x494, 0);
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00042470)(  // Heap_Init
        heap, (start + 0x1f) & ~0x1fu, start + size, 0);
    const int32_t count = word_at<int32_t>(context, 0x8c);
    if (count < 0xc) {
        word_at<uint32_t>(context, 0x90 + uint32_t(count) * 4) = heap;
        ++word_at<int32_t>(context, 0x8c);
    }
    return 0;
}

uint32_t XB_REGS2 XB_STDCALL MemContext_AddFreeBlock(uint8_t* context, uint32_t size, uint32_t start) {
    uint8_t* last = nullptr;
    const uint32_t count = word_at<uint32_t>(context, 0x8c);
    if (count) {
        const int32_t top = int32_t(count) - 1;
        last = word_at<uint8_t*>(context, 0x90 + uint32_t(top > 0 ? top : 0) * 4);
    }
    uint8_t* const heap = static_cast<uint8_t*>(as_pointer(Stream_AddFreeBlock(start, context, size)));
    if (heap && last) {
        word_at<uint32_t>(heap, 0x490) = word_at<uint32_t>(last, 0x490);
        word_at<uint32_t>(last, 0x490) = address_of(heap);
    }
    return address_of(heap);
}

PLATFORM_ASM_ENTRY(Stream_ResolveXmbParts_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_ResolveXmbParts) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Stream_ResolveXmbParts(uint8_t* list, uint32_t zone_set) {
    uint32_t i = 0;
    if (word_at<uint32_t>(list, 0x10) == 0) return 0;
    do {
        uint8_t* const model = word_at<uint8_t**>(list, 0x14)[i];
        for (uint32_t slot = 0x24; slot <= 0x34; slot += 4) {
            if (!word_at<uint8_t*>(model, slot)) continue;
            uint32_t file = 0;
            ZoneStringMap_Lookup(0, zone_set, title_global<const uint8_t*>(0x0095f6b0), &file,
                                 word_at<const char*>(word_at<uint8_t*>(model, slot), 4), 0, nullptr);
            word_at<uint32_t>(word_at<uint8_t*>(model, slot), 8) = file;
            if (!word_at<uint32_t>(model, 0x20)) word_at<uint32_t>(model, 0x20) = word_at<uint32_t>(word_at<uint8_t*>(model, slot), 8);
        }
        ++i;
    } while (i < word_at<uint32_t>(list, 0x10));
    return i;
}

PLATFORM_ASM_ENTRY(ZoneManagers_Destruct_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ZoneManagers_Destruct) "addl $4, %esp\n"
                   "ret")

void XB_CDECL ZoneManagers_Destruct(uint8_t* self) {
    word_at<uint32_t>(self, 0) = 0x00838300;  // ZoneManagers vtable
    const uint32_t skel = word_at<uint32_t>(self, 0x50);
    word_at<uint32_t>(self, 0x4c) = 0x00838294;
    const uint32_t anim = word_at<uint32_t>(self, 0x44);
    word_at<uint32_t>(self, 0x40) = 0x008381f0;
    const uint32_t clip_set = word_at<uint32_t>(self, 0x38);
    word_at<uint32_t>(self, 0x34) = 0x008380f4;
    word_at<uint32_t>(self, 0x28) = 0x00838160;
    title_global<uint32_t>(0x0090982c) = anim;
    title_global<uint32_t>(0x00909838) = skel;
    title_global<uint32_t>(0x00909828) = clip_set;
    title_global<uint32_t>(0x00909830) = word_at<uint32_t>(self, 0x2c);
    word_at<uint32_t>(self, 0x1c) = 0x008382b8;
    nglSetMeshManager(as_pointer(word_at<uint32_t>(self, 0x20)));
    word_at<uint32_t>(self, 0x1c) = 0x00838228;
    word_at<uint32_t>(self, 0x10) = 0x00838184;
    nglSetMeshFileManager(as_pointer(word_at<uint32_t>(self, 0x14)));
    word_at<uint32_t>(self, 0x10) = 0x00838270;
    word_at<uint32_t>(self, 4) = 0x0083813c;
    nglSetTextureManager(as_pointer(word_at<uint32_t>(self, 8)));
    word_at<uint32_t>(self, 4) = 0x008381cc;
    ResourceManager_Destruct(self);
}

uint8_t* XB_THISCALL ZoneManagers_DeletingDtor(uint8_t* self, uint32_t flags) {
    ZoneManagers_Destruct(self);
    if (flags & 1) title_function<void XB_CDECL(void*)>(0x000428d0)(self);  // Z_Free
    return self;
}

uint8_t* XB_THISCALL ZoneGraph_DeletingDtor(uint8_t* self, uint32_t flags) {
    word_at<uint32_t>(self, 0) = 0x00838214;  // ZoneGraph vtable
    Cmd_RemoveCommand(&title_global<const char>(0x00838320));  // "ZoneGraph"
    word_at<uint32_t>(self, 0) = 0x00838308;
    ResourceManager_Destruct(self);
    if (flags & 1) title_function<void XB_CDECL(void*)>(0x000428d0)(self);  // Z_Free
    return self;
}

uint8_t* XB_THISCALL ZoneGraphBase_DeletingDtor(uint8_t* self, uint32_t flags) {
    word_at<uint32_t>(self, 0) = 0x00838308;
    ResourceManager_Destruct(self);
    if (flags & 1) title_function<void XB_CDECL(void*)>(0x000428d0)(self);  // Z_Free
    return self;
}

PLATFORM_ASM_ENTRY(ZoneGraph_Init_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ZoneGraph_Init) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL ZoneGraph_Init(uint8_t* self) {
    word_at<uint32_t>(self, 0) = 0x00838220;  // ResourceManager vtable
    const int32_t managers = title_global<int32_t>(0x009719f8);
    if (managers < 0x14) {
        title_global<uint32_t>(0x009719fc + uint32_t(managers) * 4) = address_of(self);
        ++title_global<int32_t>(0x009719f8);
    }
    for (uint32_t offset = 4; offset < 4 + 0x63 * 4; offset += 4) word_at<uint32_t>(self, offset) = 0;
    word_at<uint32_t>(self, 0) = 0x00838214;  // ZoneGraph vtable
    word_at<uint32_t>(self, 0x1c4) = 0xffffffff;
    word_at<uint32_t>(self, 0x1c8) = 0;
    word_at<uint32_t>(self, 0x1cc) = 0xffffffff;
    word_at<uint32_t>(self, 0x1d0) = 0;
    const int32_t callbacks = title_global<int32_t>(0x00988780);
    if (callbacks < 0x20) {
        title_global<uint32_t>(0x00988784 + uint32_t(callbacks) * 4) = 0x0001b9a0;
        ++title_global<int32_t>(0x00988780);
    }
    word_at<uint32_t>(self, 0x190) = 0;
    word_at<uint32_t>(self, 0x194) = 0;
    word_at<uint32_t>(self, 0x190) &= ~1u;
    Cmd_AddCommand(&title_global<const char>(0x00838320), 0x0028c430);  // "ZoneGraph"
    return self;
}

uint8_t XB_STDCALL Zone_FreeSimpleInstances(const uint8_t* table, uint32_t zone) {
    uint8_t saved = 0;
    if (zone != 0xffffffff) {
        saved = title_global<uint8_t>(0x0095f5d0);
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = 1;
        MemContext_Push(global_context(), zone);
    }
    uint8_t* const data = word_at<uint8_t*>(table, 4 + zone * 4);
    for (uint32_t i = 0; i < word_at<uint32_t>(data, 0x20); ++i) {
        uint8_t* instance =
            word_at<uint8_t*>(word_at<uint8_t*>(data, 0x24), wrap_index(i, word_at<uint32_t>(data, 0x20)) * 16 + 4);
        while (instance) {
            uint8_t* const next = word_at<uint8_t*>(instance, 0x14);
            cdSimpleInstance_FreeList(reinterpret_cast<CdSimpleInstanceList*>(instance));
            Mem_AlignedFree(instance);
            instance = next;
        }
        word_at<uint32_t>(word_at<uint8_t*>(data, 0x24), wrap_index(i, word_at<uint32_t>(data, 0x20)) * 16 + 4) = 0;
    }
    if (zone == 0xffffffff) return saved;
    title_global<uint8_t>(0x0095f5d1) = 0;
    title_global<uint8_t>(0x0095f5d0) = saved;
    MemContext_Pop(global_context());
    return saved;
}

uint32_t XB_REGS2 XB_STDCALL StreamMem_MoveSlots(uint32_t, uint32_t* from, uint32_t* to, int32_t n) {
    int32_t counted = 0;
    bool moved = false;
    for (uint32_t i = 0; i < to[4]; ++i) {
        const uint32_t bit = 1u << (i & 31);
        const uint32_t word = uint32_t(int32_t(i) >> 5);
        if (!(from[word] & bit) || !(from[word + 2] & bit)) continue;
        if (n == -1 || counted == n) {
            from[word] &= ~bit;
            to[word] |= bit;
            from[word + 2] &= ~bit;
            to[word + 2] |= bit;
            moved = true;
        }
        ++counted;
    }
    if (moved && n != -1) return to[4];
    for (uint32_t i = 0; i < to[4]; ++i) {
        const uint32_t bit = 1u << (i & 31);
        const uint32_t word = uint32_t(int32_t(i) >> 5);
        const bool first = (from[word] & bit) != 0;
        const bool second = (from[word + 2] & bit) != 0;
        if (first == second) continue;
        if (n == -1 || counted == n) {
            if (first) {
                from[word] &= ~bit;
                to[word] |= bit;
            }
            if (second) {
                from[word + 2] &= ~bit;
                to[word + 2] |= bit;
            }
        }
        ++counted;
    }
    return to[4];
}

uint8_t* XB_REGS1 XB_CDECL Stream_RelocateBtrNodes(uint8_t* tree) {
    const auto count = [tree] { return word_at<uint32_t>(tree, 8); };
    const auto node = [tree, count](uint32_t i) { return word_at<uint8_t*>(tree, 0xc) + wrap_index(i, count()) * 16; };
    for (uint32_t i = 0; i < count(); ++i) {
        if (word_at<uint16_t>(node(i), 0) != 0xffff) continue;
        const uint32_t left = wrap_index(word_at<uint32_t>(node(i), 8), count());
        word_at<uint32_t>(node(i), 8) = address_of(word_at<uint8_t*>(tree, 0xc) + left * 16);
        const uint32_t right = wrap_index(word_at<uint32_t>(node(i), 0xc), count());
        word_at<uint32_t>(node(i), 0xc) = address_of(word_at<uint8_t*>(tree, 0xc) + right * 16);
        const uint32_t leaf = wrap_index(word_at<uint32_t>(node(i), 4), word_at<uint32_t>(tree, 0x10));
        word_at<uint32_t>(node(i), 4) = address_of(word_at<uint8_t*>(tree, 0x14) + leaf * 16);
    }
    return tree;
}

namespace {

char* title_text(uint32_t address) { return &title_global<char>(address); }

}  // namespace

void XB_CDECL Stream_LoadBtr(char* name, uint8_t* data, uint32_t, uint32_t) {
    const uint32_t table = word_at<uint32_t>(data, 0x7c) + address_of(data);
    title_global<uint8_t*>(0x0095f6b8) = data;  // cm
    word_at<uint32_t>(data, 0x7c) = table;
    Pack_RelocateTable(reinterpret_cast<PackRelocTable*>(uintptr_t{table}), address_of(data));
    Stream_RelocateBtrNodes(title_global<uint8_t*>(0x0095f6b8));
    if (title_global<uint32_t>(0x0095e394))
        title_function<void XB_CDECL(uint32_t, const char*)>(title_global<uint32_t>(0x0095e634))(1, title_text(0x0083c524));
    for (uint32_t offset = 0; offset < 0x43 * 4; offset += 4) title_global<uint32_t>(0x00988bd8 + offset) = 0;
    title_global<uint32_t>(0x0095e394) = 1;
    title_global<uint32_t>(0x0095e608) = 0;
    using Strncpy = char* XB_CDECL(char*, const char*, uint32_t);
    title_function<Strncpy>(0x001eed80)(title_text(0x00988bd8), name, 0x7f);  // _strncpy
    title_global<char>(0x00988c57) = 0;
    const char* base = title_text(0x00988bd8);
    for (const char* p = title_text(0x00988bd8); *p; ++p)
        if (*p == '/') base = p + 1;
    title_function<Strncpy>(0x001eed80)(title_text(0x00988c58), base, 0x7f);  // _strncpy
    title_global<char>(0x00988cd7) = 0;
    char* p = title_text(0x00988c58);
    while (*p && *p != '.') {
        *p = *p;
        ++p;
    }
    *p = 0;
    title_global<uint32_t>(0x0095e608) = 0x00988bd8;
    SV_ClearWorld();
}

PLATFORM_ASM_ENTRY(Stream_LoadXmbInto_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_LoadXmbInto) "addl $12, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL Stream_LoadXmbInto(uint8_t* data, uint32_t zone, uint8_t* table) {
    relocate_pack(data, 0x18);
    Stream_ResolveXmbParts(data, zone);
    word_at<uint32_t>(table, 4 + zone * 4) = address_of(data);
    return table;
}

PLATFORM_ASM_ENTRY(Stream_RelocatePathData_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_RelocatePathData) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL Stream_RelocatePathData(const char* name, uint8_t* data, uint8_t* blocks) {
    char text[0x80];
    int32_t length = 0;
    Q_strncpyLen(text, &length, 0x7f, name);
    text[0x7f] = static_cast<char>(length);
    char base[0x80];
    FixedStr_BaseName(text, base, 1);
    std::memcpy(text, base, 0x80);
    if (!word_at<uint8_t*>(blocks, 0x194)) return;
    for (uint32_t offset = 0x10; offset <= 0x28; offset += 4) word_at<uint32_t>(data, offset) += address_of(data);
    for (int32_t i = 0; i < word_at<int32_t>(word_at<uint8_t*>(blocks, 0x194), 0xc); ++i)
        word_at<uint32_t>(word_at<uint8_t*>(data, 0x28), uint32_t(i) * 8) += address_of(data);
    const int32_t index =
        title_function<int32_t XB_STDCALL(uint8_t*, const char*)>(0x002c0c50)(blocks, text);  // Path_FindZoneBlockIndex
    if (index < 0 || word_at<uint32_t>(data, 0) != 2) return;
    word_at<uint32_t>(blocks, 8 + uint32_t(index) * 4) = address_of(data);
    title_function<void XB_STDCALL(uint8_t*, int32_t)>(0x002c09e0)(blocks, index);  // Path_RelocateNodeBlock
}

void XB_CDECL Stream_LoadZpth(char* name, uint8_t* data, uint32_t, uint32_t) {
    Stream_RelocatePathData(name, data, title_global<uint8_t*>(0x0095f4f4));
}

void XB_STDCALL Stream_UnloadNotifyManagers(const uint8_t* record) {
    MemContext_Push(global_context(), word_at<uint32_t>(record, 0x74));
    const uint32_t zone = word_at<uint32_t>(record, 0x74);
    const uint32_t end = 0x009719fc + title_global<uint32_t>(0x009719f8) * 4;
    for (uint32_t slot = 0x009719fc; slot != end; slot += 4) {
        const uint32_t* const manager = title_global<const uint32_t*>(slot);
        title_function<void XB_THISCALL(const uint32_t*, uint32_t)>(vtable_slot(manager, 1))(manager, zone);
    }
    FX_KillEffectsOnEntity(int32_t(word_at<uint32_t>(record, 0x74)));
    CodNote_FreeForZone(word_at<uint32_t>(record, 0x74));
    MemContext_Pop(global_context());
}

PLATFORM_ASM_ENTRY(CodNote_FreeForZone_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CodNote_FreeForZone) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CodNote_FreeForZone(uint32_t zone) {
    uint8_t* node = title_global<uint8_t*>(0x008dda50);
    uint8_t* next = node ? word_at<uint8_t*>(node, 0) : nullptr;
    if (address_of(node) == 0x008dda54 || !next) return;
    do {
        uint8_t* const current = node;
        node = next;
        next = word_at<uint8_t*>(next, 0);
        if (word_at<uint32_t>(current, 0x24) == zone)
            title_function<void XB_CDECL(uint8_t*)>(0x002be860)(current);  // CodNote_Free
    } while (next);
}

// --- Streaming module push, second part ---

namespace {

void debug_break() { title_function<void XB_CDECL()>(0x00042460)(); }  // Sys_DebugBreak

void z_free(uint32_t block) { title_function<void XB_CDECL(uint32_t)>(0x000428d0)(block); }

// The memory context of zone `id` on the global context (+0x38 + id * 4), none outside 0..0x62.
uint8_t* zone_context(int32_t id) {
    if (id < 0 || id >= 0x63) return nullptr;
    return word_at<uint8_t*>(global_context(), 0x38 + uint32_t(id) * 4);
}

uint8_t memcontext_free(uint8_t* context, uint32_t block) {
    return title_function<uint8_t XB_THISCALL(uint8_t*, uint32_t)>(0x0028bfc0)(context, block);  // MemContext_Free_r
}

uint32_t memcontext_alloc(uint8_t* context, uint32_t align, uint32_t size) {
    return title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t, uint32_t)>(0x0028c0f0)(context, align,
                                                                                          size);  // MemContext_Alloc_r
}

// A block of a zone's context: MemContext_Free_r on it, Z_Free when that refuses or the id is -1.
void free_zone_block(uint32_t id, uint32_t block) {
    if (id != 0xffffffff && memcontext_free(zone_context(int32_t(id)), block)) return;
    z_free(block);
}

// Heap 0's 0x1000-aligned block of `size` bytes; Sys_DebugBreak when out of memory.
uint8_t* bank_alloc(uint32_t size) {
    const uint32_t heap = title_function<uint32_t XB_CDECL(uint32_t)>(0x00042ac0)(0);  // Heap_SelectByIndex
    uint8_t* const block = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x1000, size);
    title_function<void XB_CDECL(uint32_t)>(0x00042ca0)(heap);  // Heap_SetCurrent
    if (!block) {
        char text[0x80];
        title_function<int XB_CDECL(char*, const char*, uint32_t)>(0x001ee901)(text, title_text(0x0083844c), size);
        debug_break();
    }
    return block;
}

}  // namespace

// Scn_SpawnZone's subtree stores stack addresses in globals (0x00929b40): it runs where the original calls it (entry - 4
// after its push esi) with the C frame below a gap; Stream_LoadScn runs this at its own original ESP (entry - 8).
PLATFORM_ASM_ENTRY(Stream_ParseScn_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "leal 4(%ebp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_ParseScn) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL Stream_ParseScn(uint8_t* data, uint8_t* table, uint32_t zone, uint32_t entry) {
    relocate_pack(data, 0x4c);
    word_at<uint32_t>(table, 4 + zone * 4) = address_of(data);
    const uint32_t start = word_at<uint32_t>(data, 0x48);
    if (start) MemContext_AddFreeBlock(zone_context(int32_t(zone)), word_at<uint32_t>(data, 8), start);
    uint8_t* const list = data + 0x40;
    for (uint32_t i = 0; i < word_at<uint32_t>(list, 0); ++i)
        word_at<uint8_t*>(list, 4)[wrap_index(i, word_at<uint32_t>(list, 0))] = 0;
    const TitleRegs regs{word_at<uint32_t>(list, 0), address_of(table), 0, 0, address_of(list), zone};
    const uint32_t args[2] = {address_of(table), zone};
    const uint32_t result = title_call_at_regs(entry - 4, 0x002ee550, &regs, 2, args);  // Scn_SpawnZone
    if (word_at<uint32_t>(list, 0)) word_at<uint32_t>(table, 0x194) = address_of(list);
    return result;
}

PLATFORM_ASM_ENTRY(Stream_LoadScn_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl 20(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_LoadScn) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL Stream_LoadScn(uint32_t entry, uint8_t* data, uint32_t zone) {
    const TitleRegs regs{address_of(data), title_global<uint32_t>(0x0095f4f8), 0, 0, 0, zone};
    return title_call_at_regs(entry - 8, 0x002ee600, &regs, 0, nullptr);  // Stream_ParseScn
}

uint32_t XB_CDECL Stream_LoadXmpb(char*, uint8_t* data, uint32_t, uint32_t zone) {
    const uint32_t table = title_global<uint32_t>(0x0095f6b0);  // rm_xmpb
    relocate_pack(data, 0x18);
    const uint32_t result =
        title_function<uint32_t XB_STDCALL(uint8_t*, uint32_t)>(0x00241d00)(data, zone);  // Stream_ResolveXmpbSkeletons
    title_global<uint32_t>(table + 4 + zone * 4) = address_of(data);
    return result;
}

uint32_t XB_CDECL Stream_LoadFli(char*, uint8_t* data, uint32_t, uint32_t) {
    return title_function<uint32_t XB_REGS3 XB_CDECL(uint8_t*, uint32_t, uint8_t*)>(0x0028cbd0)(  // Pak_ParseFli
        data, 0, global_context());
}

uint32_t XB_CDECL Stream_LoadSpg(char*, uint8_t* data, uint32_t, uint32_t zone) {
    const uint32_t table = title_global<uint32_t>(0x0095f564);  // rm_spg
    uint32_t slot = 0;
    for (uint32_t i = 0; i < 0x20; ++i) {
        if (title_global<uint32_t>(table + 4 + i * 12) == 0xffffffff) {
            slot = table + 4 + i * 12;
            break;
        }
    }
    if (!slot) debug_break();
    volatile uint32_t* const entry = reinterpret_cast<uint32_t*>(uintptr_t{slot});  // address 0 when full, as the original
    entry[0] = zone;
    entry[2] = address_of(data);
    const uint32_t table_address = word_at<uint32_t>(data, 0x18) + address_of(data);
    word_at<uint32_t>(data, 0x18) = table_address;
    return Pack_RelocateTable(reinterpret_cast<PackRelocTable*>(uintptr_t{table_address}), address_of(data));
}

PLATFORM_ASM_ENTRY(ZoneRecord_GetContext_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(ZoneRecord_GetContext) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL ZoneRecord_GetContext(uint8_t* record) {
    if (!word_at<uint32_t>(record, 0x78))
        word_at<uint32_t>(record, 0x78) = title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(0x0028e550)(
            global_context(), word_at<uint32_t>(record, 0x74));  // MemContext_FindById
    return word_at<uint32_t>(record, 0x78);
}

void XB_REGS1 XB_CDECL StreamMem_Free(uint32_t* memory) {
    for (uint32_t i = 0; i < memory[4]; ++i) {
        const uint32_t bit = 1u << (i & 0x1f);
        const int32_t word = int32_t(i) >> 5;
        if (!(memory[word] & bit) || !(memory[word + 2] & bit)) debug_break();
    }
    if (memory[6]) z_free(memory[6]);
}

PLATFORM_ASM_ENTRY(Stream_Alloc_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_Alloc) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Stream_Alloc(uint32_t size) {
    if (!size) return nullptr;
    return bank_alloc(size);
}

PLATFORM_ASM_ENTRY(Stream_InitBanks_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_InitBanks) "addl $4, %esp\n"
                   "ret")

uint32_t* XB_CDECL Stream_InitBanks(uint32_t* banks) {
    banks[1] = 0;
    banks[0] = 0;
    banks[3] = 0;
    banks[2] = 0;
    banks[4] = 0;
    banks[5] = 0;
    banks[6] = 0;
    banks[8] = 0;
    banks[7] = 0;
    banks[10] = 0;
    banks[9] = 0;
    uint32_t count = 0x21;
    char text[0x80];
    title_function<int XB_CDECL(char*, const char*, uint32_t)>(0x001ee901)(text, title_text(0x008325b8), count);
    const uint8_t* cvar = Cvar_Get(title_text(0x00838440), text, 0x10);  // num_banks
    if (cvar && word_at<uint32_t>(cvar, 0x20)) count = word_at<uint32_t>(cvar, 0x20);
    cvar = Cvar_Get(title_text(0x00833a4c), title_text(0x0082e098), 0);  // g_connectpaths
    if (!(word_at<float>(cvar, 0x1c) == 0.0f)) count = 0xa0;
    banks[4] = count;
    banks[5] = 0xc0000;
    banks[1] = 0;
    banks[0] = 0;
    banks[3] = 0;
    banks[2] = 0;
    for (uint32_t i = 0; i < banks[4]; ++i) {
        const uint32_t bit = 1u << (i & 0x1f);
        const int32_t word = int32_t(i) >> 5;
        banks[word] |= bit;
        banks[word + 2] |= bit;
    }
    const uint32_t size = banks[5] * banks[4];
    uint8_t* const base = size ? bank_alloc(size) : nullptr;
    const uint32_t total = banks[5] * banks[4];
    banks[6] = address_of(base);
    title_global<uint32_t>(0x00947b10) = address_of(base);
    title_global<uint32_t>(0x00947b14) = total;
    return banks;
}

namespace {

// SmallHeap_Init's description: a count and 16 {block size, ?, blocks per page} entries.
struct SmallHeapConfig {
    uint32_t count;
    uint32_t entries[16][3];
};

uint32_t small_heap_create(uint32_t size, uint32_t per_page) {
    SmallHeapConfig config;
    config.count = 0;
    for (auto& entry : config.entries) {
        entry[0] = 0;
        entry[1] = 4;
        entry[2] = 0;
    }
    config.entries[0][0] = size;
    config.entries[0][1] = 4;
    config.entries[0][2] = per_page;
    config.count = 1;
    const uint32_t heap = title_function<uint32_t XB_CDECL(uint32_t)>(0x00042c10)(0x3c);  // Z_Malloc
    if (!heap) return 0;
    return title_function<uint32_t XB_THISCALL(uint32_t, SmallHeapConfig*, uint32_t, uint32_t)>(0x00044770)(
        heap, &config, 1, 0);  // SmallHeap_Init
}

}  // namespace

void XB_CDECL Stream_InitSmallHeaps() {
    title_global<uint32_t>(0x0095f624) = small_heap_create(0xe4, 0xc);
    title_global<uint32_t>(0x0095f628) = small_heap_create(0x494, 0x10);  // stream_heapHeaderPool
}

uint32_t XB_STDCALL Stream_RemoveFreeBlocks(uint8_t* context) {
    uint32_t& count = word_at<uint32_t>(context, 0x8c);
    if (int32_t(count) <= 0) {
        const uint32_t result = count;
        count = 0;
        return result;
    }
    uint32_t result = 0;
    for (int32_t i = 0; i < int32_t(count); ++i) {
        const uint32_t heap = word_at<uint32_t>(context, 0x90 + uint32_t(i) * 4);
        int32_t& heaps = title_global<int32_t>(0x008ff5a8);
        for (int32_t j = 0; j < heaps; ++j) {
            if (title_global<uint32_t>(0x008ff4a8 + uint32_t(j) * 4) != heap) continue;
            const int32_t n = heaps;
            title_global<uint32_t>(0x008ff4a8 + uint32_t(j) * 4) = title_global<uint32_t>(0x008ff4a4 + uint32_t(n) * 4);
            heaps = n - 1;
            break;
        }
        title_function<void XB_CDECL(uint32_t)>(0x00042560)(heap);
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x00044910)(title_global<uint32_t>(0x0095f628),
                                                                         heap);  // SmallHeap_Free
        result = count;
    }
    count = 0;
    return result;
}

uint32_t XB_STDCALL Stream_FreeZoneMemory(uint8_t* record) {
    uint8_t* const context = global_context();
    if (!word_at<uint32_t>(record, 0x48)) word_at<uint32_t>(context, 0x18) = 0;
    uint8_t* const own = word_at<uint8_t*>(record, 0x78);
    if (own && word_at<uint32_t>(own, 0x44)) {
        const uint32_t block = word_at<uint32_t>(own, 0x44);
        const uint32_t id = word_at<uint32_t>(record, 0x74);
        if (id == 0xffffffff || !memcontext_free(id < 0x63 ? word_at<uint8_t*>(context, 0x38 + id * 4) : nullptr, block))
            z_free(block);
        word_at<uint32_t>(word_at<uint8_t*>(record, 0x78), 0x44) = 0;
    }
    Stream_RemoveFreeBlocks(record);
    uint32_t* const bits = &word_at<uint32_t>(record, 0x54);
    if (bits[0] || bits[1] || bits[2] || bits[3]) {
        const uint32_t result = StreamMem_MoveSlots(0, bits, title_global<uint32_t*>(0x0095f62c), -1);
        word_at<uint32_t>(record, 0xdc) = 3;
        return result;
    }
    uint32_t group = 0;
    for (uint32_t offset = 0; group < word_at<uint32_t>(word_at<uint8_t*>(record, 0x7c), 0xc); offset += 0x18) {
        uint8_t* const entry = word_at<uint8_t*>(word_at<uint8_t*>(record, 0x7c), 8) + offset;
        for (uint32_t i = 0; i < word_at<uint16_t>(entry, 0xc); ++i) {
            uint8_t* const item = word_at<uint8_t*>(entry, 0x10) + i * 0x1c;
            if (!(item[0xc] & 8)) continue;
            if (word_at<uint32_t>(item, 0x14)) z_free(word_at<uint32_t>(item, 0x14));
            word_at<uint32_t>(item, 0x14) = 0;
        }
        ++group;
    }
    word_at<uint32_t>(record, 0xdc) = 3;
    return group ? group : 2;
}

void XB_STDCALL Stream_ReleaseAnims(uint8_t* streams, uint32_t id) {
    streams[0x48] = 1;
    streams[0x30] = 1;
    streams[0x3c] = 1;
    uint8_t* const zone = word_at<uint8_t*>(streams, 0x5c + id * 4);
    if (zone) {
        // Map entries (count at +offset, 8-byte entries at +offset + 4, the value at +4), index past the count -> 0.
        auto value = [zone](uint32_t offset, uint32_t i) -> uint32_t& {
            return word_at<uint32_t>(word_at<uint8_t*>(zone, offset + 4), wrap_index(i, word_at<uint32_t>(zone, offset)) * 8 + 4);
        };
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0x90); ++i) {
            const uint32_t file = value(0x90, i);
            if (!file) continue;
            for (uint32_t data = word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{file}), 0x34); data;) {
                const uint32_t* const object = word_at<const uint32_t*>(streams, 0x2c);
                title_function<void XB_THISCALL(const uint32_t*, uint32_t)>(vtable_slot(object, 4))(object, data);
                title_function<void XB_CDECL(uint32_t)>(0x002c4ae0)(data);  // AnimData_Purge
                data = word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{data}), 4);
            }
            NAL_AnimFile_Release(file);
            value(0x90, i) = 0;
        }
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0xa0); ++i) value(0xa0, i) = 0;
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0xb0); ++i) {
            const uint32_t item = value(0xb0, i);
            if (!item) continue;
            NAL_Manager828_Release(item);
            value(0xb0, i) = 0;
        }
        for (uint32_t i = 0; i < word_at<uint32_t>(zone, 0xe0); ++i) {
            const uint32_t block = value(0xe0, i);
            if (block) free_zone_block(id, block);
            value(0xe0, i) = 0;
        }
    }
    streams[0x48] = 0;
    streams[0x30] = 0;
    streams[0x3c] = 0;
}

PLATFORM_ASM_ENTRY(Stream_ReleaseZoneResources_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Stream_ReleaseZoneResources) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL Stream_ReleaseZoneResources(const uint8_t* record) {
    MemContext_Push(global_context(), word_at<uint32_t>(record, 0x74));
    Stream_ReleaseMeshFiles(title_global<uint8_t*>(0x0095f630), word_at<uint32_t>(record, 0x74));
    Stream_ReleaseFonts(title_global<uint8_t*>(0x0095f630), 0, word_at<uint32_t>(record, 0x74));
    Stream_ReleaseTextures(word_at<uint32_t>(record, 0x74), title_global<uint8_t*>(0x0095f630));
    Stream_ReleaseAnims(title_global<uint8_t*>(0x0095f630), word_at<uint32_t>(record, 0x74));
    Stream_ReleaseZoneSkeletons(word_at<uint32_t>(record, 0x74), title_global<uint8_t*>(0x0095f630));
    Stream_ReleaseEffects(title_global<uint8_t*>(0x0095f630), 0, word_at<uint32_t>(record, 0x74));
    title_global<uint32_t>(title_global<uint32_t>(0x0095f630) + 0x5c + word_at<uint32_t>(record, 0x74) * 4) = 0;
    return MemContext_Pop(global_context());
}

uint32_t XB_REGS1 XB_CDECL Stream_GetLoadHandler(const char* extension) {
    static const uint32_t kHandlers[][2] = {
        {0x00000c07, 0x002d0d40},  // Cg
        {0x00000ce2, 0x0028ee20},  // Db
        {0x00000d42, 0x0028ee50},  // Ib
        {0x00018665, 0x002911c0},  // Bpe
        {0x000186f2, 0x0023a1b0},  // Btr
        {0x000194e6, 0x0028fc60},  // Fdf
        {0x000195e9, 0x0028e030},  // Fli
        {0x00019c07, 0x002caba0},  // Dcg
        {0x00019cf3, 0x00290e50},  // Dds
        {0x00019de7, 0x00299250},  // Dlg
        {0x0001a8ac, 0x00290e10},  // Ifl
        {0x0001c00e, 0x002ee680},  // Scn
        {0x0001c267, 0x002b7010},  // Spg
        {0x0001c2e2, 0x0028ee00},  // Stb
        {0x0001e422, 0x00291920},  // Zbb
        {0x0001edc2, 0x00290730},  // Xmb
        {0x00312ae2, 0x0028d120},  // Aitb
        {0x00315e22, 0x002aeb70},  // Atrb
        {0x0037cee8, 0x002906f0},  // Lpth
        {0x003ccee8, 0x00290df0},  // Zpth
        {0x003dba62, 0x00291300},  // Xmpb
        {0x06172a73, 0x0028ee80},  // Bmips
        {0x0673c1d0, 0x00290e30},  // Ddsmp
        {0x06f24544, 0x0028d0e0},  // Lgrid
        {0x07098805, 0x0028bea0},  // Space
        {0x073134cc, 0x003393f0},  // FE_LoadPanel
        {0xc05242f2, 0x00298570},  // Cfgstr
        {0xf61019ec, 0x00290920},  // mesh file
        {0xf611354d, 0x00290760},  // Xbanim
        {0xf6171a08, 0x00290920},  // mesh file
        {0xf61820cc, 0x002907f0},  // Xbskel
    };
    uint32_t hashed[8];
    title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(hashed, extension);  // HashStr_Init
    for (const auto& handler : kHandlers)
        if (handler[0] == hashed[0]) return handler[1];
    return 0;
}

// The handler runs where the original calls it (the entry ESP, its four arguments below: handler subtrees read stale
// stack), the C frame below a gap and EBP saved there (the handler's arguments overwrite the words below the entry).
PLATFORM_ASM_ENTRY(Stream_RunLoadHandler_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Stream_RunLoadHandler) "addl $32, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret $12")

uint32_t XB_CDECL Stream_RunLoadHandler(uint32_t entry, const uint8_t* record, char* name, uint8_t* data, uint32_t size,
                                        uint32_t zone, uint32_t esi, uint32_t edi) {
    MemContext_Push(global_context(), word_at<uint32_t>(record, 0x74));
    const char* p = name;
    while (*p) ++p;
    if (*p != '.') {
        while (p > name) {
            --p;
            if (*p == '.') break;
        }
    }
    const uint32_t handler = Stream_GetLoadHandler(p + 1);
    if (handler) {
        const TitleRegs regs{handler, address_of(name), address_of(data), size, esi, edi};
        const uint32_t args[4] = {address_of(name), address_of(data), size, zone};
        title_call_at_regs(entry, handler, &regs, 4, args);
    }
    return MemContext_Pop(global_context());
}

uint32_t XB_REGS1 XB_CDECL Stream_GetFileType(const char* name) {
    static const uint32_t kTypes[][2] = {
        {0x0083870c, 1},  // .ifl
        {0x00838704, 1},  // .dds
        {0x008386fc, 1},  // .ddsmp
        {0x008386f4, 1},  // .tm2
        {0x008386ec, 1},  // .tm2mp
        {0x008386e4, 1},  // .gct
        {0x008386dc, 1},  // .gctmp
        {0x008386d4, 1},  // .fdf
        {0x008386cc, 1},  // .pcmesh
        {0x008386c0, 1},  // .ps2mesh
        {0x008386b8, 1},  // .xbmesh
        {0x008386b0, 1},  // .gcmesh
        {0x008386a8, 10},  // .pccell
        {0x0083869c, 10},  // .ps2cell
        {0x00838694, 10},  // .xbcell
        {0x0083868c, 10},  // .gccell
        {0x00838680, 1},  // .pcmorph
        {0x00838674, 1},  // .ps2morph
        {0x00838668, 1},  // .xbmorph
        {0x0083865c, 1},  // .gcmorph
        {0x00838654, 1},  // .pcanim
        {0x00838648, 1},  // .ps2anim
        {0x00838640, 1},  // .xbanim
        {0x00838638, 1},  // .gcanim
        {0x0083862c, 1},  // .pcsanim
        {0x00838620, 1},  // .ps2sanim
        {0x00838614, 1},  // .xbsanim
        {0x00838608, 1},  // .gcsanim
        {0x00838600, 1},  // .pcskel
        {0x008385f4, 1},  // .ps2skel
        {0x008385ec, 1},  // .xbskel
        {0x008385e4, 1},  // .gcskel
        {0x008385e0, 0},  // .db
        {0x008385d8, 0},  // .dbb
        {0x008385d0, 0},  // .dlg
        {0x008385c8, 0},  // .dlgb
        {0x008385c0, 0},  // .btr
        {0x008385b8, 0},  // .btrb
        {0x008385b0, 0},  // .fli
        {0x008385ac, 0},  // .ib
        {0x008385a4, 0},  // .lgrid
        {0x0083859c, 0},  // .lgridb
        {0x00838594, 0},  // .bmips
        {0x0083858c, 0},  // .xmpb
        {0x00838584, 0},  // .xmpbb
        {0x0083857c, 0},  // .xmb
        {0x00838574, 0},  // .xmbb
        {0x0083856c, 0},  // .aitb
        {0x00838564, 0},  // .aitbb
        {0x0083855c, 1},  // .atrb
        {0x00838554, 0},  // .cfgstr
        {0x00838548, 0},  // .cfgstrb
        {0x00838540, 0},  // .zbb
        {0x00838538, 0},  // .zbbb
        {0x00838530, 10},  // .scn
        {0x00838528, 10},  // .scnb
        {0x00838520, 0},  // .space
        {0x00838518, 10},  // .bpe
        {0x00838510, 10},  // .bpeb
        {0x00838508, 1},  // .wbk
        {0x00838500, 0},  // .lpth
        {0x008384f8, 0},  // .zpth
        {0x008384f0, 10},  // .menus
        {0x008384e8, 10},  // .menusb
        {0x008384e0, 10},  // .panel
        {0x008384d8, 10},  // .spg
        {0x008384d0, 10},  // .spgb
        {0x008384c8, 0},  // .rel
        {0x008384c0, 0},  // .stb
        {0x008384b8, 0},  // .stbb
        {0x008384b0, 0},  // .ps2fmv
        {0x008384a8, 0},  // .xbfmv
        {0x008384a0, 0},  // .gcfmv
        {0x0083849c, 1},  // .cg
        {0x00838494, 1},  // .cgb
        {0x0083848c, 1},  // .dcg
        {0x00838484, 1},  // .dcgb
    };
    const char* p = name;
    while (*p) ++p;
    if (*p != '.') {
        while (p > name) {
            --p;
            if (*p == '.') break;
        }
    }
    for (const auto& type : kTypes)
        if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(p, title_text(type[0])))  // __stricmp
            return type[1];
    debug_break();
    return 0;
}

namespace {

bool bit_set(const uint32_t* bits, uint32_t id) { return (bits[int32_t(id) >> 5] & (1u << (id & 0x1f))) != 0; }

void set_bit(uint32_t* bits, uint32_t id) { bits[int32_t(id) >> 5] |= 1u << (id & 0x1f); }

void append_id(uint32_t* list, uint32_t id) {
    if (int32_t(list[0]) < 0x20) list[1 + list[0]++] = id;
}

// MemContext_AllocOrHeap16 (0x0001b960): zone id in EAX, size in ESI.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL MemContext_AllocOrHeap16_call(uint32_t id, uint32_t size);
PLATFORM_ASM_ENTRY(MemContext_AllocOrHeap16_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %eax\n"
                   "movl 12(%esp), %esi\n"
                   "movl $0x0001b960, %ecx\n"
                   "call *%ecx\n"
                   "popl %esi\n"
                   "ret")

}  // namespace

void XB_THISCALL Stream_CollectSearchZones_r(uint8_t* system, uint32_t id, uint32_t* out, uint32_t* seen) {
    if (id == 0xffffffff) return;
    append_id(out, id);
    set_bit(seen, id);
    if (!word_at<uint32_t>(system, 0x18)) return;
    uint8_t* const context = title_function<uint8_t* XB_THISCALL(uint8_t*, uint32_t)>(0x0028e550)(system, id);
    if (!context) return;
    if (word_at<uint32_t>(context, 0x44)) {
        for (uint32_t i = 0; i < 0x63; ++i) {
            if (!bit_set(word_at<const uint32_t*>(context, 0x44), i) || bit_set(seen, i)) continue;
            set_bit(seen, i);
            append_id(out, i);
        }
        return;
    }
    uint32_t local_seen[4] = {};
    uint32_t local[0x21];
    local[0] = 0;
    for (uint32_t i = 0; i < word_at<uint32_t>(context, 0x1c); ++i) {
        const uint8_t* const child = word_at<uint8_t**>(context, 0x20)[wrap_index(i, word_at<uint32_t>(context, 0x1c))];
        const uint32_t child_id = word_at<uint32_t>(child, 0x28);
        if (!bit_set(local_seen, child_id)) Stream_CollectSearchZones_r(system, child_id, local, local_seen);
    }
    const uint32_t zone = word_at<uint32_t>(context, 0x28);
    if (zone) {
        uint32_t* set = MemContext_AllocOrHeap16_call(zone, 0x10);
        if (set) {
            set[3] = 0;
            set[2] = 0;
            set[1] = 0;
            set[0] = 0;
        } else {
            set = nullptr;
        }
        word_at<uint32_t*>(context, 0x44) = set;
        volatile uint32_t* const cached = set;  // a failed allocation writes through null, as the original
        cached[0] = local_seen[0];
        cached[1] = local_seen[1];
        cached[2] = local_seen[2];
        cached[3] = local_seen[3];
    }
    for (int32_t i = 0; i < int32_t(local[0]); ++i) {
        const uint32_t child_id = local[1 + i];
        if (!bit_set(local_seen, child_id) || bit_set(seen, child_id)) continue;
        set_bit(seen, child_id);
        append_id(out, local[1 + i]);
    }
}

namespace {

// HashMap_Insert (0x000148b0): the map in ESI, stdcall (out, key).
extern "C" PLATFORM_HIDDEN void XB_CDECL HashMap_Insert_call(uint32_t map, void* out, const void* key);
PLATFORM_ASM_ENTRY(HashMap_Insert_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "movl $0x000148b0, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

}  // namespace

uint32_t XB_STDCALL Stream_ResolveXmpbSkeletons(uint8_t* list, uint32_t zone) {
    for (uint32_t i = 0; i < word_at<uint32_t>(list, 0x10); ++i) {
        uint8_t* const model = word_at<uint8_t**>(list, 0x14)[wrap_index(i, word_at<uint32_t>(list, 0x10))];
        if (word_at<const char*>(model, 0x34)) {
            uint32_t hashed[8];
            title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(hashed,
                                                                                   word_at<const char*>(model, 0x34));
            word_at<uint32_t>(model, 0x38) = Stream_FindSkel(0, title_global<uint32_t>(0x0095f630), zone, hashed, 0);
        }
        for (uint32_t j = 0; j < word_at<uint32_t>(model, 0x10); ++j) {
            const char* const name =
                word_at<const char*>(word_at<uint8_t*>(model, 0x14), wrap_index(j, word_at<uint32_t>(model, 0x10)) * 12);
            uint32_t hash = 0;
            if (name) {
                hash = title_function<uint32_t XB_CDECL(const char*)>(0x002f5470)(name);  // ScrIface_HashStringNoCase
                char text[0x20];
                int32_t length = 0;
                Q_strncpyLen(text, &length, 0x1f, name);
                text[0x1f] = static_cast<char>(length);
                struct {
                    uint32_t hash;
                    char text[0x20];
                } key = {};
                int32_t key_length = 0;
                title_function<void XB_CDECL(char*, int32_t*, int32_t, const char*, uint32_t)>(0x00044370)(  // FixedStr_Copy
                    key.text, &key_length, 0x1f, text, static_cast<uint8_t>(length));
                key.hash = hash;
                key.text[0x1f] = static_cast<char>(key_length);
                uint32_t inserted[2] = {};
                HashMap_Insert_call(0x0095fe00, inserted, &key);
            }
            word_at<uint32_t>(word_at<uint8_t*>(model, 0x14), wrap_index(j, word_at<uint32_t>(model, 0x10)) * 12 + 4) = hash;
            const uint32_t other = word_at<uint32_t*>(model, 0x24)[wrap_index(j, word_at<uint32_t>(model, 0x20))];
            if (!other) continue;
            uint32_t hashed[8];
            title_function<uint32_t* XB_THISCALL(uint32_t*, uint32_t)>(0x00011060)(hashed, other);  // HashStr_Init
            const uint32_t found = Stream_FindInZones(0, 0, hashed, title_global<uint32_t>(0x0095f630), zone, 0);
            word_at<uint32_t*>(model, 0x2c)[wrap_index(j, word_at<uint32_t>(model, 0x28))] = found;
        }
    }
    return word_at<uint32_t>(list, 0x10);
}

PLATFORM_ASM_ENTRY(StreamMem_Alloc_entry,
                   "leal 8(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(StreamMem_Alloc) "addl $12, %esp\n"
                   "ret $0x14")

uint32_t* XB_CDECL StreamMem_Alloc(uint32_t* memory, uint32_t* out, float* block) {
    float wanted = block[1];
    uint32_t taken[4] = {};
    StreamMem_CountFree(memory, block);
    if (wanted > block[1]) {
        debug_break();
        out[1] = 0;
        out[0] = 0;
        out[3] = 0;
        out[2] = 0;
        return out;
    }
    const float one = title_global<float>(0x0083eb20);
    if (wanted >= one) {
        uint32_t i = 0;
        do {
            if (i >= memory[4]) break;
            const uint32_t bit = 1u << (i & 0x1f);
            const int32_t word = int32_t(i) >> 5;
            if ((memory[word] & bit) && (memory[word + 2] & bit)) {
                memory[word] &= ~bit;
                taken[word] |= bit;
                memory[word + 2] &= ~bit;
                taken[word + 2] |= bit;
                wanted -= one;
            }
            ++i;
        } while (wanted >= one);
    }
    const bool rest = wanted > 0.0f;
    if (wanted > 0.0f) {
        const uint32_t count = memory[4];
        bool found = false;
        for (uint32_t i = 0; count && i < memory[4]; ++i) {
            const uint32_t bit = 1u << (i & 0x1f);
            const int32_t word = int32_t(i) >> 5;
            const bool first = (memory[word] & bit) != 0;
            const bool second = (memory[word + 2] & bit) != 0;
            if (first == second) continue;
            if (first) {
                memory[word] &= ~bit;
                taken[word] |= bit;
            } else {
                memory[word + 2] &= ~bit;
                taken[word + 2] |= bit;
            }
            found = true;
            break;
        }
        if (!found && rest && count) {
            for (uint32_t i = 0; i < memory[4]; ++i) {
                const uint32_t bit = 1u << (i & 0x1f);
                const int32_t word = int32_t(i) >> 5;
                if (!(memory[word] & bit)) continue;
                memory[word] &= ~bit;
                taken[word] |= bit;
                break;
            }
        }
    }
    for (int k = 0; k < 4; ++k) memory[7 + k] = taken[k];
    for (int k = 0; k < 4; ++k) out[k] = taken[k];
    return out;
}

void XB_THISCALL Zone_RemoveActive(uint8_t* self, uint32_t zone) {
    bool removed = false;
    for (int32_t i = 0; i < word_at<int32_t>(self, 0xc); ++i) {
        if (word_at<uint32_t>(self, 0x19c + uint32_t(i) * 4) != zone) continue;
        const int32_t count = word_at<int32_t>(self, 0xc);
        if (i < count - 1) {
            word_at<uint32_t>(self, 0x10 + uint32_t(i) * 4) = word_at<uint32_t>(self, 0xc + uint32_t(count) * 4);
            word_at<uint32_t>(self, 0x19c + uint32_t(i) * 4) =
                word_at<uint32_t>(self, 0x198 + word_at<uint32_t>(self, 0xc) * 4);
            --i;
        }
        --word_at<int32_t>(self, 0xc);
        removed = true;
    }
    if (!removed) return;
    for (uint32_t slot = 0x00985384; slot < 0x00985404; slot += 4) {  // g_actors
        uint8_t* const actor = title_global<uint8_t*>(slot);
        if (actor && word_at<uint32_t>(actor, 0)) Actor_ResetSentientInfo(actor);
    }
    uint8_t* const box = word_at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
    if (box) {
        for (uint32_t k = 0; k < 4; ++k) word_at<uint32_t>(box, 0x430 + k * 4) = 0x7f7fffff;  // FLT_MAX
        for (uint32_t k = 0; k < 4; ++k)
            word_at<uint32_t>(box, 0x440 + k * 4) = word_at<uint32_t>(box, 0x430 + k * 4) ^ 0x80000000u;
    }
    title_function<void XB_CDECL()>(0x0024f5f0)();  // Vehicle_ResetCachedBounds
    for (uint32_t record = 0x0094d95c; record < 0x0095029c; record += 0x84) title_global<uint32_t>(record) = 0;
}

namespace {

uint32_t* raw_file_slot(uint8_t* zone, uint32_t hash) {
    const uint32_t* const index = UintMap_Find(&word_at<uint32_t>(zone, 0xd8), &hash);
    if (!index || *index == 0xffffffff) return nullptr;
    return &word_at<uint32_t>(word_at<uint8_t*>(zone, 0xe4), wrap_index(*index, word_at<uint32_t>(zone, 0xe0)) * 8 + 4);
}

}  // namespace

uint8_t XB_REGS3 XB_STDCALL Stream_FindZoneRawFile(const char* name, uint8_t* streams, uint32_t id, uint32_t* data,
                                                   uint32_t* size) {
    if (id == 0xffffffff) return 0;
    uint8_t* const zone = word_at<uint8_t*>(streams, 0x5c + id * 4);
    if (!zone) return 0;
    uint32_t hash = 0;
    if (name) hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x0001d830)(name);  // HashStr_Hash
    const uint32_t* const slot = raw_file_slot(zone, hash);
    if (!slot) return 0;
    *data = word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{*slot}), 0);
    *size = word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{*slot}), 4);
    return 1;
}

PLATFORM_ASM_ENTRY(Stream_AddZoneRawFile_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_AddZoneRawFile) "addl $20, %esp\n"
                   "ret $12")

uint32_t* XB_CDECL Stream_AddZoneRawFile(const char* name, uint32_t id, uint8_t* streams, uint32_t data, uint32_t size) {
    char text[0x80];
    int32_t length = 0;
    Q_strncpyLen(text, &length, 0x7f, name);
    text[0x7f] = static_cast<char>(length);
    char base[0x80];
    FixedStr_BaseName(text, base, 1);
    std::memcpy(text, base, 0x80);
    uint32_t* record = nullptr;
    if (id != 0xffffffff)
        record = reinterpret_cast<uint32_t*>(uintptr_t{memcontext_alloc(zone_context(int32_t(id)), 0x10, 8)});
    if (!record) record = title_function<uint32_t* XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x10, 8);  // Heap_AllocAligned
    record[1] = size;
    record[0] = data;
    uint8_t* const zone = word_at<uint8_t*>(streams, 0x5c + id * 4);
    const uint32_t hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x0001d830)(text);  // HashStr_Hash
    uint32_t* slot = raw_file_slot(zone, hash);
    volatile uint32_t* const target = slot;  // a name not in the map writes through null, as the original
    *target = address_of(record);
    return slot;
}

PLATFORM_ASM_ENTRY(Stream_SetupZoneHeaps_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_SetupZoneHeaps) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Stream_SetupZoneHeaps(uint8_t* record) {
    const uint32_t* const bits = &word_at<uint32_t>(record, 0x54);
    if (!bits[0] && !bits[1] && !bits[2] && !bits[3]) return;
    const uint8_t* const group =
        word_at<uint8_t*>(word_at<uint8_t*>(record, 0x7c), 8) + word_at<uint32_t>(record, 0x80) * 0x18;
    uint32_t previous = 0;
    for (int32_t n = int32_t(word_at<uint16_t>(group, 0xc)) - 1; n >= 0; --n) {
        const uint8_t* const item = word_at<uint8_t*>(group, 0x10) + uint32_t(n) * 0x1c;
        const uint32_t used = (word_at<uint32_t>(item, 4) + 0x7fff) & 0xffff8000u;
        if (!(item[0xc] & 1) && !((word_at<uint32_t>(item, 0xc) >> 1) & 1) && word_at<uint32_t>(item, 0x14) &&
            word_at<uint32_t>(item, 8)) {
            const uint32_t heap =
                Stream_AddFreeBlock(used + word_at<uint32_t>(item, 0x14), record, word_at<uint32_t>(item, 8) - used);
            if (heap) {
                if (previous) word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{previous}), 0x490) = heap;
                previous = heap;
            }
        }
        if (word_at<uint32_t>(record, 0x8c) == 0xc) break;
    }
    const uint8_t* const context = title_function<uint8_t* XB_THISCALL(uint8_t*, uint32_t)>(0x0028e550)(
        global_context(), word_at<uint32_t>(record, 0x74));  // MemContext_FindById
    // The children's contexts (+0x20 list, count +0x1c, zone id +0x28): null outside 0..0x62, read as the original.
    auto child_context = [context](uint32_t i) {
        const uint8_t* const child = word_at<uint8_t**>(context, 0x20)[wrap_index(i, word_at<uint32_t>(context, 0x1c))];
        return zone_context(word_at<int32_t>(child, 0x28));
    };
    for (uint32_t i = 0; i < word_at<uint32_t>(context, 0x1c); ++i) {
        uint8_t* const child = child_context(i);
        if (!word_at<uint32_t>(child, 0x8c)) continue;
        const int32_t count = word_at<int32_t>(record, 0xc0);
        if (count >= 4) continue;
        word_at<uint8_t*>(record, 0xc4 + uint32_t(count) * 4) = child;
        ++word_at<int32_t>(record, 0xc0);
    }
    if (word_at<uint32_t>(record, 0x48) != 2) return;
    int32_t heaps = title_global<int32_t>(0x008ff5a8);
    // Appends a context's heaps (+0x90, count +0x8c) to the heap list.
    auto append_heaps = [&heaps](const uint8_t* owner) {
        for (int32_t k = 0; k < word_at<int32_t>(owner, 0x8c); ++k)
            title_global<uint32_t>(0x008ff4a8 + uint32_t(heaps++) * 4) = word_at<uint32_t>(owner, 0x90 + uint32_t(k) * 4);
    };
    append_heaps(record);
    for (uint32_t i = 0; i < word_at<uint32_t>(context, 0x1c); ++i) append_heaps(child_context(i));
    title_global<int32_t>(0x008ff5a8) = heaps;
}

PLATFORM_ASM_ENTRY(SoundBank_AddPart_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SoundBank_AddPart) "addl $20, %esp\n"
                   "ret $16")

void XB_CDECL SoundBank_AddPart(const uint32_t* hashed, uint8_t* table, uint32_t index, uint32_t zone, uint32_t data) {
    const int32_t count = word_at<int32_t>(table, 8);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const bank = table + 0xc + uint32_t(i) * 0x84;
        bool same = true;
        for (uint32_t k = 0; k < 8; ++k) {
            if (word_at<uint32_t>(bank, k * 4) != hashed[k]) {
                same = false;
                break;
            }
        }
        if (!same) continue;
        word_at<uint32_t>(bank, 0x3c + index * 4) = data;
        return;
    }
    uint32_t bank[0x21];
    for (uint32_t k = 0; k < 8; ++k) bank[k] = hashed[k];
    bank[8] = zone;
    for (uint32_t k = 9; k < 0x15; ++k) bank[k] = 0;
    for (uint32_t k = 0x15; k < 0x21; ++k) bank[k] = 0xffffffff;
    bank[0xf + index] = data;
    const int32_t n = word_at<int32_t>(table, 8);
    if (n >= 0x10) return;
    word_at<int32_t>(table, 8) = n + 1;
    title_function<void XB_REGS3 XB_CDECL(uint8_t*, uint32_t, const uint32_t*)>(0x0001f740)(  // copies 0x84 bytes
        table + 0xc + uint32_t(n) * 0x84, 0, bank);
}

// --- Stream requests and the update loop ---

namespace {

// A title function taking its argument in EBX (Pak_FindNextZoneToLoad).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL call_with_ebx(uint32_t function, uint32_t ebx);
PLATFORM_ASM_ENTRY(call_with_ebx,
                   "pushl %ebx\n"
                   "movl 12(%esp), %ebx\n"
                   "movl 8(%esp), %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

uint8_t* find_next_zone_to_load(uint8_t* system) {
    return reinterpret_cast<uint8_t*>(uintptr_t{call_with_ebx(0x0028e460, address_of(system))});  // Pak_FindNextZoneToLoad
}

// Stream_QueueLoad (0x002920c0): EAX and EDI (system), the name word and a 16-byte block on the stack (ret 0x14).
uint32_t queue_load(uint8_t* system, uint32_t eax, uint32_t name, const uint32_t* block, uint32_t esi) {
    const TitleRegs regs{eax, 0, 0, 0, esi, address_of(system)};
    const uint32_t args[5] = {name, block[0], block[1], block[2], block[3]};
    return cg_call_regs(0x002920c0, &regs, args, 5);
}

uint32_t release_cell_meshes(uint32_t id) {
    return title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x00290fe0)(  // Pak_ReleaseCellMeshesForZone
        title_global<uint32_t>(0x0095f640), id);
}

void cancel_request(uint8_t* record) {
    title_function<void XB_STDCALL(uint8_t*)>(0x0028f2f0)(record);  // StreamRequest_Cancel
}

void update_load(uint8_t* record) { title_function<void XB_STDCALL(uint8_t*)>(0x002926c0)(record); }  // Stream_UpdateLoad

uint8_t* record_slot(uint8_t* system, uint32_t id) { return word_at<uint8_t*>(system, 0x38 + id * 4); }

}  // namespace

void XB_REGS1 XB_CDECL Stream_Update(uint8_t* system) {
    title_global<uint8_t>(0x00902b76) = 1;
    title_function<void XB_REGS2 XB_CDECL(uint32_t, uint8_t*)>(0x0028e180)(0, system);  // Pak_UpdateLoadProgress
    uint8_t* node = word_at<uint8_t*>(system, 0x368);
    uint8_t* next = node ? word_at<uint8_t*>(node, 0) : nullptr;
    if (node != system + 0x36c && next) {
        for (;;) {
            const uint32_t state = word_at<uint32_t>(node, 0xdc);
            if (state == 1)
                update_load(node);
            else if (state == 2)
                Stream_UpdateUnload(node);
            if (!next) break;
            node = next;
            next = word_at<uint8_t*>(next, 0);
            if (!next) break;
        }
    }
    switch (word_at<uint32_t>(system, 0x10)) {
    case 0:
        Stream_UpdateZoneSwap(system);
        break;
    case 1:
        Stream_RetireRequest(system);
        break;
    case 2:
        Stream_ScheduleZones(system);
        break;
    default:
        debug_break();
        break;
    }
}

void XB_REGS1 XB_CDECL Stream_ScheduleZones(uint8_t* system) {
    ++title_global<uint32_t>(0x008d9d38);
    uint8_t* const next = find_next_zone_to_load(system);
    uint8_t* const unload = title_function<uint8_t* XB_REGS1 XB_CDECL(uint8_t*)>(0x0028e3e0)(system);  // Pak_FindZoneToUnload
    const float max = title_global<float>(0x0082eed0);  // FLT_MAX
    const float unload_priority = unload ? Zone_UpdatePriority(unload) : max;
    const float next_priority = next ? Zone_UpdatePriority(next) : max;
    bool forced = false;
    if (unload && unload_priority == max) {
        forced = true;
    } else {
        word_at<uint8_t*>(system, 0x358) = next;
        if (next_priority == max) {
            system[0x14] = 1;
            return;
        }
        float free[4];
        StreamMem_CountFree(title_global<const uint32_t*>(0x0095f62c), free);
        if (!(word_at<float>(next, 0x10) > free[1])) {
            word_at<uint8_t*>(system, 0x354) = next;
            const uint32_t block[4] = {word_at<uint32_t>(next, 0xc), word_at<uint32_t>(next, 0x10),
                                       word_at<uint32_t>(next, 0x14), word_at<uint32_t>(next, 0x18)};
            word_at<uint32_t>(next, 0x28) =
                queue_load(system, word_at<uint32_t>(next, 8), word_at<uint32_t>(next, 0), block, address_of(next));
            word_at<uint8_t*>(system, 0x24) = next;
            system[0x14] = 0;
            return;
        }
    }
    if (!unload || (!forced && !(unload_priority > next_priority))) {
        system[0x14] = 1;
        return;
    }
    system[0x14] = 0;
    word_at<uint8_t*>(system, 0x354) = unload;
    Stream_BeginZoneUnload(word_at<uint32_t>(unload, 0x28), system);
    word_at<uint32_t>(system, 0x20) = word_at<uint32_t>(unload, 0x28);
    word_at<uint8_t*>(system, 0x24) = unload;
}

void XB_REGS1 XB_CDECL Stream_UpdateZoneSwap(uint8_t* system) {
    bool swap = false;
    uint8_t* next = nullptr;
    if (word_at<uint32_t>(system, 0x18) && word_at<int32_t>(system, 0x364) > 2) {
        next = find_next_zone_to_load(system);
        const float best = next ? Zone_UpdatePriority(next) : title_global<float>(0x0082eed0);  // FLT_MAX
        uint8_t* const current = word_at<uint8_t*>(system, 0x24) ? word_at<uint8_t*>(system, 0x24)
                                                                 : word_at<uint8_t*>(system, 0x354);
        const float priority = Zone_UpdatePriority(current);
        swap = priority > best;
        if (priority == 0.0f) swap = false;
        if (next == word_at<uint8_t*>(system, 0x354)) swap = false;
    }
    const uint32_t id = word_at<uint32_t>(system, 0x20);
    uint8_t* const record = record_slot(system, id);
    const bool loaded = id != 0xffffffff && record && word_at<uint32_t>(record, 0xdc) == 0;
    if (!loaded && (!swap || word_at<uint32_t>(record, 0xe0) != 2)) return;
    if (word_at<uint8_t*>(system, 0x354)) Zone_AddRef_r(0, 0, address_of(system), word_at<uint8_t*>(system, 0x354));
    if (swap) {
        Stream_BeginZoneUnload(word_at<uint32_t>(system, 0x20), system);
        return;
    }
    const uint32_t slot = word_at<uint32_t>(system, 0x20);
    word_at<uint32_t>(system, 0x354) = 0;
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x00291090)(  // Pak_LoadCellMeshesForZone
        title_global<uint32_t>(0x0095f640), slot);
    word_at<uint32_t>(system, 0x10) = 2;
    word_at<uint32_t>(system, 0x20) = 0xffffffff;
    word_at<uint32_t>(system, 0x24) = 0;
}

PLATFORM_ASM_ENTRY(Stream_RetireRequest_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Stream_RetireRequest) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Stream_RetireRequest(uint8_t* system) {
    const uint32_t id = word_at<uint32_t>(system, 0x20);
    if (id != 0xffffffff && record_slot(system, id) && word_at<uint32_t>(record_slot(system, id), 0xdc) != 3) return;
    uint8_t* const record = record_slot(system, id);
    uint8_t* const before = word_at<uint8_t*>(record, 0);
    word_at<uint32_t>(before, 4) = word_at<uint32_t>(record, 4);
    word_at<uint8_t*>(word_at<uint8_t*>(record, 4), 0) = word_at<uint8_t*>(record, 0);
    --word_at<int32_t>(system, 0x364);
    if (word_at<uint8_t*>(system, 0x354)) {
        Zone_Release_r(0, 0, address_of(system), word_at<uint8_t*>(system, 0x354));
        word_at<uint32_t>(word_at<uint8_t*>(system, 0x354), 0x28) = 0xffffffff;
        word_at<uint32_t>(system, 0x354) = 0;
    }
    const uint32_t kind = word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{ZoneRecord_GetContext(record)}), 0);
    if (kind == 0)
        word_at<uint32_t>(system, 0x2c) = 0xffffffff;
    else if (kind == 7)
        word_at<uint32_t>(system, 0x28) = 0xffffffff;
    else if (kind == 2)
        word_at<uint32_t>(system, 0x34) = 0xffffffff;
    word_at<uint32_t>(system, 0x38 + word_at<uint32_t>(system, 0x20) * 4) = 0;
    word_at<uint32_t>(system, 0x1c4 + word_at<uint32_t>(system, 0x20) * 4) = 0;
    word_at<uint32_t>(system, 0x10) = 2;
    word_at<uint32_t>(system, 0x20) = 0xffffffff;
    word_at<uint32_t>(system, 0x24) = 0;
    StreamRequest_Destroy(record);
    title_function<void XB_THISCALL(uint32_t, uint8_t*)>(0x00044910)(title_global<uint32_t>(0x0095f624),
                                                                    record);  // SmallHeap_Free
}

PLATFORM_ASM_ENTRY(Stream_BeginZoneUnload_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_BeginZoneUnload) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Stream_BeginZoneUnload(uint32_t id, uint8_t* system) {
    word_at<uint32_t>(system, 0x20) = id;
    word_at<uint32_t>(system, 0x10) = 1;
    uint8_t* const record = record_slot(system, id);
    word_at<uint32_t>(system, 0x354) = ZoneRecord_GetContext(record);
    if (word_at<uint32_t>(record, 0x48) == 6) --word_at<int32_t>(system, 0x578);
    if (word_at<uint32_t>(record, 0xdc) == 1) cancel_request(record);
    const uint32_t zone = word_at<uint32_t>(record, 0x74);
    word_at<uint32_t>(record, 0xe0) = 4;
    word_at<uint32_t>(record, 0xdc) = 2;
    return release_cell_meshes(zone);
}

void XB_REGS1 XB_CDECL Stream_UpdateUnload(uint8_t* record) {
    switch (word_at<uint32_t>(record, 0xe0)) {
    case 4:
        Stream_UnloadNotifyManagers(record);
        word_at<uint32_t>(record, 0xe0) = 5;
        break;
    case 5:
        Stream_ReleaseZoneResources(record);
        word_at<uint32_t>(record, 0xe0) = 6;
        break;
    case 6:
        Stream_FreeZoneMemory(record);
        title_function<void XB_CDECL(uint32_t)>(0x002fb420)(  // Scr_NotifyUnloaded
            word_at<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{ZoneRecord_GetContext(record)}), 4));
        break;
    default:
        break;
    }
}

void XB_REGS1 XB_CDECL Stream_UnloadZoneNow(uint8_t* record) {
    if (word_at<uint32_t>(record, 0xdc) == 1) cancel_request(record);
    const uint32_t zone = word_at<uint32_t>(record, 0x74);
    word_at<uint32_t>(record, 0xe0) = 4;
    word_at<uint32_t>(record, 0xdc) = 2;
    release_cell_meshes(zone);
    Stream_UnloadNotifyManagers(record);
    Stream_ReleaseZoneResources(record);
    Stream_FreeZoneMemory(record);
}

void XB_REGS1 XB_CDECL StreamRequest_Destroy(uint8_t* record) {
    const uint32_t state = word_at<uint32_t>(record, 0xdc);
    if (state == 0)
        Stream_UnloadZoneNow(record);
    else if (state == 1 || state == 2)
        debug_break();
    const uint32_t buffer = word_at<uint32_t>(record, 0x84);
    if (buffer != title_global<uint32_t>(0x0095f608)) {
        z_free(buffer);
        word_at<uint32_t>(record, 0x84) = 0;
        word_at<uint32_t>(record, 0x7c) = 0;
    }
    const uint32_t count = word_at<uint32_t>(record, 0x8c);
    for (uint32_t i = 0; i < count; ++i)
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x00044910)(  // SmallHeap_Free
            title_global<uint32_t>(0x0095f628), word_at<uint32_t>(record, 0x90 + i * 4));
}

uint32_t XB_REGS1 XB_STDCALL Stream_LoadSync(uint8_t* system, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                             uint32_t a5, uint32_t a6) {
    const uint32_t block[4] = {a3, a4, a5, a6};
    const uint32_t id = queue_load(system, a2, a1, block, 0);
    if (id == 0xffffffff) return id;
    do {
        Stream_Update(system);
    } while (!word_at<uint32_t>(system, 0x10));
    return id;
}

PLATFORM_ASM_ENTRY(Stream_WaitForLoad_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Stream_WaitForLoad) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Stream_WaitForLoad(uint8_t* system, uint32_t id) {
    Stream_BeginZoneUnload(id, system);
    do {
        Stream_Update(system);
    } while (word_at<uint32_t>(system, 0x10) == 1);
    if (record_slot(system, id)) return;
    uint8_t* node = word_at<uint8_t*>(system, 0x368);
    uint8_t* next = node ? word_at<uint8_t*>(node, 0) : nullptr;
    if (node == system + 0x36c || !next) return;
    for (;;) {
        if (word_at<uint32_t>(node, 0x74) == id) {
            debug_break();
            return;
        }
        if (!next) return;
        node = next;
        next = word_at<uint8_t*>(next, 0);
        if (!next) return;
    }
}

uint32_t XB_THISCALL Stream_StartRequest(uint8_t* system, uint8_t* zone) {
    const uint32_t id = word_at<uint32_t>(zone, 0x28);
    if (id != 0xffffffff && record_slot(system, id) && word_at<uint32_t>(record_slot(system, id), 0xdc) == 0) return id;
    const uint32_t frame = title_global<uint32_t>(0x008d9d38) + 1;
    word_at<float>(zone, 0x34) = 0.0f;
    title_global<uint32_t>(0x008d9d38) = frame;
    for (uint8_t* child = Zone_FindUnloadedChild(zone, system); child; child = Zone_FindUnloadedChild(zone, system))
        Stream_StartRequest(system, child);
    word_at<uint8_t*>(system, 0x354) = zone;
    const uint32_t block[4] = {word_at<uint32_t>(zone, 0xc), word_at<uint32_t>(zone, 0x10), word_at<uint32_t>(zone, 0x14),
                               word_at<uint32_t>(zone, 0x18)};
    const uint32_t queued = queue_load(system, word_at<uint32_t>(zone, 8), word_at<uint32_t>(zone, 0), block,
                                       address_of(zone));
    word_at<uint32_t>(zone, 0x28) = queued;
    if (queued != 0xffffffff) {
        do {
            Stream_Update(system);
        } while (!word_at<uint32_t>(system, 0x10));
    }
    return word_at<uint32_t>(zone, 0x28);
}

PLATFORM_ASM_ENTRY(StreamRequest_Init_entry,
                   "leal 12(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(StreamRequest_Init) "addl $20, %esp\n"
                   "ret $0x18")

uint8_t* XB_CDECL StreamRequest_Init(uint8_t* record, const char* name, uint32_t pak, uint32_t id,
                                     const uint32_t* block) {
    int32_t length = 0;
    word_at<uint32_t>(record, 0) = 0;
    word_at<uint32_t>(record, 4) = 0;
    char* const text = reinterpret_cast<char*>(record + 8);
    Q_strncpyLen(text, &length, 0x3f, name);
    text[0x3f] = static_cast<char>(length);
    word_at<uint32_t>(record, 0x48) = pak;
    word_at<uint32_t>(record, 0x4c) = 0;
    word_at<uint32_t>(record, 0x50) = 0xffffffff;
    for (uint32_t offset = 0x54; offset <= 0x70; offset += 4) word_at<uint32_t>(record, offset) = 0;
    word_at<uint32_t>(record, 0x80) = 0xffffffff;
    word_at<uint32_t>(record, 0x88) = 0xffffffff;
    word_at<uint32_t>(record, 0x74) = id;
    word_at<uint32_t>(record, 0x78) = 0;
    word_at<uint32_t>(record, 0x7c) = 0;
    word_at<uint32_t>(record, 0x84) = 0;
    word_at<uint32_t>(record, 0x8c) = 0;
    word_at<uint32_t>(record, 0xc0) = 0;
    word_at<uint32_t>(record, 0xd4) = 0xffffffff;
    word_at<uint32_t>(record, 0xd8) = 0;
    word_at<uint32_t>(record, 0xdc) = 1;
    word_at<uint32_t>(record, 0xe0) = 3;
    title_global<float>(0x0095f618) = 0.0f;
    title_global<uint32_t>(0x0095f61c) = 0;
    title_global<uint32_t>(0x0095f738) = 0;
    title_global<uint32_t>(0x0095f73c) = 0;
    title_global<float>(0x0095f740) = 0.0f;
    title_global<float>(0x0095f744) = 0.0f;
    uint32_t copy[4] = {block[0], block[1], block[2], block[3]};
    StreamRequest_Start(record, copy);
    return record;
}

PLATFORM_ASM_ENTRY(StreamRequest_Start_entry,
                   "leal 4(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(StreamRequest_Start) "addl $8, %esp\n"
                   "ret $0x10")

uint32_t XB_CDECL StreamRequest_Start(uint8_t* record, uint32_t* block) {
    float count;
    std::memcpy(&count, &block[1], 4);
    const float rounded = static_cast<float>(static_cast<int32_t>(count + title_global<float>(0x0083eb24)));  // + 0.5
    if (rounded > title_global<float>(0x0083eafc)) {  // 0
        float copy[4];
        std::memcpy(copy, block, 16);
        const uint32_t* const slots = StreamMem_Alloc(title_global<uint32_t*>(0x0095f62c), block, copy);
        for (uint32_t k = 0; k < 4; ++k) word_at<uint32_t>(record, 0x54 + k * 4) = slots[k];
    }
    word_at<uint32_t>(record, 0xdc) = 1;
    word_at<uint32_t>(record, 0xe0) = 0;
    return StreamRequest_OpenFile(record);
}

PLATFORM_ASM_ENTRY(StreamRequest_OpenFile_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(StreamRequest_OpenFile) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL StreamRequest_OpenFile(uint8_t* record) {
    word_at<uint32_t>(record, 0x84) = title_global<uint32_t>(0x0095f608);
    title_global<uint8_t>(0x0095f610) = 1;
    const int32_t file = title_function<int32_t XB_CDECL(uint32_t, const char*, uint32_t*)>(0x000c9d60)(  // File_OpenReadEx
        title_global<uint32_t>(0x0095f6f4), reinterpret_cast<const char*>(record + 8), &word_at<uint32_t>(record, 0x4c));
    word_at<int32_t>(record, 0x50) = file;
    if (file == -1) {
        word_at<uint32_t>(record, 0x84) = 0;
        word_at<uint32_t>(record, 0xdc) = 0;
        title_global<uint8_t>(0x0095f610) = 0;
        return uint32_t(file);
    }
    const uint32_t request = title_function<uint32_t XB_CDECL(int32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
        0x000cae70)(file, 0, word_at<uint32_t>(record, 0x84), title_global<uint32_t>(0x0095f60c),
                    0x00291940);  // File_QueueRequestEx
    word_at<uint32_t>(record, 0x88) = request;
    const uint32_t result =
        title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x000c9df0)(request, 0);  // File_SetRequestPriority
    title_global<uint32_t>(0x0095f738) = 0;
    title_global<float>(0x0095f740) = 0.0f;
    word_at<uint32_t>(record, 0xe0) = 0;
    return result;
}


// --- Streaming module push, third part ---

namespace {

void file_pump() { title_function<void XB_CDECL()>(0x000ca690)(); }  // File_PumpRequests

int32_t file_status(int32_t request) {
    return title_function<int32_t XB_CDECL(int32_t)>(0x000c9db0)(request);  // File_GetRequestStatus
}

uint8_t* load_progress() { return &title_global<uint8_t>(0x0095f738); }

// The request's current group of its zone image (+0x7c groups +8, index +0x80).
uint8_t* current_group(const uint8_t* record) {
    return word_at<uint8_t*>(word_at<uint8_t*>(record, 0x7c), 8) + word_at<uint32_t>(record, 0x80) * 0x18;
}

uint8_t* group_item(const uint8_t* group, uint32_t index) { return word_at<uint8_t*>(group, 0x10) + index * 0x1c; }

// Slot bit arrays (two words each, then the second pair): any slot set.
bool has_slots(const uint8_t* bits) {
    for (uint32_t i = 0; i < 2; ++i)
        if (word_at<uint32_t>(bits, i * 4)) return true;
    for (uint32_t i = 0; i < 2; ++i)
        if (word_at<uint32_t>(bits, 8 + i * 4)) return true;
    return false;
}

// The image moved out of the shared read buffer into its own Heap_Alloc16 block (+0x84 = +0x7c = it).
uint8_t* keep_image(uint8_t* record) {
    uint8_t* const copy = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c30)(  // Heap_Alloc16
        word_at<uint32_t>(word_at<uint8_t*>(record, 0x7c), 0x10));
    Stream_UnrelocateZone(word_at<uint8_t*>(record, 0x7c));
    const uint8_t* const image = word_at<uint8_t*>(record, 0x7c);
    platform_rep_movs(copy, image, word_at<uint32_t>(image, 0x10));
    return copy;
}

// Owned item buffers (flag 8, and `mask` bits) of the current group freed and cleared.
void free_item_buffers(uint8_t* record, uint32_t mask) {
    const uint8_t* const group = current_group(record);
    for (int32_t i = 0; i < int32_t(word_at<uint16_t>(group, 0xc)); ++i) {
        uint8_t* const item = group_item(group, uint32_t(i));
        const uint32_t flags = word_at<uint32_t>(item, 0xc);
        if (!(flags & 8) || (flags & mask) != mask) continue;
        if (word_at<uint32_t>(item, 0x14)) z_free(word_at<uint32_t>(item, 0x14));
        word_at<uint32_t>(item, 0x14) = 0;
        word_at<uint32_t>(item, 0x18) = 0;
    }
}

// The pak's zone (+0x18: string map +8, count +0x10, zones +0x14) of a name, 0 without a pak or match.
uint32_t pak_zone_of(const char* name) {
    const uint8_t* const pak = word_at<const uint8_t*>(global_context(), 0x18);
    if (!pak) return 0;
    const uint32_t* const found = Stream_StringMapFind_call(pak + 8, &name);
    if (!found) return 0;
    return word_at<const uint32_t*>(pak, 0x14)[wrap_index(*found, word_at<uint32_t>(pak, 0x10))];
}

}  // namespace

void XB_STDCALL StreamRequest_Cancel(uint8_t* record) {
    file_pump();
    const int32_t file = word_at<int32_t>(record, 0x50);
    if (file != -1) {
        title_function<void XB_CDECL(int32_t)>(0x000cac20)(file);  // File_AbortRequestsForFile
        word_at<int32_t>(record, 0x88) = -1;
    }
    if (word_at<uint8_t*>(record, 0x7c)) {
        for (int32_t i = 0; i < int32_t(word_at<uint16_t>(current_group(record), 0xc)); ++i) {
            uint8_t* const item = group_item(current_group(record), uint32_t(i));
            if (word_at<int32_t>(item, 0x10) != -1) word_at<int32_t>(item, 0x10) = -1;
        }
    }
    if (has_slots(record + 0x64))
        StreamMem_MoveSlots(0, reinterpret_cast<uint32_t*>(record + 0x64), title_global<uint32_t*>(0x0095f62c), -1);
    free_item_buffers(record, 0);
    uint8_t* const copy = keep_image(record);
    word_at<uint8_t*>(record, 0x84) = copy;
    word_at<uint8_t*>(record, 0x7c) = copy;
    Stream_RelocateZone(0, copy, 1);
    title_global<uint8_t>(0x0095f610) = 0;
    file_pump();
    title_function<int32_t XB_CDECL(int32_t)>(0x000cac90)(word_at<int32_t>(record, 0x50));  // File_Close
    word_at<int32_t>(record, 0x50) = -1;
    word_at<uint32_t>(record, 0xdc) = 0;
}

PLATFORM_ASM_ENTRY(Stream_PollChunkReads_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Stream_PollChunkReads) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Stream_PollChunkReads(uint8_t* record) {
    file_pump();
    int32_t pending = 0;
    for (int32_t i = 0; i < int32_t(word_at<uint16_t>(current_group(record), 0xc)); ++i) {
        uint8_t* const item = group_item(current_group(record), uint32_t(i));
        const int32_t request = word_at<int32_t>(item, 0x10);
        if (request == -1) continue;
        const int32_t status = file_status(request);
        if (status != -1) {
            ++pending;
            continue;
        }
        word_at<int32_t>(item, 0x10) = status;
        word_at<uint32_t>(item, 0xc) |= 4;
        const uint32_t size = (word_at<uint32_t>(item, 4) + 0x7fff) & 0xffff8000;
        Stream_SetLoadProgress(0, int32_t(size), load_progress());
        const float zero = 0.0f;
        title_global<float>(0x0095f744) = (zero - title_global<float>(0x0095f740)) + title_global<float>(0x0095f744);
        title_global<float>(0x0095f740) = zero;
        title_global<uint32_t>(0x0095f73c) += size;
        title_global<uint32_t>(0x0095f738) = 0;
    }
    if (pending >= 1) return;
    for (int32_t i = 0; i < int32_t(word_at<uint16_t>(current_group(record), 0xc)); ++i) {
        uint8_t* const item = group_item(current_group(record), uint32_t(i));
        if (word_at<int32_t>(item, 0x10) == -1 && !(item[0xc] & 4) && word_at<uint32_t>(item, 4)) {
            const uint32_t size = (word_at<uint32_t>(item, 4) + 0x7fff) & 0xffff8000;
            const int32_t request = title_function<int32_t XB_CDECL(int32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
                0x000cae70)(word_at<int32_t>(record, 0x50), word_at<uint32_t>(item, 0), word_at<uint32_t>(item, 0x14),
                            size, 0x00291940);  // File_QueueRequestEx
            word_at<int32_t>(item, 0x10) = request;
            title_function<uint32_t XB_CDECL(int32_t, uint32_t)>(0x000c9df0)(request, 0);  // File_SetRequestPriority
            ++pending;
            title_global<float>(0x0095f740) = 0.0f;
            title_global<uint32_t>(0x0095f738) = 0;
        }
        if (pending == 1) return;
    }
}

uint8_t XB_REGS1 XB_CDECL Stream_IsChunkReady(uint8_t* record) {
    const uint8_t* const entry = word_at<uint8_t*>(current_group(record), 0x14) + word_at<uint32_t>(record, 0xd4) * 0x14;
    Stream_PollChunkReads(record);
    const uint8_t* const item = group_item(current_group(record), word_at<uint32_t>(entry, 0xc));
    if (item[0xc] & 4) return 1;
    const int32_t request = word_at<int32_t>(item, 0x10);
    if (request == -1) return 0;
    if (file_status(request) == -1) return 1;
    const uint32_t needed = word_at<uint32_t>(record, 0xd8) - word_at<uint32_t>(item, 0x14) + word_at<uint32_t>(entry, 8);
    // File_GetRequestProgress writes {bytes read, time}, or nothing for an unknown request (tests zero the dead stack).
    uint32_t progress[2] = {};
    title_function<void XB_CDECL(int32_t, uint32_t*)>(0x000c9ea0)(request, progress);
    if (progress[0] >= needed) return 1;
    Stream_SetLoadProgress(0, int32_t(progress[0]), load_progress());
    return 0;
}

PLATFORM_ASM_ENTRY(Stream_QueueLoad_entry,
                   "leal 8(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Stream_QueueLoad) "addl $16, %esp\n"
                   "ret $0x14")

uint32_t XB_CDECL Stream_QueueLoad(uint8_t* system, const char* name, uint32_t kind, const uint32_t* block) {
    using FileExists = int32_t XB_CDECL(uint32_t, const char*);
    const uint32_t device = title_global<uint32_t>(0x0095f6f4);
    const char* file = name;
    if (!title_function<FileExists>(0x000cae10)(device, name)) {  // File_Exists
        bool found = false;
        const uint8_t* const pak = word_at<const uint8_t*>(system, 0x18);
        if (pak) {
            file = reinterpret_cast<const char*>(pak + 8);  // the original's EBX after StringMap_Find
            const uint32_t* const index = Stream_StringMapFind_call(pak + 8, &name);
            if (index && word_at<const uint32_t*>(pak, 0x14)[wrap_index(*index, word_at<uint32_t>(pak, 0x10))]) {
                const uint8_t* const zone = title_function<uint8_t* XB_REGS3 XB_CDECL(uint8_t*, uint32_t, const char*)>(
                    0x0028fad0)(system, 0, name);  // Pak_FindZone
                file = word_at<const char*>(zone, 8);
                found = title_function<FileExists>(0x000cae10)(device, file) != 0;
            }
        }
        if (!found) title_function<void XB_CDECL(uint32_t)>(0x0033aa20)(0x0095f8d0);  // UI_ShowDamagedDiscScreen
    }
    const uint32_t id = word_at<uint32_t>(system, 0x1c);
    uint8_t* record = reinterpret_cast<uint8_t*>(uintptr_t{title_function<uint32_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(
        0x00044850)(title_global<uint32_t>(0x0095f624), 0xe4, 0)});  // SmallHeap_Alloc
    if (record) record = StreamRequest_Init(record, file, kind, id, block);
    word_at<uint8_t*>(record, 0) = system + 0x36c;  // a failed allocation writes through null, as the original
    uint8_t* const last = word_at<uint8_t*>(system, 0x370);
    word_at<uint8_t*>(record, 4) = last;
    word_at<uint8_t*>(last, 0) = record;
    const uint32_t count = word_at<uint32_t>(system, 0x364) + 1;
    word_at<uint8_t*>(system, 0x370) = record;
    word_at<uint32_t>(system, 0x364) = count;
    word_at<uint8_t*>(system, 0x38 + id * 4) = record;
    if (word_at<uint32_t>(record, 0x48) == 6) ++word_at<uint32_t>(system, 0x578);
    int32_t next = word_at<int32_t>(system, 0x1c);
    do {
        next = (next + 1) % 0x63;
    } while (word_at<uint32_t>(system, 0x38 + uint32_t(next) * 4));
    word_at<int32_t>(system, 0x1c) = next;
    word_at<uint32_t>(system, 0x10) = 0;
    word_at<uint32_t>(system, 0x20) = id;
    if (kind == 0)
        word_at<uint32_t>(system, 0x2c) = id;
    else if (kind == 7)
        word_at<uint32_t>(system, 0x28) = id;
    else if (kind == 2)
        word_at<uint32_t>(system, 0x34) = id;
    return id;
}

uint32_t XB_STDCALL Stream_ParseZbb(uint8_t* manager, uint8_t* data, uint32_t zone) {
    relocate_pack(data, 0x18);
    word_at<uint32_t>(data, 0x40) = zone;
    for (uint32_t i = 0; i < word_at<uint32_t>(data, 0x10); ++i) {
        uint8_t* const selector = word_at<uint8_t**>(data, 0x14)[wrap_index(i, word_at<uint32_t>(data, 0x10))];
        word_at<uint32_t>(selector, 0x24) =
            word_at<const uint8_t*>(global_context(), 0x18) ? pak_zone_of(word_at<const char*>(selector, 0x20)) : 0;
    }
    for (uint32_t j = 0; j < word_at<uint32_t>(data, 0x34); ++j) {
        uint8_t* const list = word_at<uint8_t**>(data, 0x38)[wrap_index(j, word_at<uint32_t>(data, 0x34))] + 0x1c;
        for (uint32_t k = 0; k < word_at<uint32_t>(list, 0); ++k) {
            uint8_t* const entry = word_at<uint8_t*>(list, 4) + wrap_index(k, word_at<uint32_t>(list, 0)) * 12;
            word_at<uint32_t>(entry, 4) =
                word_at<const uint8_t*>(global_context(), 0x18) ? pak_zone_of(word_at<const char*>(entry, 0)) : 0;
        }
    }
    word_at<uint8_t*>(manager, 4 + zone * 4) = data;
    int32_t last = word_at<int32_t>(manager, 0x1cc);
    if (last == -1) {
        word_at<uint32_t>(manager, 0x1cc) = zone;
    } else {
        int32_t next = word_at<int32_t>(word_at<uint8_t*>(manager, 4 + uint32_t(last) * 4), 0x3c);
        while (next != -1) {
            last = next;
            next = word_at<int32_t>(word_at<uint8_t*>(manager, 4 + uint32_t(last) * 4), 0x3c);
        }
        word_at<uint32_t>(word_at<uint8_t*>(manager, 4 + uint32_t(last) * 4), 0x3c) = zone;
    }
    ++word_at<uint32_t>(manager, 0x1d0);
    word_at<uint32_t>(manager, 0x1c0) = word_at<uint32_t>(data, 0x24);
    uint8_t view[16];
    std::memcpy(view, data + 0x28, 16);
    std::memcpy(manager + 0x1a0, view, 16);
    // One 16-aligned local: the view position for Stream_SetViewPosition, then the hashed "sky".
    alignas(16) uint32_t local[8] = {};
    std::memcpy(local, data + 0x28, 16);
    Stream_SetViewPosition(manager, word_at<uint32_t>(data, 0x24), reinterpret_cast<const float*>(local), 0);
    title_function<void XB_THISCALL(uint32_t*, const char*)>(0x00011060)(local, title_text(0x008330c0));  // HashStr_Init
    uint8_t* const system = global_context();
    uint8_t* node = word_at<uint8_t*>(system, 0x368);
    uint8_t* next = node ? word_at<uint8_t*>(node, 0) : nullptr;
    uint32_t id = 0xffffffff;
    if (node != system + 0x36c && next) {
        for (;;) {
            if (word_at<uint32_t>(node, 0x48) == 2) {
                id = word_at<uint32_t>(node, 0x74);
                break;
            }
            node = next;
            next = word_at<uint8_t*>(next, 0);
            if (!next) break;
        }
    }
    const uint32_t sky = Stream_FindInZones(0, 0, local, title_global<uint32_t>(0x0095f630), id, 0);
    title_global<uint32_t>(0x00988ce0) = sky;
    return sky;
}

void XB_CDECL Stream_LoadSpace(char*, uint8_t*, uint32_t, uint32_t) {}

}  // namespace game
