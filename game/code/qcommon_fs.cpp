#include "qcommon_fs.h"

#include <cstring>

#include "cm.h"
#include "fx.h"
#include "lanes.h"
#include "fx_load.h"
#include "g_scr_runtime.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "sv.h"
#include "title_data.h"
#include "unzip.h"

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

// --- Vector and matrix helpers (q_math) ---

namespace {

constexpr uint32_t kByteDirs = 0x008d52d8;  // bytedirs[162][3]
constexpr uint32_t kZero = 0x0083eafc;      // 0.0f
constexpr uint32_t kOne = 0x0083eb20;       // 1.0f

// The title CRT's __CIsqrt of an x87 value, stored as float.
float ci_sqrt(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// FUCOMIP against 0 (no MXCSR flags): true for nonzero and NaN.
bool x87_nonzero(float value) { return static_cast<long double>(value) != title_global<float>(kZero); }

void copy_word(void* dst, const void* src) { std::memcpy(dst, src, 4); }

}  // namespace

void XB_REGS3 XB_CDECL AxisCopy(float* out, uint32_t, const float* in) {
    for (int i = 0; i < 9; ++i) copy_word(out + i, in + i);
}

void XB_REGS3 XB_CDECL MatrixTranspose(float* out, uint32_t, const float* in) {
    static const int kSource[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
    for (int i = 0; i < 9; ++i) copy_word(out + i, in + kSource[i]);
}

PLATFORM_ASM_ENTRY(ByteToDir_entry,
                   "pushl %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ByteToDir) "addl $8, %esp\n"
                   "popl %ecx\n"
                   "ret")

void XB_CDECL ByteToDir(int32_t index, float* out) {
    if (index < 0 || index >= 162) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        return;
    }
    const float* dir = &title_global<float>(kByteDirs + index * 12);
    copy_word(out, dir);
    copy_word(out + 1, dir + 1);
    copy_word(out + 2, dir + 2);
}

PLATFORM_ASM_ENTRY(DirToByte_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(DirToByte) "addl $4, %esp\n"
                   "ret")

uint8_t XB_CDECL DirToByte(const float* dir) {
    uint8_t best = 0;
    if (!dir) return best;
    float best_dot = 0.0f;
    for (uint8_t i = 0; i < 162; ++i) {
        const float* table = &title_global<float>(kByteDirs + i * 12);
        float dot = table[2] * dir[2];
        dot = dot + table[0] * dir[0];
        dot = dot + table[1] * dir[1];
        if (dot > best_dot) {
            best_dot = dot;
            best = i;
        }
    }
    return best;
}

float XB_REGS3 XB_CDECL Vec2Distance(const float* a, uint32_t, const float* b) {
    const float dx = static_cast<float>(static_cast<long double>(a[0]) - b[0]);
    const float dy = static_cast<float>(static_cast<long double>(a[1]) - b[1]);
    const long double sum = static_cast<long double>(dy) * dy + static_cast<long double>(dx) * dx;
    return ci_sqrt(sum);
}

float XB_REGS3 XB_CDECL Vec3Distance(const float* a, uint32_t, const float* b) {
    const float dx = static_cast<float>(static_cast<long double>(a[0]) - b[0]);
    const float dy = static_cast<float>(static_cast<long double>(a[1]) - b[1]);
    const float dz = static_cast<float>(static_cast<long double>(a[2]) - b[2]);
    long double sum = static_cast<long double>(dz) * dz + static_cast<long double>(dy) * dy;
    sum = sum + static_cast<long double>(dx) * dx;
    return ci_sqrt(sum);
}

// The three normalizers keep ECX (push/pop around the body) and leave the length in XMM0.
PLATFORM_ASM_ENTRY(Vec2Normalize_entry,
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Vec2Normalize) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL Vec2Normalize(float* v) {
    const float x = v[0];
    const float y = v[1];
    const float length = ci_sqrt(x * x + y * y);
    if (x87_nonzero(length)) {
        const float scale = title_global<float>(kOne) / length;
        const float nx = v[0] * scale;
        const float ny = scale * v[1];
        v[0] = nx;
        v[1] = ny;
    }
    return length;
}

PLATFORM_ASM_ENTRY(VectorNormalize_entry,
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(VectorNormalize) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL VectorNormalize(float* v) {
    const float x = v[0];
    const float y = v[1];
    const float z = v[2];
    const float length = ci_sqrt((x * x + y * y) + z * z);
    if (x87_nonzero(length)) {
        const float scale = title_global<float>(kOne) / length;
        v[0] = v[0] * scale;
        const float ny = scale * v[1];
        const float nz = scale * v[2];
        v[1] = ny;
        v[2] = nz;
    }
    return length;
}

PLATFORM_ASM_ENTRY(VectorNormalize2_entry,
                   "pushl %ecx\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(VectorNormalize2) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $8, %esp\n"
                   "popl %ecx\n"
                   "ret")

float XB_CDECL VectorNormalize2(const float* in, float* out) {
    const float x = in[0];
    const float y = in[1];
    const float z = in[2];
    const float length = ci_sqrt((x * x + y * y) + z * z);
    if (x87_nonzero(length)) {
        const float scale = title_global<float>(kOne) / length;
        out[0] = in[0] * scale;
        out[1] = scale * in[1];
        out[2] = scale * in[2];
    } else {
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
    }
    return length;
}

// Both products keep EDX (out) for their callers.
PLATFORM_ASM_ENTRY(MatrixMultiply_entry,
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %eax\n"
                   "pushl 16(%esp)\n" PLATFORM_ASM_CALL(MatrixMultiply) "addl $12, %esp\n"
                   "popl %edx\n"
                   "ret")

void XB_CDECL MatrixMultiply(const float* a, const float* b, float* out) {
    out[0] = (a[0] * b[0] + a[1] * b[3]) + b[6] * a[2];
    out[1] = (a[1] * b[4] + b[7] * a[2]) + b[1] * a[0];
    out[2] = (a[1] * b[5] + b[8] * a[2]) + b[2] * a[0];
    out[3] = (a[4] * b[3] + a[3] * b[0]) + a[5] * b[6];
    out[4] = (a[5] * b[7] + a[4] * b[4]) + a[3] * b[1];
    out[5] = (a[5] * b[8] + a[4] * b[5]) + a[3] * b[2];
    out[6] = (a[6] * b[0] + b[6] * a[8]) + a[7] * b[3];
    out[7] = (b[7] * a[8] + b[4] * a[7]) + b[1] * a[6];
    out[8] = (b[8] * a[8] + b[5] * a[7]) + b[2] * a[6];
}

PLATFORM_ASM_ENTRY(MatrixMultiply43_entry,
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %eax\n"
                   "pushl 16(%esp)\n" PLATFORM_ASM_CALL(MatrixMultiply43) "addl $12, %esp\n"
                   "popl %edx\n"
                   "ret")

void XB_CDECL MatrixMultiply43(const float* a, const float* b, float* out) {
    out[0] = (a[0] * b[0] + a[1] * b[3]) + b[6] * a[2];
    out[3] = (a[4] * b[3] + a[3] * b[0]) + a[5] * b[6];
    out[6] = (a[7] * b[3] + a[6] * b[0]) + a[8] * b[6];
    out[1] = (a[1] * b[4] + b[7] * a[2]) + b[1] * a[0];
    out[4] = (a[5] * b[7] + a[4] * b[4]) + a[3] * b[1];
    out[7] = (a[8] * b[7] + a[7] * b[4]) + a[6] * b[1];
    out[2] = (a[0] * b[2] + a[1] * b[5]) + b[8] * a[2];
    out[5] = (a[5] * b[8] + a[4] * b[5]) + a[3] * b[2];
    out[8] = (a[8] * b[8] + a[7] * b[5]) + a[6] * b[2];
    out[9] = ((a[10] * b[3] + a[9] * b[0]) + a[11] * b[6]) + b[9];
    out[10] = ((a[11] * b[7] + a[10] * b[4]) + a[9] * b[1]) + b[10];
    out[11] = ((a[11] * b[8] + a[10] * b[5]) + a[9] * b[2]) + b[11];
}

void XB_REGS3 XB_CDECL MatrixInverse43(float* out, uint32_t, const float* in) {
    copy_word(out + 0, in + 0);
    copy_word(out + 1, in + 3);
    copy_word(out + 2, in + 6);
    copy_word(out + 3, in + 1);
    const float out3 = out[3];
    copy_word(out + 4, in + 4);
    copy_word(out + 5, in + 7);
    copy_word(out + 6, in + 2);
    const float out6 = out[6];
    copy_word(out + 7, in + 5);
    copy_word(out + 8, in + 8);
    const float nx = 0.0f - in[9];
    const float nz = 0.0f - in[11];
    const float ny = 0.0f - in[10];
    out[9] = (out3 * ny + out6 * nz) + out[0] * nx;
    out[10] = (out[4] * ny + out[7] * nz) + nx * out[1];
    const float last = ny * out[5];
    out[11] = (out[8] * nz + out[2] * nx) + last;
}

PLATFORM_ASM_ENTRY(AnglesNormalize180_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(AnglesNormalize180) "addl $12, %esp\n"
                   "ret")

void XB_CDECL AnglesNormalize180(const float* b, const float* a, float* out) {
    for (int i = 0; i < 3; ++i) out[i] = Q_fmod(a[i], b[i]);
}

PLATFORM_ASM_ENTRY(PlaneFromPoints_entry,
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PlaneFromPoints) "addl $16, %esp\n"
                   "ret")

int32_t XB_CDECL PlaneFromPoints(const float* a, const float* b, const float* origin, float* plane) {
    const float d1y = a[1] - origin[1];
    const float d1z = a[2] - origin[2];
    const float d2y = b[1] - origin[1];
    const float d2z = b[2] - origin[2];
    const float d1x = a[0] - origin[0];
    const float d2x = b[0] - origin[0];
    const float nx = d1y * d2z - d1z * d2y;
    const float nz = d1x * d2y - d1y * d2x;
    plane[2] = nz;
    const float ny = d1z * d2x - d1x * d2z;
    const float length = ci_sqrt((nz * nz + ny * ny) + nx * nx);
    plane[0] = nx;
    plane[1] = ny;
    if (!x87_nonzero(length)) return 0;
    const float scale = title_global<float>(kOne) / length;
    const float z = nz * scale;
    const float x = nx * scale;
    const float y = ny * scale;
    plane[2] = z;
    plane[0] = x;
    plane[1] = y;
    const float zd = z * origin[2];
    plane[3] = (origin[0] * x + zd) + origin[1] * y;
    return 1;
}

PLATFORM_ASM_ENTRY(Vec3NormalizeSimd_entry,
                   "pushl %ecx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Vec3NormalizeSimd) "addl $4, %esp\n"
                   "popl %ecx\n"
                   "ret")

void XB_CDECL Vec3NormalizeSimd(float* v) {
    const Lanes value = lanes(v);
    const Lanes squares = mul(value, value);
    platform_keep(squares);
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    float length = sum;
    // UCOMISS; JNE: zero and NaN keep the sum.
    if (!(sum == title_global<float>(0x0082e680) || sum != sum)) {
        const float estimate = platform_rsqrt_estimate(sum);
        const float t = sum * estimate;
        const float u = t * estimate;
        length = title_global<float>(0x0082e660) - u;  // 3
        length = length * t;
        length = length * title_global<float>(0x0082e670);  // 0.5
    }
    if (length != title_global<float>(kZero)) {
        const Lanes result = {{value.v[0] / length, value.v[1] / length, value.v[2] / length, value.v[3] / length}};
        std::memcpy(v, result.v, 16);
    }
}

// --- Localization text helpers ---

// The double-byte classifiers keep ECX and EDX (Loc_PopSemicolonToken reads both after the
// call); their C versions take (EAX, ECX) and return the whole EAX.
#define LOC_CLASSIFIER_ENTRY(name)                                     \
    PLATFORM_ASM_ENTRY(name##_entry,                                   \
                       "pushl %ecx\n"                                  \
                       "pushl %edx\n"                                  \
                       "pushl %ecx\n"                                  \
                       "pushl %eax\n" PLATFORM_ASM_CALL(name) "addl $8, %esp\n" \
                       "popl %edx\n"                                   \
                       "popl %ecx\n"                                   \
                       "ret")

namespace {

uint32_t with_al(uint32_t eax, bool value) { return (eax & 0xffffff00u) | (value ? 1u : 0u); }

}  // namespace

LOC_CLASSIFIER_ENTRY(Loc_IsKsc5601Punct)
uint32_t XB_CDECL Loc_IsKsc5601Punct(uint32_t code, uint32_t) {
    return with_al(code, code > 0xa1a0 && code < 0xa1ae);
}

LOC_CLASSIFIER_ENTRY(Loc_IsShiftJisPunct)
uint32_t XB_CDECL Loc_IsShiftJisPunct(uint32_t code, uint32_t) {
    return with_al(code, code >= 0x8140 && code < 0x8152);
}

LOC_CLASSIFIER_ENTRY(Loc_IsShiftJisChar)
uint32_t XB_CDECL Loc_IsShiftJisChar(uint32_t eax, uint32_t ecx) {
    const uint8_t lead = static_cast<uint8_t>(eax);
    const uint8_t trail = static_cast<uint8_t>(ecx);
    const bool lead_ok = (lead >= 0x81 && lead <= 0x9f) || (lead >= 0xe0 && lead <= 0xef);
    const bool trail_ok = (trail >= 0x40 && trail <= 0x7e) || (trail >= 0x80 && trail <= 0xfc);
    return with_al(eax, lead_ok && trail_ok);
}

LOC_CLASSIFIER_ENTRY(Loc_IsBig5Punct)
uint32_t XB_CDECL Loc_IsBig5Punct(uint32_t code, uint32_t) {
    return with_al(code, code >= 0xa140 && code < 0xa154);
}

LOC_CLASSIFIER_ENTRY(Loc_IsBig5Char)
uint32_t XB_CDECL Loc_IsBig5Char(uint32_t, uint32_t code) {
    const uint32_t high = code >> 8;
    const uint8_t lead = static_cast<uint8_t>(high);
    const uint8_t trail = static_cast<uint8_t>(code);
    const bool lead_ok = (lead >= 0xa1 && lead <= 0xc6) || (lead >= 0xc9 && lead <= 0xf9);
    const bool trail_ok = (trail >= 0x40 && trail <= 0x7e) || (trail >= 0xa1 && trail <= 0xfe);
    return with_al(high, lead_ok && trail_ok);
}

LOC_CLASSIFIER_ENTRY(Loc_IsKsc5601Char)
uint32_t XB_CDECL Loc_IsKsc5601Char(uint32_t eax, uint32_t ecx) {
    const uint8_t lead = static_cast<uint8_t>(eax);
    const uint8_t trail = static_cast<uint8_t>(ecx);
    return lead >= 0xb0 && lead <= 0xc8 && trail > 0xa0 && trail < 0xff ? 1 : 0;
}

LOC_CLASSIFIER_ENTRY(Loc_IsGb2312Char)
uint32_t XB_CDECL Loc_IsGb2312Char(uint32_t, uint32_t code) {
    const uint8_t lead = static_cast<uint8_t>(code >> 8);
    const uint8_t trail = static_cast<uint8_t>(code);
    return lead >= 0xa1 && lead <= 0xf7 && trail > 0xa0 && trail < 0xff ? 1 : 0;
}

#undef LOC_CLASSIFIER_ENTRY

namespace {

using StrStr = const char* XB_CDECL(const char*, const char*);
using StrRChr = char* XB_CDECL(const char*, int32_t);
using CharClass = int32_t XB_CDECL(int32_t);
using StrICmp = int32_t XB_CDECL(const char*, const char*);
using StrNICmp = int32_t XB_CDECL(const char*, const char*, uint32_t);

constexpr uint32_t kStrStr = 0x001efb10;
constexpr uint32_t kStrRChr = 0x001efd40;
constexpr uint32_t kIsSpace = 0x001eefa0;
constexpr uint32_t kToUpper = 0x001efd6d;
constexpr uint32_t kStrICmp = 0x001f1443;
constexpr uint32_t kStrNICmp = 0x001f1486;

void copy_string(char* dest, const char* src) {
    char c;
    do {
        c = *src++;
        *dest++ = c;
    } while (c);
}

}  // namespace

void XB_REGS1 XB_CDECL Loc_StripLineComment(char* line) {
    const char* const comment = &title_global<const char>(0x008333ac);  // "//"
    char* start = line;
    int32_t quotes = 0;
    char* found = const_cast<char*>(title_function<StrStr>(kStrStr)(start, comment));
    if (!found) return;
    for (;;) {
        const int32_t length = static_cast<int32_t>(found - start);
        for (int32_t i = 0; i < length; ++i)
            if (start[i] == '"') ++quotes;
        if (!(quotes & 1)) break;
        start = found + 1;
        found = const_cast<char*>(title_function<StrStr>(kStrStr)(start, comment));
        if (!found) return;
    }
    *found = 0;
    if (!*start) return;
    int32_t i = static_cast<int32_t>(std::strlen(start)) - 1;
    while (i >= 0) {
        if (!title_function<CharClass>(kIsSpace)(static_cast<signed char>(start[i]))) return;
        --i;
        start[i + 1] = 0;
    }
}

PLATFORM_ASM_ENTRY(Loc_MatchKeyword_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Loc_MatchKeyword) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL Loc_MatchKeyword(const char* keyword, char** cursor) {
    if (title_function<StrNICmp>(kStrNICmp)(keyword, *cursor, static_cast<uint32_t>(std::strlen(keyword))))
        return 0;
    char* p = *cursor + std::strlen(keyword);
    for (;;) {
        *cursor = p;
        if (*p != '\t' && *p != ' ') break;
        ++p;
    }
    return 1;
}

char* XB_REGS1 XB_CDECL Loc_StripFileExtension(const char* name) {
    char* const buffer = &title_global<char>(0x0098b220);
    copy_string(buffer, name);
    char* const dot = title_function<StrRChr>(kStrRChr)(buffer, '.');
    char* const backslash = title_function<StrRChr>(kStrRChr)(buffer, '\\');
    char* const slash = title_function<StrRChr>(kStrRChr)(buffer, '/');
    if (dot && (!backslash || dot > backslash) && (!slash || dot > slash)) *dot = 0;
    return buffer;
}

char* XB_REGS1 XB_CDECL Com_DirName(const char* path) {
    char* const buffer = &title_global<char>(0x0098b2a0);
    copy_string(buffer, path);
    char* const backslash = title_function<StrRChr>(kStrRChr)(buffer, '\\');
    char* last = title_function<StrRChr>(kStrRChr)(buffer, '/');
    if (backslash > last) last = backslash;
    if (last) *last = 0;
    return buffer;
}

int32_t XB_REGS3 XB_CDECL Q_strncmp(int32_t n, const char* s1, const char* s2) {
    int32_t left = n;
    for (;;) {
        const int32_t c1 = static_cast<signed char>(*s1++);
        const int32_t c2 = static_cast<signed char>(*s2++);
        if (left-- == 0) return 0;
        if (c1 != c2) return c1 >= c2 ? 1 : -1;
        if (!c1) return 0;
    }
}

char* XB_REGS1 XB_CDECL vtos(const float* v) {
    uint32_t& index = title_global<uint32_t>(0x0098bb68);
    const uint32_t slot = index;
    const int32_t y = static_cast<int32_t>(v[1]);
    char* const buffer = &title_global<char>(0x0098ba68 + slot * 32);
    index = (slot + 1) & 7;
    const int32_t z = static_cast<int32_t>(v[2]);
    const int32_t x = static_cast<int32_t>(v[0]);
    const uint32_t args[4] = {0x00832864, static_cast<uint32_t>(x), static_cast<uint32_t>(y),  // "(%i %i %i)"
                              static_cast<uint32_t>(z)};
    Com_sprintf(buffer, 32, args);
    return buffer;
}

const char* XB_CDECL Com_StringContains(const char* str1, const char* str2, int32_t casesensitive) {
    const int32_t length = static_cast<int32_t>(std::strlen(str1)) - static_cast<int32_t>(std::strlen(str2));
    for (int32_t i = 0; i <= length; ++i, ++str1) {
        int32_t j = 0;
        for (; str2[j]; ++j) {
            if (casesensitive) {
                if (str1[j] != str2[j]) break;
            } else {
                const int32_t a = title_function<CharClass>(kToUpper)(static_cast<signed char>(str1[j]));
                if (a != title_function<CharClass>(kToUpper)(static_cast<signed char>(str2[j]))) break;
            }
        }
        if (!str2[j]) return str1;
    }
    return nullptr;
}

int32_t XB_REGS1 XB_CDECL FS_IsCfgMenuOrDatFile(const char* ext) {
    if (*ext == '.') ++ext;
    if (!title_function<StrICmp>(kStrICmp)(ext, &title_global<const char>(0x00837fc0))) return 1;  // "cfg"
    if (!ext) return 0;
    if (!Q_stricmpn(ext, &title_global<const char>(0x00837fb8), 99999)) return 1;  // "menu"
    return Q_stricmpn(ext, &title_global<const char>(0x00837fb4), 99999) ? 0 : 1;  // "dat"
}

// --- File handles ---

namespace {

constexpr uint32_t kFsh = 0x009477a0;
constexpr uint32_t kBreak = 0x002938f0;

struct FileHandle {
    void* file;           // FILE* or unzFile
    int32_t unique;       // zip handle owns its unzFile
    int32_t handle_sync;
    uint8_t name[0x0c];
    int32_t zip_file;
    uint8_t rest[0x104];
};
static_assert(sizeof(FileHandle) == 0x120);

FileHandle& file_handle(int32_t f) { return title_global<FileHandle>(kFsh + static_cast<uint32_t>(f) * 0x120); }

using FileRw = int32_t XB_CDECL(void*, uint32_t, uint32_t, void*);
using FileOp = int32_t XB_CDECL(void*);
using FileSeek = int32_t XB_CDECL(void*, int32_t, int32_t);

int32_t crt_fread(void* buffer, uint32_t size, uint32_t count, void* file) {
    return title_function<FileRw>(0x001f019a)(buffer, size, count, file);
}
int32_t crt_fwrite(const void* buffer, uint32_t size, uint32_t count, void* file) {
    return title_function<FileRw>(0x001f047d)(const_cast<void*>(buffer), size, count, file);
}
int32_t crt_fflush(void* file) { return title_function<FileOp>(0x001efefe)(file); }
int32_t crt_ftell(void* file) { return title_function<FileOp>(0x001f062b)(file); }
int32_t crt_fseek(void* file, int32_t offset, int32_t origin) {
    return title_function<FileSeek>(0x001f06fb)(file, offset, origin);
}
int32_t crt_fclose(void* file) { return title_function<FileOp>(0x001f0790)(file); }

}  // namespace

void XB_REGS1 XB_CDECL FS_Flush(int32_t f) { crt_fflush(file_handle(f).file); }

int32_t XB_CDECL FS_Write(const void* buffer, int32_t len, int32_t f) {
    if (!f) return 0;
    FileHandle& handle = file_handle(f);
    void* const file = handle.file;
    int32_t remaining = len;
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);
    bool tried = false;
    while (remaining) {
        const int32_t written = crt_fwrite(buf, 1, static_cast<uint32_t>(remaining), file);
        if (!written) {
            if (tried) return 0;
            tried = true;
        } else if (written == -1) {
            return 0;
        }
        remaining -= written;
        buf += written;
    }
    if (handle.handle_sync) crt_fflush(file);
    return len;
}

