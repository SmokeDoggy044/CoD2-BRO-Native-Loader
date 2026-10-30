#include "qcommon_fs.h"

#include <cstring>

#include "cm.h"
#include "fx.h"
#include "lanes.h"
#include "nal_math.h"
#include "fx_load.h"
#include "g_scr_runtime.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "sv.h"
#include "tr_main.h"
#include "stream.h"
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
                   "pushl 16(%esp)\n" PLATFORM_ASM_CALL(MatrixMultiply3x3) "addl $12, %esp\n"
                   "popl %edx\n"
                   "ret")

void XB_CDECL MatrixMultiply3x3(const float* a, const float* b, float* out) {
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

// --- Pak zones and memory contexts ---

namespace {

template <typename T>
T& pk(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* pk_ptr(uint8_t* base, uint32_t offset) {
    return reinterpret_cast<uint8_t*>(uintptr_t{pk<uint32_t>(base, offset)});
}

// MSVC vector at(): an index past the size reads element 0.
uint8_t* vector_at(uint8_t* vec, uint32_t i) {
    const uint32_t index = i < pk<uint32_t>(vec, 0x10) ? i : 0;
    return pk_ptr(pk_ptr(vec, 0x14), index * 4);
}

// The first node of the loaded list and its next: both 0 for an empty list.
uint8_t* loaded_first(uint8_t* pak, uint8_t** next) {
    uint8_t* node = pk_ptr(pak, 0x368);
    *next = node ? pk_ptr(node, 0) : nullptr;
    if (node == pak + 0x36c) {
        *next = nullptr;
        node = nullptr;
    }
    return node;
}

uint8_t* pak_slot(uint8_t* pak, int32_t slot) { return pk_ptr(pak, 0x38 + static_cast<uint32_t>(slot) * 4); }

}  // namespace

uint8_t* XB_THISCALL MemContext_FindById(uint8_t* pak, int32_t id) {
    uint8_t* zones = pk_ptr(pak, 0x18);
    if (!zones || id == -1) return nullptr;
    const uint32_t cache = 0x1c4 + static_cast<uint32_t>(id) * 4;
    if (pk<uint32_t>(pak, cache)) return pk_ptr(pak, cache);
    uint8_t* record = pak_slot(pak, id);
    if (record && pk<uint32_t>(record, 0x78)) {
        const uint32_t context = ZoneRecord_GetContext(record);
        pk<uint32_t>(pak, cache) = context;
        return reinterpret_cast<uint8_t*>(uintptr_t{context});
    }
    const int32_t count = pk<int32_t>(zones, 0x10);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* zone = vector_at(zones, static_cast<uint32_t>(i));
        if (pk<int32_t>(zone, 0x28) == id) {
            pk<uint32_t>(pak, cache) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zone));
            return zone;
        }
    }
    return nullptr;
}

uint8_t* XB_REGS3 XB_STDCALL MemContextScope_Enter(uint32_t flag, uint32_t, int32_t id, uint8_t* scope) {
    pk<int32_t>(scope, 0) = id;
    if (id != -1) {
        title_global<uint8_t>(0x0095f5d1) = static_cast<uint8_t>(flag);
        const uint8_t old = title_global<uint8_t>(0x0095f5d0);
        uint8_t* context = reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x0095f638)});
        title_global<uint8_t>(0x0095f5d0) = 1;
        scope[4] = old;
        MemContext_Push(context, static_cast<uint32_t>(id));
    }
    return scope;
}

PLATFORM_ASM_ENTRY(Pak_CollectLoadedZoneIds_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Pak_CollectLoadedZoneIds) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Pak_CollectLoadedZoneIds(int32_t* out) {
    uint8_t* pak = reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x0095f638)});
    if (!pak) return;
    uint8_t* next;
    uint8_t* node = loaded_first(pak, &next);
    while (next) {
        if (out[0] < 0x20) {
            out[out[0] + 1] = pk<int32_t>(node, 0x74);
            ++out[0];
        }
        node = next;
        next = pk_ptr(node, 0);
    }
}

PLATFORM_ASM_ENTRY(Pak_AssignZoneIdsByName_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Pak_AssignZoneIdsByName) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Pak_AssignZoneIdsByName(uint8_t* pak) {
    const auto strstr_ = title_function<const char* XB_CDECL(const char*, const char*)>(0x001efb10);  // _strstr
    uint8_t* next;
    uint8_t* node = loaded_first(pak, &next);
    while (next) {
        const int32_t id = pk<int32_t>(node, 0x74);
        const char* name = reinterpret_cast<const char*>(node + 8);
        for (uint32_t i = 0; i < pk<uint32_t>(pk_ptr(pak, 0x18), 0x10); ++i) {
            uint8_t* zone = vector_at(pk_ptr(pak, 0x18), i);
            if (!strstr_(reinterpret_cast<const char*>(pk_ptr(zone, 8)), name)) continue;
            pk<int32_t>(vector_at(pk_ptr(pak, 0x18), i), 0x28) = id;
            if (pk<uint32_t>(vector_at(pk_ptr(pak, 0x18), i), 0x2c) == 0) ++pk<uint32_t>(vector_at(pk_ptr(pak, 0x18), i), 0x2c);
        }
        node = next;
        next = pk_ptr(node, 0);
    }
}

PLATFORM_ASM_ENTRY(Pak_FindNextZoneToLoad_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Pak_FindNextZoneToLoad) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Pak_FindNextZoneToLoad(uint8_t* pak) {
    uint8_t* best = nullptr;
    float best_priority = title_global<float>(0x0082eed0);  // FLT_MAX
    for (uint32_t i = 0; i < pk<uint32_t>(pk_ptr(pak, 0x18), 0x10); ++i) {
        uint8_t* zone = vector_at(pk_ptr(pak, 0x18), i);
        const int32_t slot = pk<int32_t>(zone, 0x28);
        if (slot != -1) {
            uint8_t* record = pak_slot(pak, slot);
            if (record && pk<int32_t>(record, 0xdc) != 3) continue;
        }
        bool take = !best;
        if (!take) {
            if (best_priority > Zone_UpdatePriority(zone)) {
                take = true;
            } else {
                if (!(Zone_UpdatePriority(zone) == best_priority)) continue;  // UCOMISS + JP
                if (Zone_FindUnloadedChild(zone, pak) != zone) continue;
                if (Zone_FindUnloadedChild(best, pak) == best) continue;
                take = true;
            }
        }
        best_priority = Zone_UpdatePriority(zone);
        best = zone;
    }
    if (!best) return nullptr;
    const int32_t slot = pk<int32_t>(best, 0x28);
    if (slot != -1) {
        uint8_t* record = pak_slot(pak, slot);
        if (record && pk<int32_t>(record, 0xdc) == 0) return best;
    }
    uint8_t* child = Zone_FindUnloadedChild(best, pak);
    return child ? child : best;
}

void XB_REGS2 XB_CDECL Pak_UpdateLoadProgress(uint32_t, uint8_t* pak) {
    const uint32_t callback = pk<uint32_t>(pak, 0x35c);
    if (!callback) return;
    uint8_t* counts = reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x0095f62c)});
    const float total = platform_x87_unsigned_to_float(pk<uint32_t>(counts, 0x10));
    const float one = title_global<float>(0x0083eb20);
    const float half = title_global<float>(0x0083eb24);
    float done = 0.0f;
    uint8_t* next;
    uint8_t* node = loaded_first(pak, &next);
    while (next) {
        if (pk<int32_t>(node, 0xdc) != 1) {
            done = LoadBits_Progress(reinterpret_cast<const uint32_t*>(node + 0x54)) + done;
        } else {
            const int32_t state = pk<int32_t>(node, 0xe0);
            if (state == 2 || state == 3) {
                uint8_t* table = pk_ptr(pk_ptr(node, 0x7c), 8);
                uint8_t* entry = table + pk<uint32_t>(node, 0x80) * 24;
                const int32_t n = *reinterpret_cast<const uint16_t*>(entry + 0xc);
                uint8_t* items = pk_ptr(entry, 0x10);
                for (int32_t k = 0; k < n; ++k) {
                    uint8_t* item = items + k * 0x1c;
                    if (!(item[0xc] & 4)) continue;
                    const uint32_t full = pk<uint32_t>(counts, 0x14);
                    const uint32_t value = pk<uint32_t>(item, 0x18);
                    if (value == full)
                        done = done + one;
                    else if (value == full >> 1)
                        done = done + half;
                }
            }
        }
        node = next;
        next = pk_ptr(node, 0);
    }
    const float scale = (one - title_global<float>(0x008d9d3c)) - title_global<float>(0x008d9d40);
    title_function<void XB_CDECL(float)>(callback)(scale * (done / total));
}

PLATFORM_ASM_ENTRY(Pak_Init_entry, "pushl %esi\n" PLATFORM_ASM_CALL(Pak_Init) "addl $4, %esp\n"
                                   "ret")

