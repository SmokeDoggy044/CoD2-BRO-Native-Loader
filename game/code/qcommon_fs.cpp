#include "qcommon_fs.h"

#include <cstring>

#include "fx.h"
#include "fx_load.h"
#include "g_scr_runtime.h"
#include "qcommon.h"
#include "sv.h"
#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kScrUndefinedVecFloat = 0x00922eac;  // g_scrUndefinedFloat
constexpr uint32_t kScrUndefinedFloat = 0x00922eb8;
constexpr uint32_t kScrUndefinedInt = 0x00922ec0;   // g_scrUndefinedInt
constexpr uint32_t kScrUndefinedBool = 0x00922ed0;  // g_scrUndefinedBool

}  // namespace

// --- Script-heap containers ---

uint32_t* XB_STDCALL DynArray_Alloc24_FillUndefined(uint32_t count, uint32_t) {
    const uint32_t bytes = count * 24;
    uint32_t* data = Scr_Alloc(bytes, 4);
    uint8_t* const end = reinterpret_cast<uint8_t*>(data) + bytes;
    for (uint8_t* element = reinterpret_cast<uint8_t*>(data); element != end; element += 24) {
        if (!element) continue;
        uint32_t* words = reinterpret_cast<uint32_t*>(element);
        for (int i = 0; i < 6; ++i) words[i] = title_global<uint32_t>(kScrUndefinedFloat);
    }
    return data;
}

uint32_t* XB_STDCALL DynArray_AllocInt_FillUndefined(uint32_t count) {
    const uint32_t bytes = count << 2;
    uint32_t* data = Scr_Alloc(bytes, 4);
    uint8_t* const end = reinterpret_cast<uint8_t*>(data) + bytes;
    for (uint8_t* slot = reinterpret_cast<uint8_t*>(data); slot != end; slot += 4)
        if (slot) *reinterpret_cast<uint32_t*>(slot) = title_global<uint32_t>(kScrUndefinedInt);
    return data;
}

uint8_t* XB_STDCALL DynArray_AllocBool_FillUndefined(uint32_t count) {
    uint8_t* data = reinterpret_cast<uint8_t*>(Scr_Alloc(count, 4));
    uint8_t* const end = data + count;
    for (uint8_t* slot = data; slot != end; ++slot)
        if (slot) *slot = title_global<uint8_t>(kScrUndefinedBool);
    return data;
}

uint32_t* XB_CDECL std_Fill4(uint32_t* first, uint32_t* last, const uint32_t* value) {
    for (; first != last; ++first) *first = *value;
    return first;
}

uint32_t* XB_CDECL std_CopyBackward4(uint32_t* first, uint32_t* last, uint32_t* dest_end) {
    while (last != first) {
        const uint32_t word = *(last - 1);
        --last;
        --dest_end;
        *dest_end = word;
    }
    return dest_end;
}

void XB_THISCALL DynArray_ResizeInt(DynArray* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            uint8_t* slot = reinterpret_cast<uint8_t*>(array->data + old_size);
            uint8_t* const end = reinterpret_cast<uint8_t*>(array->data + size);
            for (; slot != end; slot += 4)
                if (slot) *reinterpret_cast<uint32_t*>(slot) = title_global<uint32_t>(kScrUndefinedInt);
        }
        array->size = size;
        return;
    }
    uint32_t* data = DynArray_AllocInt_FillUndefined(capacity);
    for (uint32_t i = 0; i < (array->size >= size ? size : array->size); ++i) data[i] = array->data[i];
    if (array->data) Scr_Free(array->data);
    array->capacity = capacity;
    array->data = data;
    array->size = size;
}

void XB_THISCALL DynArray_Assign4(DynArray* array, const DynArray* source) {
    if (array->data) Scr_Free(array->data);
    if (!source->size) {
        array->data = nullptr;
        array->size = 0;
        array->capacity = 0;
        return;
    }
    uint32_t* data = DynArray_AllocInt_FillUndefined(source->capacity);
    for (int32_t i = 0; i < static_cast<int32_t>(source->size); ++i) data[i] = source->data[i];
    array->data = data;
    array->size = source->size;
    array->capacity = source->capacity;
}