int32_t XB_CDECL FS_Read(void* buffer, int32_t len, int32_t f) {
    if (!f) return 0;
    FileHandle& handle = file_handle(f);
    if (handle.zip_file)
        return unzReadCurrentFile(static_cast<UnzState*>(handle.file), buffer, static_cast<uint32_t>(len));
    int32_t remaining = len;
    uint8_t* buf = static_cast<uint8_t*>(buffer);
    bool tried = false;
    while (remaining) {
        const int32_t read = crt_fread(buf, 1, static_cast<uint32_t>(remaining), handle.file);
        if (!read) {
            if (tried) return len - remaining;
            tried = true;
        } else if (read == -1) {
            title_function<void XB_CDECL(int32_t, const char*)>(kBreak)(0, &title_global<const char>(0x00837f94));
        }
        remaining -= read;
        buf += read;
    }
    return len;
}

void XB_CDECL FS_FCloseFile(int32_t f) {
    FileHandle& handle = file_handle(f);
    if (handle.zip_file) {
        unzCloseCurrentFile(static_cast<UnzState*>(handle.file));
        if (handle.unique) unzClose(static_cast<UnzState*>(handle.file));
    } else if (handle.file) {
        crt_fclose(handle.file);
    }
    Com_Memset(reinterpret_cast<uint8_t*>(&handle), 0, 0x120);
}