uint8_t* XB_CDECL Pak_Init(uint8_t* pak) {
    pk<int32_t>(pak, 0x28) = -1;
    pk<int32_t>(pak, 0x2c) = -1;
    pk<int32_t>(pak, 0x34) = -1;
    pk<uint32_t>(pak, 0x18) = 0;
    pk<uint32_t>(pak, 0x350) = 0;
    pk<uint32_t>(pak, 0x35c) = 0;
    pak[0x360] = 0;
    pk<uint32_t>(pak, 0x36c) = 0;
    pk<uint32_t>(pak, 0x364) = 0;
    pk<uint32_t>(pak, 0x368) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pak + 0x36c));
    pk<uint32_t>(pak, 0x370) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pak + 0x368));
    pk<uint32_t>(pak, 0x374) = 0;
    pk<uint32_t>(pak, 0x578) = 0;
    pk<uint32_t>(pak, 0) = 0;
    pk<uint32_t>(pak, 4) = 0;
    pk<uint32_t>(pak, 8) = 0;
    pk<uint32_t>(pak, 0xc) = 0;
    const auto text = [](uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); };
    Cmd_AddCommand(text(0x008383a0), 0x0028cd40);  // "PakRender"
    pk<uint32_t>(pak, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Cvar_Get(text(0x00838394), text(0x0082e098), 0)));
    pk<uint32_t>(pak, 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Cvar_Get(text(0x00838388), text(0x0082e098), 0)));
    pk<uint32_t>(pak, 0xc) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Cvar_Get(text(0x0083837c), text(0x0082e098), 0)));
    Pak_Clear(pak);
    title_global<uint32_t>(0x0095f60c) = 0x9000;
    title_global<uint32_t>(0x0095f608) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Stream_Alloc(0x9000)));
    for (uint32_t k = 0; k < 0x63; ++k) {
        pk<uint32_t>(pak, 0x38 + 4 * k) = 0;
        pk<uint32_t>(pak, 0x1c4 + 4 * k) = 0;
    }
    const int32_t count = title_global<int32_t>(0x00988780);
    if (count < 0x20) {
        title_global<uint32_t>(0x00988784 + static_cast<uint32_t>(count) * 4) = 0x0001b9a0;
        ++title_global<int32_t>(0x00988780);
    }
    return pak;
}

namespace {

void cell_meshes_for_zone(uint8_t* ctx, int32_t id, uint32_t release) {
    int32_t list = pk<int32_t>(ctx, 0x1cc);
    while (list != -1) {
        uint8_t* cells = pk_ptr(ctx, 4 + static_cast<uint32_t>(list) * 4);
        for (uint32_t i = 0; i < pk<uint32_t>(cells, 0x10); ++i) {
            uint8_t* cell = vector_at(cells - 0, i);
            if (pk<int32_t>(pk_ptr(cell, 0x24), 0x28) != id) continue;
            for (uint32_t j = 0; j < pk<uint32_t>(cell, 0x30); ++j) {
                const uint32_t index = j < pk<uint32_t>(cell, 0x30) ? j : 0;
                uint8_t* mesh = pk_ptr(pk_ptr(cell, 0x34), index * 4);
                R_LoadCellMeshes(pk<uint32_t>(mesh, 0x20), release);
            }
        }
        list = pk<int32_t>(pk_ptr(ctx, 4 + static_cast<uint32_t>(list) * 4), 0x3c);
    }
}

}  // namespace

void XB_STDCALL Pak_ReleaseCellMeshesForZone(uint8_t* ctx, int32_t id) { cell_meshes_for_zone(ctx, id, 1); }

void XB_STDCALL Pak_LoadCellMeshesForZone(uint8_t* ctx, int32_t id) { cell_meshes_for_zone(ctx, id, 0); }

namespace {

// TEST BYTE [p] every 32 bytes over min(size, 4096) rounded up (reads only).
void prefetch_touch(const uint8_t* data, int32_t size) {
    if (size > 0x1000) size = 0x1000;
    uint32_t lines = static_cast<uint32_t>(size + 0x1f) >> 5;
    for (const volatile uint8_t* p = data; lines; --lines, p += 0x20) (void)*p;
}

}  // namespace

void XB_REGS1 XB_CDECL Com_Prefetch(uint32_t mode, const uint8_t* data, int32_t size) {
    if (mode == 0 || mode == 2) prefetch_touch(data, size);
}

void XB_CDECL Com_MemcpyPrefetch(uint8_t* dst, const uint8_t* src, int32_t count) {
    prefetch_touch(src, count);
    if (count == 0) return;
    const auto copy32 = [](uint8_t* d, const uint8_t* s) {
        uint32_t w;
        std::memcpy(&w, s, 4);
        std::memcpy(d, &w, 4);
    };
    if (count >= 0x20) {
        for (int32_t off = (count & ~0x1f) - 0x20; off >= 0; off -= 0x20) {
            for (int32_t k = 0; k < 0x20; k += 8) {  // two loads, then two stores
                uint32_t a, b;
                std::memcpy(&a, src + off + k, 4);
                std::memcpy(&b, src + off + k + 4, 4);
                std::memcpy(dst + off + k, &a, 4);
                std::memcpy(dst + off + k + 4, &b, 4);
            }
        }
        src += count & ~0x1f;
        dst += count & ~0x1f;
        count &= 0x1f;
        if (count == 0) return;
    }
    if (count >= 0x10) {
        for (int32_t k = 0; k < 0x10; k += 4) copy32(dst + k, src + k);
        count -= 0x10;
        src += 0x10;
        dst += 0x10;
    }
    if (count >= 8) {
        copy32(dst, src);
        copy32(dst + 4, src + 4);
        count -= 8;
        src += 8;
        dst += 8;
    }
    if (count >= 4) {
        copy32(dst, src);
        src += 4;
        count -= 4;
        dst += 4;
    }
    if (count >= 2) {
        uint16_t h;
        std::memcpy(&h, src, 2);
        std::memcpy(dst, &h, 2);
        if (count >= 3) dst[2] = src[2];
    } else if (count >= 1) {
        dst[0] = src[0];
    }
}

uint32_t XB_CDECL Com_SafeMode() {
    const auto line = [](int32_t i) {
        return reinterpret_cast<char*>(uintptr_t{title_global<uint32_t>(0x0093fcf8 + static_cast<uint32_t>(i) * 4)});
    };
    const char* const empty = reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});
    for (int32_t i = 0; i < title_global<int32_t>(0x009403e0); ++i) {  // com_numConsoleLines
        Cmd_TokenizeString(line(i), 0);
        const char* arg = title_global<uint32_t>(0x00934248)  // cmd_argc
                              ? reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x0092df90)})
                              : empty;
        if (arg && !Q_stricmpn(arg, reinterpret_cast<const char*>(uintptr_t{0x00837d84}), 99999)) {  // "safe"
            line(i)[0] = 0;
            return 1;
        }
        arg = title_global<uint32_t>(0x00934248) ? reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x0092df90)})
                                                 : empty;
        if (arg && !Q_stricmpn(arg, reinterpret_cast<const char*>(uintptr_t{0x00837d74}), 99999)) {  // "cvar_restart"
            line(i)[0] = 0;
            return 1;
        }
    }
    return title_global<uint32_t>(0x0095f594);
}

PLATFORM_ASM_ENTRY(Com_GetRealEvent_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Com_GetRealEvent) "addl $4, %esp\n"
                   "ret")

SysEvent* XB_CDECL Com_GetRealEvent(SysEvent* out) {
    const auto error = title_function<void XB_CDECL(uint32_t, const char*)>(0x002938f0);  // __break
    const char* const read_error = reinterpret_cast<const char*>(uintptr_t{0x00837f34});
    const char* const write_error = reinterpret_cast<const char*>(uintptr_t{0x00837d40});
    uint8_t* journal = reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x00940a00)});
    const int32_t file = title_global<int32_t>(0x0093fce0);
    SysEvent ev;
    if (pk<int32_t>(journal, 0x20) == 2) {
        if (FS_Read(&ev, 0x18, file) != 0x18) error(0, read_error);
        if (ev.ptr_length) {
            const int32_t length = ev.ptr_length;
            uint8_t* buffer = title_function<uint8_t* XB_CDECL(int32_t)>(0x00042c10)(length);  // Z_Malloc
            Com_Memset(buffer, 0, static_cast<uint32_t>(length));
            ev.ptr = buffer;
            if (FS_Read(buffer, ev.ptr_length, title_global<int32_t>(0x0093fce0)) != ev.ptr_length) error(0, read_error);
        }
    } else {
        SysEvent queued;
        ev = *Sys_GetEvent(&queued);
        if (pk<int32_t>(journal, 0x20) == 1) {
            if (FS_Write(&ev, 0x18, title_global<int32_t>(0x0093fce0)) != 0x18) error(0, write_error);
            if (ev.ptr_length && FS_Write(ev.ptr, ev.ptr_length, title_global<int32_t>(0x0093fce0)) != ev.ptr_length)
                error(0, write_error);
        }
    }
    *out = ev;
    return out;
}

uint32_t XB_CDECL FS_FileExists(const char* name) {
    char path[0x80];
    FS_BuildOSPath(reinterpret_cast<const char*>(uintptr_t{0x0093fc30}), 0,  // fs_gamedir
                   reinterpret_cast<const char*>(uintptr_t{pk<uint32_t>(
                       reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x00947798)}), 4)}),  // fs_homepath
                   name, path, 0);
    void* f = title_function<void* XB_CDECL(const char*, const char*)>(0x001effb3)(
        path, reinterpret_cast<const char*>(uintptr_t{0x00838080}));  // _fopen "rb"
    if (!f) return 0;
    title_function<int32_t XB_CDECL(void*)>(0x001f0790)(f);  // _fclose
    return 1;
}