void XB_THISCALL DynArray_ResizeBytes(DynArrayBytes* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            uint8_t* slot = array->data + old_size;
            uint8_t* const end = array->data + size;
            for (; slot != end; ++slot)
                if (slot) *slot = title_global<uint8_t>(kScrUndefinedBool);
        }
        array->size = size;
        return;
    }
    uint8_t* data = reinterpret_cast<uint8_t*>(Scr_Alloc(capacity, 4));
    uint8_t* const end = data + capacity;
    for (uint8_t* slot = data; slot != end; ++slot)
        if (slot) *slot = title_global<uint8_t>(kScrUndefinedBool);
    for (uint32_t i = 0; i < (array->size >= size ? size : array->size); ++i) data[i] = array->data[i];
    if (array->data) Scr_Free(array->data);
    array->data = data;
    array->capacity = capacity;
    array->size = size;
}

uint32_t* XB_CDECL std_UninitializedCopy4(const uint32_t* first, const uint32_t* last, uint32_t* dest) {
    for (; first != last; ++first, ++dest)
        if (dest) *dest = *first;
    return dest;
}

uint32_t* XB_STDCALL std_list_BuyNode16(uint32_t next, uint32_t prev, const uint32_t* value) {
    uint32_t* node = Scr_Alloc(0x18, 4);
    if (node) {
        node[0] = next;
        node[1] = prev;
        node[2] = value[0];
        node[3] = value[1];
        node[4] = value[2];
        node[5] = value[3];
    }
    return node;
}

uint32_t* XB_CDECL std_list_BuyHead() {
    uint32_t* node = Scr_Alloc(0x18, 4);
    if (node) node[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    // The original tests &node[1] for null (never true) before setting prev.
    node[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    return node;
}

void XB_THISCALL DynArray_Resize4(DynArray* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        array->size = size;
        return;
    }
    uint32_t* data = Scr_Alloc(capacity * 4, 4);
    for (uint32_t i = 0; i < (array->size >= size ? size : array->size); ++i) data[i] = array->data[i];
    if (array->data) Scr_Free(array->data);
    array->capacity = capacity;
    array->data = data;
    array->size = size;
}

void XB_THISCALL DynArray_ResizeBytesNoFill(DynArrayBytes* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        array->size = size;
        return;
    }
    uint8_t* data = reinterpret_cast<uint8_t*>(Scr_Alloc(capacity, 4));
    for (uint32_t i = 0; i < (array->size >= size ? size : array->size); ++i) data[i] = array->data[i];
    if (array->data) Scr_Free(array->data);
    array->capacity = capacity;
    array->data = data;
    array->size = size;
}

uint32_t* XB_CDECL std_UninitializedFillN4(uint32_t* dest, uint32_t count, const uint32_t* value) {
    if (!count) return nullptr;  // EAX = the zero count
    for (uint32_t left = count; left; --left, ++dest)
        if (dest) *dest = *value;
    return dest;
}

namespace {

// The buckets hold list iterators; each bucket's nodes are sorted by key.
template <typename Key>
HashNode** hash_map_find(HashMap* map, HashNode** out, Key key) {
    const uint32_t mask = map->mask;
    uint32_t index = (static_cast<uint32_t>(key) ^ 0xdeadbeef) & mask;
    if (map->max_index <= index) index += 0xffffffffu - (mask >> 1);
    HashNode* const last = map->buckets[index + 1];
    HashNode* node = map->buckets[index];
    for (; node != last; node = node->next) {
        const Key node_key = static_cast<Key>(node->key);
        if (node_key >= key) {
            *out = key < node_key ? map->head : node;
            return out;
        }
    }
    *out = map->head;
    return out;
}

}  // namespace

HashNode** XB_THISCALL HashMap_Find(HashMap* map, HashNode** out, const uint32_t* key) {
    return hash_map_find<uint32_t>(map, out, *key);
}