int32_t XB_REGS1 XB_CDECL FS_filelength(int32_t f) {
    FileHandle& handle = file_handle(f);
    if (handle.zip_file) return *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(handle.file) + 0x44);
    void* const file = handle.file;
    const int32_t pos = crt_ftell(file);
    crt_fseek(file, 0, 2);
    const int32_t end = crt_ftell(file);
    crt_fseek(file, pos, 0);
    return end;
}

// --- Misc ---

// Callers keep XMM2-XMM7 (and XMM1, the original only reads it) across the call.
PLATFORM_ASM_ENTRY(Rand_StepSeed_entry,
                   "subl $112, %esp\n"
                   "movups %xmm1, 0(%esp)\n"
                   "movups %xmm2, 16(%esp)\n"
                   "movups %xmm3, 32(%esp)\n"
                   "movups %xmm4, 48(%esp)\n"
                   "movups %xmm5, 64(%esp)\n"
                   "movups %xmm6, 80(%esp)\n"
                   "movups %xmm7, 96(%esp)\n"
                   "subl $8, %esp\n"
                   "movss %xmm1, 4(%esp)\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(Rand_StepSeed) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $8, %esp\n"
                   "movups 0(%esp), %xmm1\n"
                   "movups 16(%esp), %xmm2\n"
                   "movups 32(%esp), %xmm3\n"
                   "movups 48(%esp), %xmm4\n"
                   "movups 64(%esp), %xmm5\n"
                   "movups 80(%esp), %xmm6\n"
                   "movups 96(%esp), %xmm7\n"
                   "addl $112, %esp\n"
                   "ret")