void XB_CDECL Cmd_Where_f(uint8_t* ent) {
    if (pk<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x0093fcd8)}), 0x20)) return;
    const float* origin = reinterpret_cast<const float*>(pk_ptr(ent, 0x23c));
    if (!origin) return;
    const uint32_t handle = pk<uint32_t>(ent, 0x224);
    const uint32_t args[2] = {0x0083bcbc, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vtos(origin)))};
    SV_GameSendServerCommand(va(args), handle);
    const char* text = vtos(reinterpret_cast<const float*>(pk_ptr(ent, 0x23c)));
    char* dst = &title_global<char>(0x00947f18);
    char c;
    do {
        c = *text++;
        *dst++ = c;
    } while (c);
}

// --- Net channel, loopback, parsing, localization, profiles ---

namespace {

uint8_t* loopback_of(uint32_t sock) { return &title_global<uint8_t>(0x00928590 + (sock ^ 1) * 0x93c); }

}  // namespace

PLATFORM_ASM_ENTRY(Netchan_Process_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Netchan_Process) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Netchan_Process(uint8_t* chan, uint8_t* msg) {
    const auto shown = [] {
        return pk<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x00935118)}), 0x20) ||
               pk<uint32_t>(reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x00927c30)}), 0x20);
    };
    pk<int32_t>(msg, 0x10) = 0;
    const int32_t sequence = MSG_ReadLong(msg);
    if (pk<int32_t>(chan, 0) == 1) pk<int32_t>(msg, 0x10) += 2;
    if (sequence <= pk<int32_t>(chan, 0x20)) {
        if (shown()) NET_AdrToString(chan + 8);
        return 0;
    }
    const int32_t dropped = sequence - pk<int32_t>(chan, 0x20) - 1;
    pk<int32_t>(chan, 4) = dropped;
    if (dropped > 0 && shown()) NET_AdrToString(chan + 8);
    pk<int32_t>(chan, 0x20) = sequence;
    return 1;
}

uint8_t* XB_REGS1 XB_CDECL Netchan_Transmit(uint8_t* chan, int32_t length, const uint8_t* data) {
    const auto error = title_function<void XB_CDECL(uint32_t, const char*, int32_t)>(0x002938f0);  // __break
    if (length > 0x800) error(1, reinterpret_cast<const char*>(uintptr_t{0x00833c30}), length);
    if (length >= 0x8ca) error(1, reinterpret_cast<const char*>(uintptr_t{0x00833c0c}), 0);
    uint8_t packet[0x930] = {};
    const uint32_t sequence = pk<uint32_t>(chan, 0x24);
    std::memcpy(packet, &sequence, 4);
    int32_t size = 4;
    pk<uint32_t>(chan, 0x24) = pk<uint32_t>(chan, 0x24) + 1;
    if (pk<uint32_t>(chan, 0) == 0) size += 2;  // qport 0
    platform_rep_movs(packet + size, data, static_cast<uint32_t>(length));
    uint8_t* loopback = loopback_of(pk<uint32_t>(chan, 0));
    ++pk<int32_t>(loopback, 0x938);
    size += length;
    platform_rep_movs(loopback, packet, static_cast<uint32_t>(size));
    pk<int32_t>(loopback, 0x930) = size;
    return loopback;
}

PLATFORM_ASM_ENTRY(NET_OutOfBandPrint_entry,
                   "leal 4(%esp), %ecx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NET_OutOfBandPrint) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL NET_OutOfBandPrint(uint32_t sock, const uint32_t* args) {
    // args: the address (5 words), the format, then the variable arguments (MSVC va_list = their address).
    char text[0x800];
    text[0] = text[1] = text[2] = text[3] = static_cast<char>(0xff);
    title_function<int32_t XB_CDECL(char*, const char*, const void*)>(0x001eea95)(  // _vsprintf
        text + 4, reinterpret_cast<const char*>(uintptr_t{args[5]}), args + 6);
    const int32_t length = static_cast<int32_t>(std::strlen(text));
    uint8_t* loopback = loopback_of(sock);
    ++pk<int32_t>(loopback, 0x938);
    platform_rep_movs(loopback, text, static_cast<uint32_t>(length));
    pk<int32_t>(loopback, 0x930) = length;
    return length;
}

char* XB_THISCALL Com_ParseOnLine(const char** data_p) {
    uint8_t* info = reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x008d4ca0)});
    if (pk<uint32_t>(info, 0x84)) {
        const uint32_t restore = pk<uint32_t>(info, 0x88);
        pk<uint32_t>(info, 0x84) = 0;
        if (!restore) return reinterpret_cast<char*>(info);
        *data_p = reinterpret_cast<const char*>(uintptr_t{pk<uint32_t>(info, 0x98)});
        pk<uint32_t>(info, 0x80) = pk<uint32_t>(info, 0x94);
    }
    return COM_ParseExt(data_p, 0);
}

PLATFORM_ASM_ENTRY(MSG_ReadStringLine_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(MSG_ReadStringLine) "addl $4, %esp\n"
                   "ret")

char* XB_CDECL MSG_ReadStringLine(uint8_t* msg) {
    char* const out = &title_global<char>(0x0098b3c8);
    const int32_t read = pk<int32_t>(msg, 0x10);
    const char* text = reinterpret_cast<const char*>(pk_ptr(msg, 4)) + read;
    const int32_t length = static_cast<int32_t>(std::strlen(text));
    if (length >= 0x100 || read + length >= pk<int32_t>(msg, 0xc)) {
        out[0] = 0;
        return out;
    }
    char* d = out;
    char c;
    do {
        c = *text++;
        *d++ = c;
    } while (c);
    pk<int32_t>(msg, 0x10) = pk<int32_t>(msg, 0x10) + static_cast<int32_t>(std::strlen(out)) + 1;
    return out;
}

PLATFORM_ASM_ENTRY(Loc_ReadLine_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Loc_ReadLine) "addl $8, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Loc_ReadLine(const char** cursor, char* line) {
    const auto strchr_ = title_function<const char* XB_CDECL(const char*, int32_t)>(0x001ef5e0);  // _strchr
    if (!**cursor) return 0;
    const char* newline = strchr_(*cursor, '\n');
    if (newline) {
        const int32_t length = static_cast<int32_t>(newline - *cursor);
        title_function<char* XB_CDECL(char*, const char*, int32_t)>(0x001eed80)(line, *cursor, length);  // _strncpy
        line[length] = 0;
        *cursor += length;
        while (**cursor) {
            if (!strchr_(reinterpret_cast<const char*>(uintptr_t{0x008333a8}), **cursor)) break;  // "\r\n"
            ++*cursor;
        }
    } else {
        const char* s = *cursor;
        char* d = line;
        char c;
        do {
            c = *s++;
            *d++ = c;
        } while (c);
        *cursor += std::strlen(*cursor);
    }
    if (!line[0]) return 1;
    int32_t i = static_cast<int32_t>(std::strlen(line)) - 1;
    while (i >= 0) {
        if (!title_function<int32_t XB_CDECL(int32_t)>(0x001eefa0)(line[i])) break;  // _isspace
        --i;
        line[i + 1] = 0;
    }
    Loc_StripLineComment(line);
    return 1;
}

void XB_THISCALL SpgManager_Unload(uint8_t* manager, int32_t id) {
    for (int32_t i = 0; i < 0x20; ++i) {
        uint8_t* entry = manager + 4 + i * 12;
        if (pk<int32_t>(entry, 0) == id) {
            pk<int32_t>(entry, 0) = -1;
            pk<uint32_t>(entry, 8) = 0;
            return;
        }
    }
}

uint8_t* XB_REGS1 XB_CDECL SpgManager_Construct(uint8_t* self) {
    const uint32_t me = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
    pk<uint32_t>(self, 0) = 0x00838220;  // ResourceManager vtable
    const int32_t count = title_global<int32_t>(0x009719f8);
    if (count < 0x14) {
        title_global<uint32_t>(0x009719fc + static_cast<uint32_t>(count) * 4) = me;
        ++title_global<int32_t>(0x009719f8);
    }
    pk<uint32_t>(self, 0) = 0x00832660;  // SpgManager vtable
    for (int32_t i = 0; i < 0x20; ++i) {
        pk<int32_t>(self, 4 + i * 12) = -1;
        pk<uint32_t>(self, 12 + i * 12) = 0;
    }
    title_global<uint32_t>(0x00909940) = 0x002ae1b0;
    return self;
}

void XB_CDECL Loc_AppendLanguageSuffix() {
    if (title_global<uint8_t>(0x0098ad11)) return;
    char* const name = &title_global<char>(0x0095f828);
    uint8_t& length = title_global<uint8_t>(0x0095f847);
    title_global<uint8_t>(0x0098ad11) = 1;
    if (length < 0x1e) {
        name[length] = '_';
        ++length;
        name[length] = 0;
    }
    int32_t current = length;
    const char* language =
        reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x008d5a80 + title_global<uint32_t>(0x0095f5dc) * 4)});
    Str_AppendBounded(name, &current, 0x1f, language);
    length = static_cast<uint8_t>(current);
}

PLATFORM_ASM_ENTRY(Save_CheckVersion_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Save_CheckVersion) "addl $4, %esp\n"
                   "ret")