uint32_t** XB_THISCALL std_vector_Erase4(Vector4* vector, uint32_t** out, uint32_t* first, uint32_t* last) {
    if (first != last) {
        uint32_t* const end = vector->last;
        uint32_t* dest = first;
        for (; last != end; ++last, ++dest) *dest = *last;
        vector->last = dest;
    }
    *out = first;
    return out;
}
void XB_THISCALL DynArray_AssignBytes(DynArrayBytes* array, const DynArrayBytes* source) {
    if (array->data) Scr_Free(array->data);
    if (!source->size) {
        array->data = nullptr;
        array->size = 0;
        array->capacity = 0;
        return;
    }
    uint8_t* data = DynArray_AllocBool_FillUndefined(source->capacity);
    for (int32_t i = 0; i < static_cast<int32_t>(source->size); ++i) data[i] = source->data[i];
    array->data = data;
    array->size = source->size;
    array->capacity = source->capacity;
}

float* XB_CDECL Vec3_SetScaled(float* out, float x, float y, float z, float scale) {
    const float sx = x * scale;
    const float sy = y * scale;
    out[0] = sx;
    const float sz = z * scale;
    out[1] = sy;
    out[2] = sz;
    return out;
}

uint32_t* XB_CDECL PtrArray_RemoveValue(DynArray* array, uint32_t value) {
    uint32_t* p = array->data;
    uint32_t* const end = array->data + array->size;
    if (p == end) return p;
    while (*p != value)
        if (++p == end) return p;
    uint32_t* const last = end - 1;
    for (; p != last; ++p) p[0] = p[1];
    --array->size;
    return p;
}

uint32_t* XB_STDCALL DynArray_Alloc12_FillUndefined(uint32_t count, uint32_t) {
    const uint32_t bytes = count * 12;
    uint32_t* data = Scr_Alloc(bytes, 4);
    uint8_t* const end = reinterpret_cast<uint8_t*>(data) + bytes;
    for (uint8_t* element = reinterpret_cast<uint8_t*>(data); element != end; element += 12) {
        if (!element) continue;
        uint32_t* words = reinterpret_cast<uint32_t*>(element);
        words[0] = title_global<uint32_t>(kScrUndefinedInt);
        words[1] = title_global<uint32_t>(kScrUndefinedInt);
        words[2] = 0;
    }
    return data;
}

uint32_t* XB_STDCALL DynArray_Alloc24_FillUndefined_2(uint32_t count, uint32_t) {
    const uint32_t bytes = count * 24;
    uint32_t* data = Scr_Alloc(bytes, 4);
    uint8_t* const end = reinterpret_cast<uint8_t*>(data) + bytes;
    for (uint8_t* element = reinterpret_cast<uint8_t*>(data); element != end; element += 24) {
        if (!element) continue;
        uint32_t* words = reinterpret_cast<uint32_t*>(element);
        for (int i = 0; i < 3; ++i) words[i] = title_global<uint32_t>(0x00922eb4);
        words[3] = title_global<uint32_t>(kScrUndefinedVecFloat);
        words[4] = title_global<uint32_t>(kScrUndefinedVecFloat);
        words[5] = title_global<uint32_t>(kScrUndefinedInt);
    }
    return data;
}

uint32_t* XB_STDCALL DynArray_Alloc52_FillUndefined(uint32_t count, uint32_t) {
    const uint32_t bytes = count * 0x34;
    uint32_t* data = Scr_Alloc(bytes, 4);
    uint8_t* const end = reinterpret_cast<uint8_t*>(data) + bytes;
    for (uint8_t* element = reinterpret_cast<uint8_t*>(data); element != end; element += 0x34) {
        if (!element) continue;
        uint32_t* words = reinterpret_cast<uint32_t*>(element);
        words[0] = title_global<uint32_t>(kScrUndefinedInt);
        words[1] = 0;
        words[2] = 0;
        words[3] = title_global<uint32_t>(0x00922eb4);
        for (int i = 4; i < 7; ++i) words[i] = title_global<uint32_t>(kScrUndefinedVecFloat);
        for (int i = 7; i < 13; ++i) words[i] = title_global<uint32_t>(kScrUndefinedFloat);
    }
    return data;
}