float XB_CDECL Rand_StepSeed(float a, float b) {
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    const uint32_t r = seed >> 17;  // below 2^15: the 2^32 correction never applies
    const float rf = platform_x87_int_to_float(static_cast<int32_t>(r));
    float value = a - b;
    value = value * rf;
    value = value / title_global<float>(0x0083ebc8);  // 32768
    return value + b;
}

void XB_CDECL Netchan_Init() {
    const char* const zero = &title_global<const char>(0x0082e098);  // "0"
    title_global<uint8_t*>(0x00927c30) = Cvar_Get(&title_global<const char>(0x00833c5c), zero, 0x100);  // showpackets
    title_global<uint8_t*>(0x00935118) = Cvar_Get(&title_global<const char>(0x00833c50), zero, 0x100);  // showdrop
    uint32_t* loopbacks = &title_global<uint32_t>(0x00928590);
    for (uint32_t i = 0; i < 0x49e; ++i) loopbacks[i] = 0;
}

uint8_t* XB_CDECL IntHashMap_FindValue(int32_t key) {
    uint32_t node = 0;
    IntHashMap_Find(&node, &key, &title_global<const uint8_t>(0x0095fe00));
    if (node == title_global<uint32_t>(0x0095fe08)) return nullptr;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(node)) + 0xc;
}

