#include "stream.h"

#include <cstring>

#include "cg_ents.h"
#include "lanes.h"
#include "ngl.h"
#include "qcommon.h"
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

}  // namespace game