uint8_t XB_CDECL Save_CheckVersion(const char* header) {
    const char* date = reinterpret_cast<const char*>(uintptr_t{title_global<uint8_t>(0x0095e7f8) ? 0x0082ab00 : 0x0082aaf8});
    if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0)(header, date, 0x19))  // _strncmp
        return 0;
    const char* a = header + 0x3c0;
    const char* b = reinterpret_cast<const char*>(uintptr_t{Save_GetData()});
    for (;; ++a, ++b) {
        if (*a != *b) return 0;
        if (!*a) return 1;
    }
}

void XB_STDCALL Profile_SumMissionStats(const int32_t* chapters, int32_t* out) {
    int32_t sums[18] = {};
    const int32_t unlocked = title_global<int32_t>(0x008dce44);  // g_chaptersUnlocked
    if (unlocked >= 1) {
        const int32_t count = title_global<uint8_t>(0x008dce54) ? 0xe : unlocked;
        for (int32_t c = 0; c < count; ++c)
            for (int32_t k = 0; k < 17; ++k) sums[k] += chapters[c * 18 + k];
    }
    std::memcpy(out, sums, sizeof sums);
}

void XB_THISCALL DynArray_PushBack24(DynArray* array, const uint32_t* value) {
    if (array->size >= array->capacity) {
        const uint32_t capacity = array->size + 4;
        uint32_t* data = DynArray_Alloc24_FillUndefined(capacity, array->size + 1);
        for (uint32_t i = 0; i < array->size; ++i)
            for (int32_t k = 0; k < 6; ++k) data[i * 6 + k] = array->data[i * 6 + k];
        if (array->data) Scr_Free(array->data);
        array->data = data;
        array->capacity = capacity;
    }
    uint32_t* slot = array->data + array->size * 6;
    for (int32_t k = 0; k < 6; ++k) slot[k] = value[k];
    ++array->size;
}

uint32_t* XB_STDCALL std_vector_UfillN4(uint32_t* dest, uint32_t count, const uint32_t* value) {
    std_UninitializedFillN4(dest, count, value);
    return dest + count;
}

uint32_t* XB_STDCALL std_list_BuyNodeKeyArray(uint32_t next, uint32_t prev, const uint32_t* value) {
    uint32_t* node = Scr_Alloc(0x18, 4);
    if (node) {
        node[0] = next;
        node[1] = prev;
        node[2] = value[0];
        node[3] = 0;
        node[4] = 0;
        node[5] = 0;
        DynArray_Assign4(reinterpret_cast<DynArray*>(node + 3), reinterpret_cast<const DynArray*>(value + 1));
    }
    return node;
}

// --- qcommon push 4 (high half) ---

namespace {

uint32_t u32(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
const char* tstr(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }

template <uint32_t Words>
void dynarray_push(DynArray* array, const uint32_t* value, uint32_t* (XB_STDCALL* alloc)(uint32_t, uint32_t)) {
    if (array->size >= array->capacity) {
        const uint32_t capacity = array->size + 4;
        uint32_t* data = alloc(capacity, array->size + 1);
        for (uint32_t i = 0; i < array->size; ++i)
            for (uint32_t k = 0; k < Words; ++k) data[i * Words + k] = array->data[i * Words + k];
        if (array->data) Scr_Free(array->data);
        array->data = data;
        array->capacity = capacity;
    }
    uint32_t* slot = array->data + array->size * Words;
    for (uint32_t k = 0; k < Words; ++k) slot[k] = value[k];
    ++array->size;
}

}  // namespace

void XB_THISCALL DynArray_PushBack12_2(DynArray* array, const uint32_t* value) {
    dynarray_push<3>(array, value, &DynArray_Alloc12_FillUndefined);
}

void XB_THISCALL DynArray_PushBack24_2(DynArray* array, const uint32_t* value) {
    dynarray_push<6>(array, value, &DynArray_Alloc24_FillUndefined_2);
}

void XB_THISCALL DynArray_Push52(DynArray* array, const uint32_t* value) {
    dynarray_push<13>(array, value, &DynArray_Alloc52_FillUndefined);
}

void XB_REGS3 XB_CDECL Pak_ParseFli(uint8_t* fli, uint32_t, uint8_t* pak) {
    const auto strstr_ = title_function<const char* XB_CDECL(const char*, const char*)>(0x001efb10);  // _strstr
    const auto get_language = title_function<uint32_t XB_STDCALL()>(0x001e6f2a);           // XGetLanguage
    pk<uint32_t>(pak, 0x18) = u32(fli);
    uint8_t* table = fli + pk<uint32_t>(fli, 0x18);
    pk<uint32_t>(fli, 0x18) = u32(table);
    Pack_RelocateTable(reinterpret_cast<PackRelocTable*>(table), u32(fli));
    const char* name = reinterpret_cast<const char*>(pk_ptr(pak, 0x18) + 0x1c);
    int32_t& language = title_global<int32_t>(0x0095f5dc);  // g_languageIndex
    if (!language) {
        if (strstr_(name, tstr(0x0082d010))) {  // "de"
            language = 1;
        } else if (strstr_(name, tstr(0x00838378))) {  // "eu"
            if (get_language() == 5)
                language = 3;
            else
                language = get_language() == 6 ? 4 : 2;
        } else {
            language = 0;
        }
    }
    if (strstr_(name, tstr(0x00838374)) || strstr_(name, tstr(0x00838370)))  // "ck", "ce"
        title_global<uint8_t>(0x0095e7f8) = 1;
    if (strstr_(name, tstr(0x00838374)) || strstr_(name, tstr(0x0083836c)))  // "ck", "uk"
        title_global<uint8_t>(0x0095ee00) = 1;
    Loc_AppendLanguageSuffix();
    Pak_AssignZoneIdsByName(pak);
}

namespace {

// The zone record of a loaded-list node: +0x78 caches MemContext_FindById(+0x74).
uint8_t* node_zone(uint8_t* node) {
    if (!pk<uint32_t>(node, 0x78))
        pk<uint32_t>(node, 0x78) = u32(MemContext_FindById(title_global<uint8_t*>(0x0095f638), pk<int32_t>(node, 0x74)));
    return pk_ptr(node, 0x78);
}

}  // namespace

uint32_t XB_REGS1 XB_CDECL Pak_CountActiveSlots(uint8_t* pak) {
    uint32_t count = 0;
    uint8_t* next;
    uint8_t* node = loaded_first(pak, &next);
    while (next) {
        if (!(Zone_UpdatePriority(node_zone(node)) == title_global<float>(0x0083eafc)))  // UCOMISS + JNP: NaN counts
            count += StreamMem_CountUsed(title_global<uint8_t*>(0x0095f62c), reinterpret_cast<const uint32_t*>(node + 0x54));
        node = next;
        next = pk_ptr(next, 0);
    }
    return count;
}

uint8_t* XB_REGS1 XB_CDECL Pak_FindZoneToUnload(uint8_t* pak) {
    float best = 0.0f;
    uint8_t* result = nullptr;
    uint8_t* next;
    uint8_t* node = loaded_first(pak, &next);
    while (next) {
        uint8_t* zone = node_zone(node);
        float priority = Zone_UpdatePriority(zone);
        if (pk<uint32_t>(zone, 0x2c) > 1) priority = 0.0f;
        if (priority > best) {
            best = priority;
            result = zone;
        }
        node = next;
        next = pk_ptr(next, 0);
    }
    return result;
}

int32_t XB_REGS1 XB_CDECL Com_Filter(const char* filter, const char* name, int32_t casesensitive) {
    const auto toupper_ = title_function<int32_t XB_CDECL(int32_t)>(0x001efd6d);  // LIBCMT::_toupper
    char buf[0x84];
    while (*filter) {
        if (*filter == '*') {
            ++filter;
            int32_t i = 0;
            for (; *filter && *filter != '*' && *filter != '?'; ++i) buf[i] = *filter++;
            buf[i] = 0;
            const size_t length = std::strlen(buf);
            if (length) {
                const char* found = Com_StringContains(name, buf, casesensitive);
                if (!found) return 0;
                name = found + std::strlen(buf);
            }
        } else if (*filter == '?') {
            ++filter;
            ++name;
        } else if (*filter == '[' && filter[1] == '[') {
            ++filter;
        } else if (*filter == '[') {
            ++filter;
            bool found = false;
            while (*filter && !found) {
                if (*filter == ']' && filter[1] != ']') break;
                if (filter[1] == '-' && filter[2] && (filter[2] != ']' || filter[3] == ']')) {
                    if (casesensitive) {
                        if (*name >= *filter && *name <= filter[2]) found = true;
                    } else {
                        const int32_t low = toupper_(*filter);
                        if (toupper_(*name) >= low) {
                            const int32_t high = toupper_(filter[2]);
                            if (toupper_(*name) <= high) found = true;
                        }
                    }
                    filter += 3;
                } else {
                    if (casesensitive) {
                        if (*filter == *name) found = true;
                    } else {
                        const int32_t a = toupper_(*filter);
                        if (a == toupper_(*name)) found = true;
                    }
                    ++filter;
                }
            }
            if (!found) return 0;
            while (*filter) {
                if (*filter == ']' && filter[1] != ']') break;
                ++filter;
            }
            ++filter;
            ++name;
        } else {
            if (casesensitive) {
                if (*filter != *name) return 0;
            } else {
                const int32_t a = toupper_(*filter);
                if (a != toupper_(*name)) return 0;
            }
            ++filter;
            ++name;
        }
    }
    return 1;
}

int32_t XB_REGS2 XB_CDECL Com_FilterPath(const char* name, const char* filter, int32_t casesensitive) {
    char new_name[0x80];
    char new_filter[0x80];
    int32_t i = 0;
    for (; i < 0x7f && filter[i]; ++i) new_filter[i] = filter[i] == '\\' || filter[i] == ':' ? '/' : filter[i];
    new_filter[i] = 0;
    for (i = 0; i < 0x7f && name[i]; ++i) new_name[i] = name[i] == '\\' || name[i] == ':' ? '/' : name[i];
    new_name[i] = 0;
    return Com_Filter(new_filter, new_name, casesensitive);
}

void XB_CDECL Com_PushAllRealEvents() {
    for (;;) {
        SysEvent real;
        SysEvent ev = *Com_GetRealEvent(&real);
        if (!ev.type) return;
        Com_PushEvent(reinterpret_cast<const uint32_t*>(&ev));
    }
}

int32_t XB_CDECL Com_Milliseconds() {
    for (;;) {
        SysEvent real;
        SysEvent ev = *Com_GetRealEvent(&real);
        if (!ev.type) return ev.time;
        Com_PushEvent(reinterpret_cast<const uint32_t*>(&ev));
    }
}

PLATFORM_ASM_ENTRY(Com_GetEvent_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Com_GetEvent) "addl $4, %esp\n"
                   "ret")