PLATFORM_ASM_ENTRY(Profile_ApplySkill_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Profile_ApplySkill) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Profile_ApplySkill(const uint8_t* profile) {
    const volatile int32_t* skill = reinterpret_cast<const volatile int32_t*>(profile + 0x20);
    if (*skill == 0) SV_SetSkillHard();
    if (*skill == 1) SV_SetSkillMedium();
    if (*skill == 2) SV_SetSkillEasy();
}

namespace {

const char* cmd_arg(int32_t argc, int32_t index) {
    return static_cast<uint32_t>(index) < static_cast<uint32_t>(argc) ? title_global<const char*>(0x0092df90 + index * 4)
                                                                      : &title_global<const char>(0x0082dfeb);  // ""
}

void cvar_set2(const char* name, const char* value) {
    title_function<uint32_t XB_CDECL(const char*, const char*, uint32_t)>(0x0029c030)(name, value, 0);  // Cvar_Set2
}

}  // namespace

void XB_CDECL Cvar_Toggle_f() {
    const int32_t argc = title_global<int32_t>(0x00934248);  // cmd_argc
    if (argc < 2) return;
    if (argc == 2) {
        const uint8_t* var = Cvar_FindVar(title_global<const char*>(0x0092df94));
        float value = 0.0f;
        if (var) value = *reinterpret_cast<const float*>(var + 0x1c);
        const int32_t now = title_global<int32_t>(0x00934248);
        const uint32_t toggled = static_cast<int32_t>(value) == 0 ? 1 : 0;
        const char* name = cmd_arg(now, 1);
        const uint32_t args[2] = {0x0082ded4, toggled};  // "%i"
        cvar_set2(name, va(args));
        return;
    }
    const uint8_t* var = Cvar_FindVar(cmd_arg(argc, 1));
    const char* current = var ? *reinterpret_cast<const char* const*>(var + 4) : &title_global<const char>(0x0082dfeb);
    const int32_t count = title_global<int32_t>(0x00934248);
    for (int32_t i = 2; i < count - 1; ++i) {
        if (!std::strcmp(current, cmd_arg(count, i))) {
            const char* next = cmd_arg(count, i + 1);
            cvar_set2(cmd_arg(count, 1), next);
            return;
        }
    }
    cvar_set2(cmd_arg(count, 1), cmd_arg(count, 2));
}