HashNode** XB_THISCALL HashMap_Find_2(HashMap* map, HashNode** out, const int32_t* key) {
    return hash_map_find<int32_t>(map, out, *key);
}

uint32_t* XB_THISCALL std_exception_Ctor(uint32_t* self) {
    self[1] = 0;
    self[2] = 0;
    self[0] = 0x00827904;  // std::exception vtable
    return self;
}

// --- Event queue ---

namespace {

constexpr uint32_t kEventQueue = 0x0095e800;
constexpr uint32_t kEventHead = 0x0095e7f4;
constexpr uint32_t kEventTail = 0x0095ee04;
constexpr uint32_t kSysMilliseconds = 0x002b1ca0;
constexpr uint32_t kZFree = 0x000428d0;

SysEvent& event_slot(int32_t index) {
    return (&title_global<SysEvent>(kEventQueue))[index & 0x3f];
}

}  // namespace

PLATFORM_ASM_ENTRY(Sys_GetEvent_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Sys_GetEvent) "addl $4, %esp\n"
                   "ret")

SysEvent* XB_CDECL Sys_GetEvent(SysEvent* out) {
    int32_t& tail = title_global<int32_t>(kEventTail);
    if (title_global<int32_t>(kEventHead) > tail) {
        ++tail;
        const SysEvent& event = event_slot(tail - 1);
        *out = event;
        return out;
    }
    const int32_t time = title_function<int32_t XB_CDECL()>(kSysMilliseconds)();
    out->time = time;
    out->type = 0;
    out->value = 0;
    out->value2 = 0;
    out->ptr_length = 0;
    out->ptr = nullptr;
    return out;
}

int32_t XB_REGS1 XB_CDECL Sys_QueEvent(int32_t time, int32_t type, int32_t value, int32_t value2,
                                       int32_t ptr_length, void* ptr) {
    const int32_t head = title_global<int32_t>(kEventHead);
    SysEvent& event = event_slot(head);
    if (head - title_global<int32_t>(kEventTail) >= 64) {
        if (event.ptr) title_function<void XB_CDECL(void*)>(kZFree)(event.ptr);
        ++title_global<int32_t>(kEventTail);
    }
    ++title_global<int32_t>(kEventHead);
    if (!time) time = title_function<int32_t XB_CDECL()>(kSysMilliseconds)();
    event.type = type;
    event.value = value;
    event.time = time;
    event.value2 = value2;
    event.ptr_length = ptr_length;
    event.ptr = ptr;
    return ptr_length;
}

// --- File system ---

int32_t XB_REGS1 XB_CDECL FS_DirectoryHasFiles(const char* path) {
    using FindFirst = uint32_t XB_STDCALL(const char*, uint8_t*);
    using FindNext = int32_t XB_STDCALL(uint32_t, uint8_t*);
    using Close = int32_t XB_STDCALL(uint32_t);
    char mask[0x80] = {};
    uint8_t data[0x140] = {};  // WIN32_FIND_DATAA: attributes +0, cFileName +0x2c
    const uint32_t args[2] = {0x0083cac8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path))};  // "%s\*"
    Com_sprintf(mask, 0x80, args);
    const uint32_t handle = title_function<FindFirst>(0x001e79ad)(mask, data);
    if (handle == 0xffffffff) return 0;
    do {
        if (!(data[0] & 0x10)) return 1;  // a file
        const char* name = reinterpret_cast<const char*>(data + 0x2c);
        if (Q_stricmpn(name, &title_global<const char>(0x0082d700), 99999) &&     // "."
            Q_stricmpn(name, &title_global<const char>(0x008380a4), 99999) &&     // ".."
            Q_stricmpn(name, &title_global<const char>(0x0083cac4), 99999))       // "CVS"
            return 1;
    } while (title_function<FindNext>(0x001e7aba)(handle, data));
    title_function<Close>(0x001e7b0b)(handle);
    return 0;
}