SysEvent* XB_CDECL Com_GetEvent(SysEvent* out) {
    int32_t& tail = title_global<int32_t>(0x0095f58c);  // com_pushedEventsTail
    if (title_global<int32_t>(0x0095f588) > tail) {     // com_pushedEventsHead
        ++tail;
        *out = *reinterpret_cast<const SysEvent*>(static_cast<uintptr_t>(0x00940400u + static_cast<uint32_t>((tail - 1) & 0x3f) * 0x18u));
        return out;
    }
    SysEvent real;
    *out = *Com_GetRealEvent(&real);
    return out;
}

namespace {

// AxisToAngles with row 1 at `row1`: FastSinCos of -angle * pi / 180 on the x87.
void axis_to_angles(float* angles, const float* row0, const float* row1) {
    const float pi = title_global<float>(0x0083ed00);
    const float k180 = title_global<float>(0x0083eb60);
    vectoangles(row0, angles);
    const float rx = row1[0], ry = row1[1], rz = row1[2];
    float s1, c1, s2, c2;
    FastSinCos(&s1, 0, &c1, static_cast<float>(-static_cast<long double>(angles[1]) * pi / k180));
    const float tx = c1 * rx - s1 * ry;
    const float ty = c1 * ry + s1 * rx;
    FastSinCos(&s2, 0, &c2, static_cast<float>(-static_cast<long double>(angles[0]) * pi / k180));
    float right[3];
    right[0] = tx * c2 + s2 * rz;
    right[1] = ty;
    right[2] = c2 * rz - tx * s2;
    const float pitch = vectopitch(right);
    if (0.0f > right[1])
        angles[2] = (0.0f > pitch ? k180 : title_global<float>(0x0083ebd0)) + pitch;
    else
        angles[2] = 0.0f - pitch;
}

}  // namespace

void XB_REGS3 XB_CDECL MatrixToAngles(float* angles, uint32_t, const float* matrix) {
    axis_to_angles(angles, matrix, matrix + 4);
}

void XB_REGS3 XB_CDECL AxisToAngles(float* angles, uint32_t, const float* axis) {
    axis_to_angles(angles, axis, axis + 3);
}

PLATFORM_ASM_ENTRY(vectoangles_ToOut_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(vectoangles_ToOut) "addl $8, %esp\n"
                   "ret")

void XB_CDECL vectoangles_ToOut(const float* v, float* out) {
    float angles[3];
    vectoangles(v, angles);
    out[0] = angles[0];
    out[1] = angles[1];
    out[2] = angles[2];
}

void XB_CDECL Noise_Init() {
    if (title_global<uint32_t>(0x0095f560)) return;
    const auto rand_ = title_function<int32_t XB_CDECL()>(0x001efcc0);  // _rand
    title_global<uint32_t>(0x0095f560) = 1;
    int32_t* perm = &title_global<int32_t>(0x0093e758);
    float* grad = &title_global<float>(0x0093eff0);
    for (int32_t i = 0; i < 0x100; ++i) {
        perm[i] = i;
        grad[i] = static_cast<float>(rand_() % 0x200 - 0x100) * title_global<float>(0x0083ebe4);  // 1/256
    }
    for (int32_t i = 0xff; i != 0; --i) {
        const int32_t k = perm[i];
        const int32_t j = rand_() % 0x100;
        perm[i] = perm[j];
        perm[j] = k;
    }
    for (int32_t i = 0; i < 0x102; ++i) {
        perm[0x100 + i] = perm[i];
        reinterpret_cast<uint32_t*>(grad)[0x100 + i] = reinterpret_cast<const uint32_t*>(grad)[i];
    }
}

void XB_REGS1 XB_CDECL FS_FreeSearchPaths(uint8_t* list) {
    for (uint8_t* path = list; path;) {
        uint8_t* next = pk_ptr(path, 0);
        if (pk<uint32_t>(path, 4)) {
            unzClose(reinterpret_cast<UnzState*>(pk_ptr(pk_ptr(path, 4), 0x180)));
            Z_Free(pk_ptr(pk_ptr(path, 4), 0x19c));
            Z_Free(pk_ptr(path, 4));
        }
        if (pk<uint32_t>(path, 8)) Z_Free(pk_ptr(path, 8));
        Z_Free(path);
        path = next;
    }
}

PLATFORM_ASM_ENTRY(Bounds_AddPoints8_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Bounds_AddPoints8) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Bounds_AddPoints8(const float* m, const float* box, float* bounds) {
    const auto add_point = title_function<void XB_REGS3 XB_CDECL(float*, uint32_t, const float*)>(0x00029380);
    for (int32_t i = 0; i < 8; ++i) {
        const float x = (i & 4) ? box[0] : box[4];
        const float y = (i & 2) ? box[1] : box[5];
        const float z = (i & 1) ? box[2] : box[6];
        alignas(16) float point[4] = {};
        point[0] = (m[0] * x + z * m[6]) + y * m[3];
        point[1] = (m[4] * y + m[7] * z) + m[1] * x;
        point[2] = (m[8] * z + m[2] * x) + y * m[5];
        add_point(bounds, 0, point);
    }
}

void XB_REGS3 XB_CDECL Bounds_AddPointSplit(float* maxs, uint32_t, float* mins, const float* point) {
    float p[4], m[4];
    for (int32_t i = 0; i < 4; ++i) p[i] = point[i];
    for (int32_t i = 0; i < 4; ++i) m[i] = mins[i];
    for (int32_t i = 0; i < 4; ++i) mins[i] = m[i] < p[i] ? m[i] : p[i];  // MINPS mins, point: point if unordered
    for (int32_t i = 0; i < 4; ++i) p[i] = point[i];
    for (int32_t i = 0; i < 4; ++i) m[i] = maxs[i];
    for (int32_t i = 0; i < 4; ++i) maxs[i] = m[i] > p[i] ? m[i] : p[i];  // MAXPS maxs, point
}

PLATFORM_ASM_ENTRY(CM_WorldTrees_Construct_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CM_WorldTrees_Construct) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL CM_WorldTrees_Construct(uint8_t* self) {
    pk<uint32_t>(self, 0) = 0x00838220;  // ResourceManager vtable
    const int32_t count = title_global<int32_t>(0x009719f8);
    if (count < 0x14) {
        title_global<uint32_t>(0x009719fc + static_cast<uint32_t>(count) * 4) = u32(self);
        ++title_global<int32_t>(0x009719f8);
    }
    pk<uint32_t>(self, 0) = 0x00832788;
    const int32_t debug = title_global<int32_t>(0x00988780);
    if (debug < 0x20) {
        title_global<uint32_t>(0x00988784 + static_cast<uint32_t>(debug) * 4) = 0x0001d0d0;
        ++title_global<int32_t>(0x00988780);
    }
    pk<uint32_t>(self, 0xc) = 0;
    std::memset(self + 0x10, 0, 0x63 * 4);
    pk<uint32_t>(self, 4) = 0;
    pk<uint32_t>(self, 8) = 0;
    pk<uint32_t>(self, 4) &= ~1u;
    Cmd_AddCommand(tstr(0x00833520), 0x002afb90);  // "cg"
    return self;
}

