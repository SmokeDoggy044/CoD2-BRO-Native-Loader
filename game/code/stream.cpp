#include "stream.h"

#include <cstring>

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

}  // namespace game