// --- Angles from vectors ---

namespace {

// The title CRT's __CIsqrt of an x87 value, stored as float.
float vec_ci_sqrt(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// FLD/FABS/FSTP DWORD (a signalling NaN comes back quiet).
float x87_abs(float x) {
    float result;
    __asm__("flds %1\n\tfabs\n\tfstps %0" : "=m"(result) : "m"(x) : "st");
    return result;
}

// t + t^3 / 6 + (0.075 t^5 + 0.054 t^7) on SSE (t < 0.5).
float asin_series_near(float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float t5 = t3 * t2;
    const float t7 = t5 * t2;
    return ((t3 * title_global<float>(0x0083ee9c)) +
            ((t5 * title_global<float>(0x0083ee98)) + (t7 * title_global<float>(0x0083ee94)))) +
           t;
}

// t >= 0.5: s = __CIsqrt(|(1 - t) * 0.5|) (x87), then (((s^7 c - 0.15 s^5) - s^3 / 3) - 2 s) + pi / 2.
float asin_series_far(float t) {
    const float u = static_cast<float>(
        (static_cast<long double>(title_global<float>(0x0083eb20)) - t) * title_global<float>(0x0083eb24));
    const float s = vec_ci_sqrt(x87_abs(u));
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float two_s = s * title_global<float>(0x0083eba8);
    const float s5 = s3 * s2;
    const float s7 = s5 * s2;
    return (((s7 * title_global<float>(0x0083ee8c) - s5 * title_global<float>(0x0083ed2c)) -
             s3 * title_global<float>(0x0083ee90)) -
            two_s) +
           title_global<float>(0x0083ee88);
}

float asin_series(float t) {
    return title_global<float>(0x0083eb24) > t ? asin_series_near(t) : asin_series_far(t);
}

// atan2(b, a) in radians from the two components (a = x for the yaw, the xy length for the pitch);
// length = __CIsqrt of the x87 sum b^2 + a^2, 0 when |b| + |a| is 0.
float atan2_series(float a, float b) {
    const float abs_a = x87_abs(a);
    const float abs_b = x87_abs(b);
    if (static_cast<long double>(abs_b) + abs_a == 0.0L) return 0.0f;  // FUCOMIP (a NaN goes on)
    const float length = vec_ci_sqrt(static_cast<long double>(b) * b + static_cast<long double>(a) * a);
    const float inv = static_cast<float>(static_cast<long double>(title_global<float>(0x0083eb20)) / length);
    float r;
    if (abs_b > abs_a)
        r = title_global<float>(0x0083ed0c) - asin_series(inv * abs_a);  // pi / 2 -
    else
        r = asin_series(inv * abs_b);
    const float zero = 0.0f;
    if (zero > a) r = title_global<float>(0x0083ed00) - r;  // pi -
    if (zero > b) r = zero - r;
    return r;
}

// The yaw before the wrap: degrees in (-180, 180], 0 for x = y = 0 (UCOMISS + JP: exactly 0).
float yaw_degrees(const float* v) {
    if (v[1] == 0.0f && v[0] == 0.0f) return 0.0f;
    const float r = atan2_series(v[0], v[1]);
    return (r * title_global<float>(0x0083eb60)) / title_global<float>(0x0083ed00);  // * 180 / pi
}

// The pitch before the wrap: (-180, 180] (r * -180 / pi); x = y = 0 gives `vertical` (z > 0) or 90.
float pitch_degrees(const float* v, uint32_t vertical) {
    if (v[1] == 0.0f && v[0] == 0.0f) return v[2] > 0.0f ? title_global<float>(vertical) : title_global<float>(0x0083eb64);
    const float a = vec_ci_sqrt(static_cast<long double>(v[1] * v[1] + v[0] * v[0]));
    const float r = atan2_series(a, v[2]);
    return (r * title_global<float>(0x0083ebd0)) / title_global<float>(0x0083ed00);
}

float wrap_360(float degrees) {
    if (0.0f > degrees) degrees = degrees + title_global<float>(0x0083ebcc);
    return degrees;
}

}  // namespace

PLATFORM_ASM_ENTRY(vectoangles_2_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(vectoangles_2) "addl $8, %esp\n"
                   "ret")

void XB_CDECL vectoangles_2(const float* v, float* angles) {
    const float yaw = yaw_degrees(v);
    const float pitch = pitch_degrees(v, 0x0083efc8);  // -90
    angles[0] = pitch;
    angles[1] = yaw;
    angles[2] = 0.0f;
}

PLATFORM_ASM_ENTRY(vectoangles_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(vectoangles) "addl $8, %esp\n"
                   "ret")

void XB_CDECL vectoangles(const float* v, float* angles) {
    const float yaw = v[1] == 0.0f && v[0] == 0.0f ? 0.0f : wrap_360(yaw_degrees(v));
    float pitch = pitch_degrees(v, 0x0083efcc);  // 270
    if (!(v[1] == 0.0f && v[0] == 0.0f)) pitch = wrap_360(pitch);
    angles[0] = pitch;
    angles[1] = yaw;
    angles[2] = 0.0f;
}

#define XMM0_RESULT_TAIL(pop) "addl $" #pop ", %esp\n" "subl $4, %esp\n" "fstps (%esp)\n" "movss (%esp), %xmm0\n" "addl $4, %esp\n" "ret"

PLATFORM_ASM_ENTRY(vectopitch_entry, "pushl %esi\n" PLATFORM_ASM_CALL(vectopitch) XMM0_RESULT_TAIL(4))

float XB_CDECL vectopitch(const float* v) { return pitch_degrees(v, 0x0083efc8); }

PLATFORM_ASM_ENTRY(vectoyaw_2_entry, "pushl %ecx\n" PLATFORM_ASM_CALL(vectoyaw_2) XMM0_RESULT_TAIL(4))

float XB_CDECL vectoyaw_2(const float* v) { return yaw_degrees(v); }

PLATFORM_ASM_ENTRY(vectoyaw_entry, "pushl %ecx\n" PLATFORM_ASM_CALL(vectoyaw) XMM0_RESULT_TAIL(4))

float XB_CDECL vectoyaw(const float* v) {
    if (v[1] == 0.0f && v[0] == 0.0f) return 0.0f;  // returns before the wrap
    return wrap_360(yaw_degrees(v));
}

PLATFORM_ASM_ENTRY(Plane_PitchAlongYaw_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Plane_PitchAlongYaw) XMM0_RESULT_TAIL(8))