void XB_CDECL Loc_InitCvars() {
    title_global<uint8_t*>(0x0093f7fc) = Cvar_Get(tstr(0x0083349c), tstr(0x0082e098), 0x21);  // cl_language
    title_global<uint8_t*>(0x0092efe8) = Cvar_Get(tstr(0x00833484), tstr(0x0082e098), 0);     // cl_languagesavailable
    title_global<uint8_t*>(0x00929b4c) = Cvar_Get(tstr(0x0083346c), tstr(0x0082e058), 0x20);  // cl_languagetranslate
    title_global<uint8_t*>(0x0093f804) = Cvar_Get(tstr(0x00833458), tstr(0x0082e098), 0);     // cl_languagewarnings
    title_global<uint8_t*>(0x00927b2c) = Cvar_Get(tstr(0x0083343c), tstr(0x0082e098), 0);     // ..aserrors
    Cvar_Get(tstr(0x0083349c), tstr(0x0082e098), 0x21);
    const int32_t language = pk<int32_t>(title_global<uint8_t*>(0x0093f7fc), 0x20);
    title_global<uint32_t>(0x0093e750) = language >= 8 && language <= 0xc ? 1 : 0;
}

uint8_t* XB_REGS3 XB_CDECL Profile_Copy(uint8_t* dst, uint32_t, uint8_t* src) {
    for (uint32_t i = 0; i < 0x10; ++i) dst[i] = src[i];
    pk<uint32_t>(dst, 0x10) = pk<uint32_t>(src, 0x10);
    pk<uint32_t>(dst, 0x14) = pk<uint32_t>(src, 0x14);
    pk<uint32_t>(dst, 0x18) = pk<uint32_t>(src, 0x18);
    pk<uint32_t>(dst, 0x1c) = pk<uint32_t>(src, 0x1c);
    pk<uint32_t>(dst, 0x20) = pk<uint32_t>(src, 0x20);
    pk<uint8_t>(dst, 0x24) = pk<uint8_t>(src, 0x24);
    pk<uint8_t>(dst, 0x25) = pk<uint8_t>(src, 0x25);
    pk<uint32_t>(dst, 0x28) = pk<uint32_t>(src, 0x28);
    pk<uint32_t>(dst, 0x2c) = pk<uint32_t>(src, 0x2c);
    pk<uint32_t>(dst, 0x30) = pk<uint32_t>(src, 0x30);
    pk<uint32_t>(dst, 0x34) = pk<uint32_t>(src, 0x34);
    pk<uint8_t>(dst, 0x38) = pk<uint8_t>(src, 0x38);
    pk<uint8_t>(dst, 0x39) = pk<uint8_t>(src, 0x39);
    pk<uint8_t>(dst, 0x3a) = pk<uint8_t>(src, 0x3a);
    pk<uint32_t>(dst, 0x3c) = pk<uint32_t>(src, 0x3c);
    pk<uint8_t>(dst, 0x40) = pk<uint8_t>(src, 0x40);
    pk<uint8_t>(dst, 0x41) = pk<uint8_t>(src, 0x41);
    pk<uint8_t>(dst, 0x42) = pk<uint8_t>(src, 0x42);
    pk<uint32_t>(dst, 0x44) = pk<uint32_t>(src, 0x44);
    pk<uint32_t>(dst, 0x48) = pk<uint32_t>(src, 0x48);
    pk<uint32_t>(dst, 0x4c) = pk<uint32_t>(src, 0x4c);
    pk<uint8_t>(dst, 0x50) = pk<uint8_t>(src, 0x50);
    pk<uint32_t>(dst, 0x54) = pk<uint32_t>(src, 0x54);
    pk<uint32_t>(dst, 0x58) = pk<uint32_t>(src, 0x58);
    pk<uint8_t>(dst, 0x5c) = pk<uint8_t>(src, 0x5c);
    pk<uint8_t>(dst, 0x5d) = pk<uint8_t>(src, 0x5d);
    pk<uint32_t>(dst, 0x60) = pk<uint32_t>(src, 0x60);
    pk<uint32_t>(dst, 0x64) = pk<uint32_t>(src, 0x64);
    pk<uint32_t>(dst, 0x68) = pk<uint32_t>(src, 0x68);
    pk<uint32_t>(dst, 0x6c) = pk<uint32_t>(src, 0x6c);
    pk<uint32_t>(dst, 0x70) = pk<uint32_t>(src, 0x70);
    pk<uint32_t>(dst, 0x74) = pk<uint32_t>(src, 0x74);
    pk<uint32_t>(dst, 0x78) = pk<uint32_t>(src, 0x78);
    pk<uint32_t>(dst, 0x7c) = pk<uint32_t>(src, 0x7c);
    pk<uint32_t>(dst, 0x80) = pk<uint32_t>(src, 0x80);
    pk<uint32_t>(dst, 0x84) = pk<uint32_t>(src, 0x84);
    pk<uint32_t>(dst, 0x88) = pk<uint32_t>(src, 0x88);
    pk<uint32_t>(dst, 0x8c) = pk<uint32_t>(src, 0x8c);
    pk<uint32_t>(dst, 0x90) = pk<uint32_t>(src, 0x90);
    pk<uint32_t>(dst, 0x94) = pk<uint32_t>(src, 0x94);
    pk<uint32_t>(dst, 0x98) = pk<uint32_t>(src, 0x98);
    pk<uint32_t>(dst, 0x9c) = pk<uint32_t>(src, 0x9c);
    pk<uint32_t>(dst, 0xa0) = pk<uint32_t>(src, 0xa0);
    pk<uint32_t>(dst, 0xdc) = pk<uint32_t>(src, 0xdc);
    for (uint32_t i = 0; i < 0xe; ++i) pk<uint32_t>(dst, 0xa4 + i * 4) = pk<uint32_t>(src, 0xa4 + i * 4);
    pk<uint8_t>(dst, 0xe0) = pk<uint8_t>(src, 0xe0);
    pk<uint32_t>(dst, 0xe4) = pk<uint32_t>(src, 0xe4);
    pk<uint8_t>(dst, 0x39c) = pk<uint8_t>(src, 0x39c);
    pk<uint8_t>(dst, 0x39f) = pk<uint8_t>(src, 0x39f);
    return dst;
}

void XB_THISCALL ProfileSave_Dtor(uint8_t* self) {
    pk<uint32_t>(self, 0) = 0x008287dc;
    for (uint32_t i = 0; i < 4; ++i) Z_Free(pk_ptr(self, 0x11c0 + i * 4));
    Z_Free(pk_ptr(self, 0x11bc));
}

void XB_CDECL StaticInit_Mat44() {
    const float one = title_global<float>(0x0083eb20);
    const float r0[4] = {one, 0.0f, 0.0f, 0.0f};
    const float r1[4] = {0.0f, one, 0.0f, 0.0f};
    const float r2[4] = {0.0f, 0.0f, one, 0.0f};
    const float r3[4] = {0.0f, 0.0f, 0.0f, one};
    Mat44_FromRows(&title_global<float>(0x00909880), r0, r1, r2, r3);
}

void XB_CDECL StaticInit_Vtable_8bb768() { title_global<uint32_t>(0x008bb768) = 0x007c4d90; }

namespace {

// StringMap_Find (0x000249a0): map in EBX, a pointer to the key's pointer on the stack (stdcall).
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL qfs_StringMap_Find(const uint8_t* map, const char* const* key);
PLATFORM_ASM_ENTRY(qfs_StringMap_Find,
                   "pushl %ebx\n"
                   "movl 8(%esp), %ebx\n"
                   "pushl 12(%esp)\n"
                   "movl $0x000249a0, %eax\n"
                   "call *%eax\n"
                   "popl %ebx\n"
                   "ret")

// The value of `name` in a zone's string map (+8; index past the count +0x10 reads element 0 of +0x14).
uint32_t zone_string_value(uint8_t* zone, const char* const* key, bool* found) {
    const uint32_t* entry = qfs_StringMap_Find(zone + 8, key);
    *found = entry != nullptr;
    if (!entry) return 0;
    uint32_t index = *entry;
    if (index >= pk<uint32_t>(zone, 0x10)) index = 0;
    return pk<uint32_t>(pk_ptr(zone, 0x14), index * 4);
}

}  // namespace

uint32_t XB_REGS1 XB_STDCALL Pak_FindZoneByName(const char* name, uint8_t* pak) {
    const char* key = name;
    for (int32_t id = pk<int32_t>(pak, 0x1cc); id != -1; id = pk<int32_t>(pk_ptr(pak, 4 + id * 4), 0x3c)) {
        bool found;
        const uint32_t value = zone_string_value(pk_ptr(pak, 4 + id * 4), &key, &found);
        if (found && value) return value;
    }
    return 0;
}

uint32_t XB_REGS3 XB_CDECL Pak_FindZone(uint8_t* context, uint32_t, const char* name) {
    uint8_t* zone = pk_ptr(context, 0x18);
    if (!zone) return 0;
    const char* key = name;
    bool found;
    return zone_string_value(zone, &key, &found);
}

uint8_t XB_THISCALL MemContext_Free_r(uint8_t* context, uint8_t* mem) {
    if (!mem) return 1;
    const int32_t heaps = pk<int32_t>(context, 0x8c);
    for (int32_t i = 0; i < heaps; ++i) {
        uint8_t* heap = pk_ptr(context, 0x90 + i * 4);
        if (heap && mem >= pk_ptr(heap, 0x474) && mem < pk_ptr(heap, 0x478)) {
            Heap_FreeIn(u32(heap), mem);
            return 1;
        }
    }
    for (int32_t i = 0; i < pk<int32_t>(context, 0xc0); ++i)
        if (MemContext_Free_r(pk_ptr(context, 0xc4 + i * 4), mem)) return 1;
    return 0;
}