// --- Angles ---

namespace {

// Shared tail: 16-bit quantization (182.044 = 65536 / 360) and the wrap to (-180, 180].
float angle_quantize_wrap(float angle) {
    const float scaled = angle * title_global<float>(0x0083ebd8);
    const int32_t units = static_cast<int32_t>(scaled) & 0xffff;
    float result = static_cast<float>(units) * title_global<float>(0x0083ebd4);
    if (result > title_global<float>(0x0083eb60)) result = result - title_global<float>(0x0083ebcc);
    return result;
}

float angle_wrap_to_target(float angle, float target) {
    const float half = title_global<float>(0x0083eb60);   // 180
    const float full = title_global<float>(0x0083ebcc);   // 360
    while (angle - target > half) angle = angle - full;
    const float low = title_global<float>(0x0083ebd0);    // -180
    while (low > angle - target) angle = angle + full;
    return angle;
}

float sign_cleared(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits &= 0x7fffffff;
    std::memcpy(&value, &bits, 4);
    return value;
}

// |delta| > 0.005 and the step does not overshoot: angle = target + step (x87 compares).
float angle_step(float angle, float target, float delta, float step) {
    const float delta_abs = sign_cleared(delta);
    if (platform_x87_greater(delta_abs, title_global<float>(0x0083eb88)) &&
        !platform_x87_greater(sign_cleared(step), delta_abs))
        angle = step + target;
    return angle;
}

}  // namespace

// Callers keep ECX/EDX across the call; the original only writes XMM0-5 and EAX.
PLATFORM_ASM_ENTRY(AngleWrapNear_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "subl $8, %esp\n"
                   "movss %xmm2, 4(%esp)\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(AngleWrapNear) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $16, %esp\n"
                   "popl %edx\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL AngleWrapNear(float angle, float target, float scale, int32_t rate) {
    angle = angle_wrap_to_target(angle, target);
    float step = static_cast<float>(rate) * title_global<float>(0x0083eb4c);  // 0.001
    const float delta = angle - target;
    step = step * delta;
    step = step * scale;
    return angle_quantize_wrap(angle_step(angle, target, delta, step));
}

PLATFORM_ASM_ENTRY(AngleWrapNear2_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl 12(%esp)\n"
                   "subl $8, %esp\n"
                   "movss %xmm1, 4(%esp)\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(AngleWrapNear2) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $12, %esp\n"
                   "popl %edx\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL AngleWrapNear2(float angle, float target, float scale) {
    angle = angle_wrap_to_target(angle, target);
    const float delta = angle - target;
    float step = title_global<float>(0x008ddaa0) * scale;
    if (!(delta > 0.0f)) step = 0.0f - step;
    return angle_quantize_wrap(angle_step(angle, target, delta, step));
}

// --- Bare `ret` stubs ---

void XB_CDECL Cmd_NglStats_f() {}
void XB_CDECL Cmd_Invinc_f() {}
void XB_CDECL Cmd_List_f() {}
void XB_CDECL Cmd_Echo_f() {}
void XB_CDECL CrtInitStub_379cc0() {}
void XB_CDECL CrtInitStub_379cf0() {}
void XB_CDECL CrtInitStub_379d00() {}
void XB_CDECL CrtInitStub_379d10() {}
void XB_CDECL CrtInitStub_379d20() {}
void XB_CDECL CrtInitStub_379d30() {}

// --- Boot ---

void XB_CDECL Mem_InitLargeBlockPools() {
    static const int32_t kPools[13][2] = {
        {0x1f4, 0x40},  {0x190, 0x80},  {0x190, 0x100}, {0x12c, 0x200}, {0xc8, 0x400},
        {0x64, 0x800},  {0x3c, 0xc00},  {0x32, 0x1000}, {0x32, 0x1400}, {0x1e, 0x1800},
        {0x1e, 0x1c00}, {0x32, 0x2000}, {0xf, 0x4000},
    };
    FxPools_Clear();
    for (int i = 0; i < 13; ++i) apsAddPool(kPools[i][0], kPools[i][1]);
}