float XB_CDECL Plane_PitchAlongYaw(const float* n, float yaw) {
    float s, c;  // c lands in the yaw argument slot in the original (arg_bytes 4)
    FastSinCos(&s, 0, &c, platform_x87_mul(yaw, title_global<float>(0x0083edd8)));
    const float zero = 0.0f;
    const float inv = title_global<float>(0x0083eb20) / (((n[0] * n[0]) + (n[1] * n[1])) + (n[2] * n[2]));
    const float d = (((n[0] * c) + (s * n[1])) + (n[2] * zero)) * inv;
    const float px = (n[0] * inv) * d;
    const float py = (inv * n[1]) * d;
    const float pz = (inv * n[2]) * d;
    float p[3] = {c - px, s - py, zero - pz};
    return Vec3_PitchDegrees(p);
}

PLATFORM_ASM_ENTRY(YawToAxis_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(YawToAxis) "addl $8, %esp\n"
                   "ret")

void XB_CDECL YawToAxis(float* axis, float yaw) {
    float s, c;  // c lands in the yaw argument slot in the original (arg_bytes 4)
    FastSinCos(&s, 0, &c, platform_x87_mul(yaw, title_global<float>(0x0083edd8)));
    const float zero = 0.0f;
    if (axis) {
        axis[0] = c;
        axis[1] = s;
        axis[2] = zero;
    }
    const float minus_c = zero - c;
    axis[8] = title_global<float>(0x0083eb20);
    axis[3] = zero - s;
    const float row1_y = zero - minus_c;
    const float row1_z = zero - zero;
    axis[6] = zero;
    axis[7] = zero;
    axis[4] = row1_y;
    axis[5] = row1_z;
}

