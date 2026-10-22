#include "stream.h"

#include <cstring>

#include "ngl.h"
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

uint32_t XB_CDECL Stream_LoadBmips(char*, uint8_t* data, uint32_t, uint32_t zone) {
    relocate_pack(data, 0x14);
    *reinterpret_cast<uint32_t*>(data + 8) = zone;
    title_global<uint32_t>(title_global<uint32_t>(0x0095f630) + 0x58) = address_of(data);
    return zone;
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

}  // namespace game