// --- Cheat commands ---

namespace {

constexpr uint32_t kCheatsEnabled = 0x00954f34;

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t text_address(uint32_t address) { return address; }

// SV_GameSendServerCommand(va(format, text), ent's client handle +0x224).
void send_print(uint8_t* ent, uint32_t format, uint32_t text) {
    const uint32_t handle = field<uint32_t>(ent, 0x224);
    const uint32_t args[2] = {format, text};
    SV_GameSendServerCommand(va(args), handle);
}

// The common checks; true when the command may run.
bool cheat_allowed(uint8_t* ent) {
    if (!title_global<int32_t>(kCheatsEnabled)) {
        send_print(ent, text_address(0x0083bd78), 0);  // "print \"GAME_CHEATSNOTENABLED\""
        return false;
    }
    if (field<int32_t>(ent, 0x350) <= 0) {
        send_print(ent, text_address(0x0083bd58), 0);  // "print \"GAME_MUSTBEALIVECOMMAND\""
        return false;
    }
    return true;
}

constexpr uint32_t kPrintString = 0x00831d5c;  // "print \"%s\""

}  // namespace

void XB_CDECL Cmd_SetSpawnPt_f() {
    if (title_global<int32_t>(0x00934248) != 2) return;  // cmd_argc
    char text[0x80];
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(  // _strncpy
        text, title_global<const char*>(0x0092df94), 0x7f);
    text[0x7f] = 0;
    title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(text);  // _atol; the value is unused
}

void XB_CDECL Cmd_UFO_f(uint8_t* ent) {
    if (!cheat_allowed(ent)) return;
    uint8_t* client = field<uint8_t*>(ent, 0x23c);
    const int32_t ufo = field<int32_t>(client, 0x588);
    const uint32_t text = ufo ? 0x0083bcd4 : 0x0083bcc8;  // "GAME_UFOOFF" : "GAME_UFOON"
    field<int32_t>(client, 0x588) = ufo == 0;
    send_print(ent, kPrintString, text);
}

void XB_CDECL Cmd_Noclip_f(uint8_t* ent) {
    if (!cheat_allowed(ent)) return;
    uint8_t* client = field<uint8_t*>(ent, 0x23c);
    const int32_t noclip = field<int32_t>(client, 0x584);
    uint32_t text;
    if (noclip) {
        text = 0x00831d78;  // "GAME_NOCLIPOFF"
        title_global<uint8_t>(0x0095f679) = 0;
    } else {
        text = 0x00831d68;  // "GAME_NOCLIPON"
        title_global<uint8_t>(0x0095f679) = 1;
    }
    field<int32_t>(client, 0x584) = noclip == 0;
    send_print(ent, kPrintString, text);
}

void XB_CDECL Cmd_Notarget_f(uint8_t* ent) {
    if (!cheat_allowed(ent)) return;
    const uint32_t flags = field<uint32_t>(ent, 0x2a4) ^ 2;
    field<uint32_t>(ent, 0x2a4) = flags;
    uint32_t text;
    if (flags & 2) {
        text = 0x0083bce0;  // "GAME_NOTARGETON"
        title_global<uint8_t>(0x0095f67a) = 1;
    } else {
        text = 0x0083bcf0;  // "GAME_NOTARGETOFF"
        title_global<uint8_t>(0x0095f67a) = 0;
    }
    send_print(ent, kPrintString, text);
}

void XB_CDECL Cmd_God_f(uint8_t* ent) {
    if (!cheat_allowed(ent)) return;
    field<uint32_t>(ent, 0x2a4) ^= 1;
    uint32_t text;
    if (field<uint8_t>(ent, 0x2a4) & 1) {
        text = 0x0083bd04;  // "GAME_GODMODEON"
        title_global<uint8_t>(0x0095f678) = 1;
    } else {
        text = 0x0083bd14;  // "GAME_GODMODEOFF"
        title_global<uint8_t>(0x0095f678) = 0;
    }
    send_print(ent, kPrintString, text);
}