namespace {

void angles_to_axis_left(const float* angles, float* axis) {
    uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(axis))};
    float right[3];
    AnglesToAxis(args, right, axis + 6);
    const float zero = 0.0f;
    axis[3] = zero - right[0];
    axis[4] = zero - right[1];
    axis[5] = zero - right[2];
}

}  // namespace

void XB_REGS1 XB_CDECL AnglesToAxisLeft2(const float* angles, float* axis) { angles_to_axis_left(angles, axis); }

void XB_REGS1 XB_CDECL AnglesToAxisLeft(const float* angles, float* axis) { angles_to_axis_left(angles, axis); }

PLATFORM_ASM_ENTRY(AnglesToAxisVec4_entry,
                   "pushl %ebx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(AnglesToAxisVec4) "addl $8, %esp\n"
                   "ret")

void XB_CDECL AnglesToAxisVec4(const float* angles, float* out) {
    float axis[9];
    uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(axis))};
    AnglesToAxis(args, axis + 3, axis + 6);
    const float zero = 0.0f;
    out[0] = axis[0];
    out[1] = axis[1];
    out[2] = axis[2];
    out[4] = zero - axis[3];
    out[5] = zero - axis[4];
    out[6] = zero - axis[5];
    out[8] = axis[6];
    out[9] = axis[7];
    out[10] = axis[8];
}

float* XB_REGS3 XB_CDECL Mat44_GetAxis(float* out, uint32_t, const float* m) {
    for (int32_t row = 0; row < 3; ++row) {  // whole row loaded (MOVAPS through a stack copy), 3 lanes stored
        uint32_t words[4];
        std::memcpy(words, m + 4 * row, 16);
        std::memcpy(out + 4 * row, words, 12);
    }
    return out;
}

const float* XB_REGS3 XB_CDECL Mat43_UntransformPoint(const float* m, uint32_t, float* out, const float* p) {
    const float d1 = p[1] - m[10];
    const float d2 = p[2] - m[11];
    const float d0 = p[0] - m[9];
    out[0] = ((m[2] * d2) + (m[1] * d1)) + (m[0] * d0);
    out[1] = ((m[5] * d2) + (m[4] * d1)) + (m[3] * d0);
    out[2] = ((m[8] * d2) + (m[7] * d1)) + (m[6] * d0);
    return m;
}

float* XB_CDECL RandomPointInUnitCircle(float* x, float* y) {
    const auto rand = title_function<int32_t XB_CDECL()>(0x001efcc0);  // _rand
    const float range = title_global<float>(0x0083ebc8);                // 32768
    const float angle = (static_cast<float>(rand()) / range) * title_global<float>(0x0083ebcc);
    const float radius = static_cast<float>(rand()) / range;
    const float radians = (angle * title_global<float>(0x0083ed00)) / title_global<float>(0x0083eb60);
    float s, c;
    FastSinCos(&s, 0, &c, radians);
    *x = c * radius;
    *y = s * radius;
    return x;
}

}  // namespace game