uint8_t* XB_THISCALL MemContext_Alloc_r(uint8_t* context, uint32_t align, uint32_t size) {
    if (size == 0xffffffffu) size = 0x400;
    for (int32_t i = 0; i < pk<int32_t>(context, 0x8c); ++i) {
        uint8_t* heap = pk_ptr(context, 0x90 + i * 4);
        const uint32_t used = pk<uint32_t>(heap, 0x488);
        const uint32_t capacity = pk<uint32_t>(heap, 0x484);
        if (used + size >= capacity || capacity == used) continue;
        const uint32_t previous = Heap_SetCurrent(u32(heap));
        uint8_t* mem = Heap_AllocAligned(align, size);
        Heap_SetCurrent(previous);
        if (mem) return mem;
    }
    for (int32_t i = 0; i < pk<int32_t>(context, 0xc0); ++i)
        if (uint8_t* mem = MemContext_Alloc_r(pk_ptr(context, 0xc4 + i * 4), align, size)) return mem;
    return nullptr;
}

PLATFORM_ASM_ENTRY(MemContext_AllocOrHeap_entry,
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(MemContext_AllocOrHeap) "addl $16, %esp\n"
                   "ret")

uint8_t* XB_CDECL MemContext_AllocOrHeap(int32_t slot, uint8_t* pak, uint32_t align, uint32_t size) {
    if (slot != -1) {
        uint8_t* context = slot >= 0 && slot < 0x63 ? pk_ptr(pak, 0x38 + static_cast<uint32_t>(slot) * 4) : nullptr;
        if (uint8_t* mem = MemContext_Alloc_r(context, align, size)) return mem;
    }
    return Heap_AllocAligned(align, size);
}

PLATFORM_ASM_ENTRY(Loc_GetString_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Loc_GetString) "addl $4, %esp\n"
                   "ret")

const char* XB_CDECL Loc_GetString(const char* key) {
    if (!key) return key;
    const uint32_t hash = title_function<uint32_t XB_REGS1 XB_CDECL(const char*)>(0x00024440)(key);  // Loc_HashKey
    const uint8_t* entry = Zone_FindByHash(title_global<const uint8_t*>(0x0095f5d8), hash);  // rm_locStrings
    if (!entry) return key;
    const char* text = reinterpret_cast<const char*>(uintptr_t{*reinterpret_cast<const uint32_t*>(entry + 8)});
    return text ? text : tstr(0x008379b4);  // "NO STRING"
}

void XB_THISCALL Pak_PushZoneIds_r(uint8_t* pak, uint8_t* zone) {
    for (uint32_t i = 0; i < pk<uint32_t>(zone, 0x1c); ++i) {
        const uint32_t index = i < pk<uint32_t>(zone, 0x1c) ? i : 0;
        Pak_PushZoneIds_r(pak, pk_ptr(pk_ptr(zone, 0x20), index * 4));
    }
    const int32_t count = pk<int32_t>(pak, 0x374);
    if (count < 0x80) {
        pk<uint32_t>(pak, 0x378 + static_cast<uint32_t>(count) * 4) = pk<uint32_t>(zone, 0x28);
        ++pk<int32_t>(pak, 0x374);
    }
}

void XB_REGS3 XB_CDECL Q_CleanStrLower(char* dst, uint32_t, const char* src) {
    const auto tolower_ = title_function<int32_t XB_CDECL(int32_t)>(0x001eed48);  // tolower
    while (*src) {
        if (*src == 0x1b) {
            src += 2;
            continue;
        }
        if (*src >= 0x20) *dst++ = static_cast<char>(tolower_(*src));
        ++src;
    }
    *dst = 0;
}

// Runs where the original's call puts SV_PacketEvent (by-value address on the stack), with its registers.
PLATFORM_ASM_ENTRY(Com_RunAndTimeServerPacket_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl 8(%ebp)\n"
                   "leal 4(%ebp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Com_RunAndTimeServerPacket) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret")

uint32_t XB_CDECL Com_RunAndTimeServerPacket(uint32_t entry, uint32_t msg, const uint32_t* from, uint32_t ebx,
                                             uint32_t edi) {
    const auto sys_milliseconds = title_function<uint32_t XB_CDECL()>(0x002b1ca0);
    uint8_t* speeds = title_global<uint8_t*>(0x009403e8);  // com_speeds
    if (pk<uint32_t>(speeds, 0x20)) sys_milliseconds();
    const uint32_t words[5] = {from[0], from[1], from[2], from[3], from[4]};
    const TitleRegs regs = {msg, ebx, entry - 0x14, from[4], u32(from), edi};
    title_call_at_regs(entry, 0x002c9de0, &regs, 5, words);  // SV_PacketEvent
    speeds = title_global<uint8_t*>(0x009403e8);
    if (pk<uint32_t>(speeds, 0x20)) return sys_milliseconds();
    return u32(speeds);
}

void XB_REGS3 XB_STDCALL Quat_ToMat44(uint32_t, const float* translation, float* out, const float* q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float s = title_global<float>(0x0083eba8) / (((w * w + xx) + yy) + zz);  // 2
    const float xxs = xx * s, zzs = zz * s, yys = yy * s;
    const float xy = (y * x) * s;
    const float yw = (y * s) * w;
    const float xz = (x * s) * z;
    const float xw = (x * s) * w;
    const float yz = (y * s) * z;
    const float wz = (s * w) * z;
    const float one = title_global<float>(0x0083eb20);
    const float r00 = one - (zzs + yys);
    const float yx = yys + xxs;
    out[0] = r00;
    out[1] = wz + xy;
    out[2] = xz - yw;
    out[3] = r00;
    const float r10 = xy - wz;
    const float r11 = one - (zzs + xxs);
    out[4] = r10;
    out[5] = r11;
    out[6] = yz + xw;
    out[7] = r10;
    const float r20 = yw + xz;
    out[8] = r20;
    out[9] = yz - xw;
    out[10] = one - yx;
    out[11] = r20;
    uint32_t* row3 = reinterpret_cast<uint32_t*>(out + 12);
    const uint32_t* source = translation ? reinterpret_cast<const uint32_t*>(translation)
                                         : &title_global<uint32_t>(0x0082e290);  // {0, 0, 0, 1}
    for (int32_t i = 0; i < 4; ++i) row3[i] = source[i];
}

uint32_t XB_REGS2 XB_STDCALL Mat44_ToQuatTranslation(const float* m, float* translation, float* q) {
    if (translation) {
        for (int32_t i = 0; i < 4; ++i)
            reinterpret_cast<uint32_t*>(translation)[i] = reinterpret_cast<const uint32_t*>(m)[12 + i];
    }
    const float one = title_global<float>(0x0083eb20);
    const float half = title_global<float>(0x0083eb24);
    const float yz = m[5] + m[10];
    const float trace = yz + m[0];
    // r = sqrt(radicand + 1) on the x87; the quaternion part is r * 0.5, the others scale by 0.5 / (float)r.
    const auto root = [&](long double radicand, float* big) {
        const long double r = platform_x87_sqrt_ext(radicand + one);
        const float rf = static_cast<float>(r);
        const float scale = half / rf;
        *big = static_cast<float>(r * half);
        return scale;
    };
    if (trace >= 0.0f) {
        const float s = root(trace, &q[3]);
        q[0] = (m[6] - m[9]) * s;
        q[1] = (m[8] - m[2]) * s;
        q[2] = (m[1] - m[4]) * s;
    } else {
        int32_t i = m[5] > m[0] ? 1 : 0;
        if (m[10] > m[i * 5]) i = 2;
        if (i == 2) {
            const float s = root(static_cast<long double>(m[10]) - (static_cast<long double>(m[5]) + m[0]), &q[2]);
            q[0] = (m[8] + m[2]) * s;
            q[1] = (m[9] + m[6]) * s;
            q[3] = (m[1] - m[4]) * s;
        } else if (i == 1) {
            const float s = root(static_cast<long double>(m[5]) - (static_cast<long double>(m[0]) + m[10]), &q[1]);
            q[2] = (m[9] + m[6]) * s;
            q[0] = (m[4] + m[1]) * s;
            q[3] = (m[8] - m[2]) * s;
        } else {
            const float s = root(static_cast<long double>(m[0]) - yz, &q[0]);
            q[1] = (m[4] + m[1]) * s;
            q[2] = (m[8] + m[2]) * s;
            q[3] = (m[6] - m[9]) * s;
        }
    }
    const long double a = q[0], b = q[1], c = q[2], d = q[3];
    const long double length = platform_x87_sqrt_ext(((a * a + b * b) + c * c) + d * d);
    const long double error = platform_x87_abs_ext(one - length);
    return static_cast<long double>(title_global<float>(0x0083ed50)) > error ? 1 : 0;  // 0.0001
}

void XB_CDECL Com_InitNodeRing() {
    for (int32_t i = 1; i <= 0x200; ++i) {
        uint8_t* node = &title_global<uint8_t>(0x00965cfa + static_cast<uint32_t>(i - 1) * 0x30);
        pk<uint16_t>(node, 0) = static_cast<uint16_t>((i + 0x1fe) % 0x200);
        pk<uint16_t>(node, 2) = static_cast<uint16_t>(i % 0x200);
    }
    title_global<float>(0x00965d00) = 0.0f;
    title_global<float>(0x00965d04) = 0.0f;
    title_global<uint16_t>(0x00965d08) = 0;
    title_global<uint16_t>(0x00965d0a) = 0;
    title_function<void XB_THISCALL(uint8_t*, const char*)>(0x0011dd20)(&title_global<uint8_t>(0x00965ce8),
                                                                       tstr(0x00832b28));  // "end"
}