// --- Angles (2) ---

PLATFORM_ASM_ENTRY(Angles2Normalize180_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Angles2Normalize180) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Angles2Normalize180(float* angles) {
    const float yaw = Q_fmod(angles[0], title_global<float>(0x00972c10));
    const float second = angles[1];
    angles[0] = yaw;
    angles[1] = Q_fmod(second, title_global<float>(0x00972c14));
}

// --- Pak zones ---

void XB_REGS1 XB_STDCALL Pak_ReportProgress(uint8_t* pak, float fraction) {
    const uint32_t callback = field<uint32_t>(pak, 0x35c);
    if (!callback) return;
    const float weight = title_global<float>(0x008d9d40);
    float value = title_global<float>(0x0083eb20) - weight;  // 1
    value = value - title_global<float>(0x008d9d3c);
    const float part = weight * fraction;
    value = value + part;
    title_function<void XB_CDECL(float)>(callback)(value);
}

void XB_THISCALL Pak_Clear(uint8_t* pak) {
    field<uint8_t>(pak, 0x14) = 0;
    field<uint32_t>(pak, 0x358) = 0;
    field<uint32_t>(pak, 0x1c) = 0;
    field<uint32_t>(pak, 0x24) = 0;
    field<uint32_t>(pak, 0x354) = 0;
    for (uint32_t i = 0; i < 100; ++i) {
        field<uint32_t>(pak, 0x38 + i * 4) = 0;
        field<uint32_t>(pak, 0x1c4 + i * 4) = 0;
    }
    field<uint32_t>(pak, 0x350) = 0;
}

int32_t XB_THISCALL Pak_TopZoneId(const uint8_t* pak) {
    const int32_t count = *reinterpret_cast<const int32_t*>(pak + 0x374);
    if (!count) return -1;
    int32_t index = count - 1;
    if (index <= 0) index = 0;
    return reinterpret_cast<const int32_t*>(pak + 0x378)[index];
}

// --- Byte order ---

float XB_CDECL FloatNoSwap(float value) { return value; }

float XB_CDECL FloatSwap(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits = (bits >> 24) | ((bits >> 8) & 0xff00) | ((bits << 8) & 0xff0000) | (bits << 24);
    float swapped;
    std::memcpy(&swapped, &bits, 4);
    return swapped;
}

uint64_t XB_CDECL Long64NoSwap(uint64_t value) { return value; }

uint64_t XB_CDECL Long64Swap(uint64_t value) {
    uint64_t swapped = 0;
    for (int i = 0; i < 8; ++i) swapped = (swapped << 8) | ((value >> (i * 8)) & 0xff);
    return swapped;
}

int32_t XB_CDECL LongNoSwap(int32_t value) { return value; }

int32_t XB_CDECL LongSwap(int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    return static_cast<int32_t>((bits >> 24) | ((bits >> 8) & 0xff00) | ((bits << 8) & 0xff0000) | (bits << 24));
}

PLATFORM_ASM_ENTRY(ShortNoSwap_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ShortNoSwap) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL ShortNoSwap(uint32_t eax, uint32_t value) { return (eax & 0xffff0000u) | (value & 0xffff); }

uint32_t XB_CDECL ShortSwap(uint32_t value) { return ((value >> 8) & 0xff) + ((value & 0xff) << 8); }

// --- Quantized angles (unnamed in the symbol export) ---

// Callers keep ECX/EDX; the originals write only EAX and XMM0.
PLATFORM_ASM_ENTRY(AngleNormalize180Quantized_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(AngleNormalize180Quantized) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "popl %edx\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL AngleNormalize180Quantized(float angle) { return angle_quantize_wrap(angle); }

PLATFORM_ASM_ENTRY(AngleDeltaQuantized_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl 12(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(AngleDeltaQuantized) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $8, %esp\n"
                   "popl %edx\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL AngleDeltaQuantized(float a, float b) { return angle_quantize_wrap(a - b); }

}  // namespace game