namespace {

// The ring's name string (0x00965ce8, text at +6) cleared unless empty.
void clear_ring_name() {
    const uint32_t text = title_global<uint32_t>(0x00965ce8);
    if (text && text + 6 && *reinterpret_cast<const char*>(uintptr_t{text + 6}))
        title_function<void XB_THISCALL(uint8_t*)>(0x0011d660)(&title_global<uint8_t>(0x00965ce8));  // ScriptString_Clear
}

}  // namespace

void XB_CDECL Com_ResetNodeRing() {
    if (title_global<uint32_t>(0x0095f590)) title_global<uint32_t>(0x0095f590) = 0;
    clear_ring_name();
    Com_InitNodeRing();
    title_global<uint32_t>(0x0095f590) = 1;
}

void XB_CDECL Com_Shutdown() {
    if (title_global<uint32_t>(0x0095f590)) title_global<uint32_t>(0x0095f590) = 0;
    clear_ring_name();
    if (const int32_t log = title_global<int32_t>(0x009476fc)) {  // logfile
        FS_FCloseFile(log);
        title_global<int32_t>(0x009476fc) = 0;
    }
    if (const int32_t journal = title_global<int32_t>(0x0093fce0)) {  // com_journalFile
        FS_FCloseFile(journal);
        title_global<int32_t>(0x0093fce0) = 0;
    }
}

PLATFORM_ASM_ENTRY(FS_AddGameDirectories_entry,
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(FS_AddGameDirectories) "addl $8, %esp\n"
                   "ret")

void XB_CDECL FS_AddGameDirectories(uint32_t path, uint32_t dir) {
    const auto add = title_function<void XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t)>(
        0x0029cb60);  // FS_AddGameDirectory (ECX + 3 stack words)
    for (int32_t i = 0xd; i >= 0; --i) add(0, 0, path, dir, 1, i);
    add(0, 0, path, dir, 0, 0);
}

void XB_CDECL Loc_Init() {
    Cvar_Get(tstr(0x0083349c), tstr(0x0082e098), 0x21);  // cl_language
    const int32_t language = pk<int32_t>(title_global<uint8_t*>(0x0093f7fc), 0x20);
    title_global<uint32_t>(0x0093e750) = language >= 8 && language <= 0xc ? 1 : 0;
    Cvar_Set2(tstr(0x00833484), const_cast<char*>(tstr(0x0082e058)), 1);  // cl_languagesavailable "1"
    title_function<void XB_CDECL()>(0x002ca5e0)();     // Loc_ClearStrings
}

namespace {

// HashMap_Insert (0x000148b0): map in ESI, out and pair on the stack (stdcall).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL qfs_HashMap_Insert(uint8_t* map, uint32_t* out, const void* pair);
PLATFORM_ASM_ENTRY(qfs_HashMap_Insert,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "movl $0x000148b0, %eax\n"
                   "call *%eax\n"
                   "popl %esi\n"
                   "ret")

// IntHashMap_Erase (0x00014af0): map in EDI, out in EBX, the node on the stack (stdcall).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL qfs_IntHashMap_Erase(uint8_t* map, uint32_t* out, uint32_t node);
PLATFORM_ASM_ENTRY(qfs_IntHashMap_Erase,
                   "pushl %ebx\n"
                   "pushl %edi\n"
                   "movl 12(%esp), %edi\n"
                   "movl 16(%esp), %ebx\n"
                   "pushl 20(%esp)\n"
                   "movl $0x00014af0, %eax\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "popl %ebx\n"
                   "ret")

}  // namespace

void XB_CDECL NameMap_Insert(uint32_t value, const char* name) {
    // Locals the original leaves partly unset (zero in tests): out (+0, starts as the copy length), the name copy
    // (+8), the pair {value, 32-byte fixed string} (+0x28).
    struct Pair {
        uint32_t value;
        char key[0x20];
    };
    int32_t length = 0;
    char copy[0x20] = {};
    Q_strncpyLen(copy, &length, 0x1f, name);
    copy[0x1f] = static_cast<char>(length);
    Pair pair = {};
    pair.value = value;
    int32_t key_length = 0;
    FixedStr_Copy(pair.key, &key_length, 0x1f, copy, static_cast<uint8_t>(length));
    pair.key[0x1f] = static_cast<char>(key_length);
    uint32_t out[2] = {static_cast<uint32_t>(length), 0};
    qfs_HashMap_Insert(&title_global<uint8_t>(0x0095fe00), out, &pair);
}

PLATFORM_ASM_ENTRY(Loc_TranslateString_entry,
                   "pushl %edi\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Loc_TranslateString) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL Loc_TranslateString(const char* key, char* out, const char* context, int32_t mode) {
    const auto va_ = title_function<const char* XB_CDECL(const char*, ...)>(0x002a93d0);
    const uint8_t* translate = title_global<const uint8_t*>(0x00929b4c);  // cl_languagetranslate
    const char* text = key;
    if (translate && pk<uint32_t>(const_cast<uint8_t*>(translate), 0x20) && key[0] && key[1]) text = Loc_GetString(key);
    if (!text) {
        const uint8_t* warnings = title_global<const uint8_t*>(0x0093f804);
        if (warnings && pk<uint32_t>(const_cast<uint8_t*>(warnings), 0x20)) {
            const uint8_t* errors = title_global<const uint8_t*>(0x00927b2c);
            if (errors && pk<uint32_t>(const_cast<uint8_t*>(errors), 0x20) && mode != 1)
                title_function<void XB_CDECL(uint32_t, const char*, ...)>(0x002938f0)(7, tstr(0x008333d4), context, key);
            text = va_(tstr(0x00835264), key);  // "^1UNLOCALIZED(^7%s^1)^7"
        } else {
            text = va_(tstr(0x0082ddd4), key);  // "%s"
        }
        if (mode == 1) return 0;
    }
    char c;
    do {
        c = *text;
        *out++ = c;
        ++text;
    } while (c);
    return 1;
}

uint8_t XB_REGS1 XB_CDECL IntHashMap_EraseByString(const char* name) {
    const int32_t hash = title_function<int32_t XB_CDECL(const char*)>(0x002f5470)(name);  // ScrIface_HashStringNoCase
    uint32_t node = 0;
    IntHashMap_Find(&node, &hash, &title_global<uint8_t>(0x0095fe00));
    if (node == title_global<uint32_t>(0x0095fe08)) return 0;
    qfs_IntHashMap_Erase(&title_global<uint8_t>(0x0095fe00), &node, node);
    return 1;
}

void XB_CDECL Com_Freeze_f() {
    if (title_global<int32_t>(0x00934248) != 2) return;  // cmd_argc
    const float seconds = title_function<float XB_CDECL(const char*)>(0x001ef056)(
        reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x0092df94)}));  // atof(argv[1])
    const int32_t start = Com_Milliseconds();
    for (;;) {
        const int32_t elapsed = Com_Milliseconds() - start;
        const long double s = static_cast<long double>(static_cast<double>(elapsed)) * title_global<double>(0x0083ef88);
        if (s > static_cast<long double>(seconds)) return;
    }
}

void XB_REGS3 XB_CDECL Quat_FromRotationVector(uint32_t, const float* v, float* out) {
    // MULPS v * v over all four lanes (MXCSR flags), the length from lanes 0-2.
    float sq[4];
    for (int32_t i = 0; i < 4; ++i) sq[i] = v[i] * v[i];
    platform_keep(sq);
    const float n = (sq[0] + sq[1]) + sq[2];
    float length;
    if (n == 0.0f || n != n) {  // UCOMISS + JNE: equal or unordered
        length = n;
    } else {
        const float r = platform_rsqrt_estimate(n);
        const float nr = n * r;
        length = ((title_global<float>(0x0082e660) - nr * r) * nr) * title_global<float>(0x0082e670);  // 3, 0.5
    }
    if (length == 0.0f) {
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
        out[3] = title_global<float>(0x0083eb20);
        return;
    }
    const float one = title_global<float>(0x0083eb20);
    const long double half_angle = static_cast<long double>(length) * title_global<float>(0x0083eb24);
    const float inv = one / length;
    const float s = static_cast<float>(platform_x87_sin_ext(half_angle));
    float a[4];
    for (int32_t i = 0; i < 4; ++i) a[i] = s * (v[i] * inv);  // MULPS twice, all lanes
    platform_keep(a);
    const float a0 = a[0];
    out[0] = a0;
    const float a1 = s * (v[1] * inv);
    out[1] = a1;
    const float a2 = s * (v[2] * inv);
    const float c = static_cast<float>(platform_x87_cos_ext(half_angle));
    const long double la2 = a2, la1 = a1, lc = c, la0 = a0;
    const float norm = static_cast<float>(platform_x87_sqrt_ext(((la2 * la2 + la1 * la1) + lc * lc) + la0 * la0));
    const float k = one / norm;
    out[0] = k * a0;
    out[1] = k * a1;
    out[2] = k * a2;
    out[3] = k * c;
}

}  // namespace game
