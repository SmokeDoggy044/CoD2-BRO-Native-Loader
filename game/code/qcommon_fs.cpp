#include "qcommon_fs.h"

#include <cstdarg>
#include <cstring>

#include "cl.h"
#include "cg_view.h"
#include "cm.h"
#include "fe.h"
#include "fx.h"
#include "lanes.h"
#include "movie.h"
#include "nal_math.h"
#include "fx_load.h"
#include "actor.h"
#include "g_scr_runtime.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "script_string.h"
#include "snd.h"
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
    int32_t zip_file_pos;   // +0xc, unztell/ftell when opened by mode
    int32_t file_size;      // +0x10
    int32_t zip_info_pos;   // +0x14, unzSetCurrentFileInfoPosition's argument (rewind)
    int32_t zip_file;       // +0x18
    int32_t streamed;       // +0x1c
    char name[0x100];       // +0x20
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


// --- qcommon push 5 (high half): file system, command buffer ---

namespace {

constexpr uint32_t kSearchPaths = 0x0095f578;  // fs_searchpaths
constexpr uint32_t kFsGamedir = 0x0093fc30;    // fs_gamedir (char[0x80])

using BreakV = void XB_CDECL(int32_t, const char*, ...);
using CrtFopen = void* XB_CDECL(const char*, const char*);
using CrtStrncpy = char* XB_CDECL(char*, const char*, uint32_t);
using CrtStrstr = const char* XB_CDECL(const char*, const char*);

void fs_break(int32_t level, uint32_t message) { title_function<BreakV>(kBreak)(level, tstr(message)); }
void* crt_fopen(const char* path, uint32_t mode) { return title_function<CrtFopen>(0x001effb3)(path, tstr(mode)); }
void crt_strncpy(char* dst, const char* src, uint32_t n) { title_function<CrtStrncpy>(0x001eed80)(dst, src, n); }
const char* crt_strstr(const char* s, uint32_t sub) { return title_function<CrtStrstr>(0x001efb10)(s, tstr(sub)); }
uint8_t* cvar_ptr(uint32_t global) { return title_global<uint8_t*>(global); }
int32_t cvar_int(uint32_t global) { return pk<int32_t>(cvar_ptr(global), 0x20); }
const char* cvar_str(uint32_t global) { return reinterpret_cast<const char*>(pk_ptr(cvar_ptr(global), 4)); }

// fs_searchpaths node: +0 next, +4 pack, +8 directory, +0xc localized, +0x10 language.
// directory: +0 path, +0x80 gamedir. pack: +0x100 base name, +0x194 hash size, +0x198 hash table;
// pack file entries: +0x10 name, +0x14 next.
bool search_path_skipped(uint8_t* sp) {
    if (!pk<uint32_t>(sp, 0xc)) return false;
    if (cvar_int(0x00940a04)) return true;                     // fs_ignoreLocalized
    return pk<int32_t>(sp, 0x10) != cvar_int(0x0093f7fc);      // loc_language
}

}  // namespace

void XB_CDECL FS_Printf(int32_t f, const char* format, ...) {
    char text[0x1000];
    va_list args;
    va_start(args, format);
    title_function<int32_t XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    FS_Write(text, static_cast<int32_t>(std::strlen(text)), f);
}

int32_t XB_REGS3 XB_CDECL FS_Seek(uint32_t, uint32_t, int32_t offset, int32_t f, int32_t origin) {
    FileHandle& handle = file_handle(f);
    if (!handle.zip_file) {
        void* const file = handle.file;
        switch (origin) {
        case 0: return crt_fseek(file, offset, 1);  // FS_SEEK_CUR
        case 1: return crt_fseek(file, offset, 2);  // FS_SEEK_END
        case 2: return crt_fseek(file, offset, 0);  // FS_SEEK_SET
        default: return 0;
        }
    }
    auto rewind = [&] {
        unzSetCurrentFileInfoPosition(static_cast<UnzState*>(handle.file), static_cast<uint32_t>(handle.zip_info_pos));
        return unzOpenCurrentFile(static_cast<UnzState*>(handle.file));
    };
    if (!offset) {
        if (origin == 2) return rewind();
        if (origin == 0) return 0;
    }
    const int32_t pos = unztell(static_cast<UnzState*>(handle.file));
    int32_t remaining;
    if (origin == 0) {
        if (offset < 0) {
            rewind();
            remaining = pos + offset;
        } else {
            remaining = offset;
        }
    } else if (origin == 1) {
        if (FS_filelength(f) + offset < pos) {
            rewind();
            remaining = FS_filelength(f) + offset;
        } else {
            remaining = FS_filelength(f) + (offset - pos);
        }
    } else if (origin == 2) {
        if (offset < pos) {
            rewind();
            remaining = offset;
        } else {
            remaining = offset - pos;
        }
    } else {
        return -1;
    }
    char buffer[0x10000];
    while (remaining) {
        int32_t read;
        if (remaining < 0x10000) {
            read = FS_Read(buffer, remaining, f);
            remaining = 0;
        } else {
            read = FS_Read(buffer, 0x10000, f);
            remaining -= 0x10000;
        }
        if (!read) return -1;
    }
    return 0;
}

void XB_REGS3 XB_CDECL FS_CopyFile(char* to, uint32_t, const char* from) {
    if (crt_strstr(from, 0x00838094)) return;  // "journal.dat"
    if (crt_strstr(from, 0x00838084)) return;  // "journaldata.dat"
    void* f = crt_fopen(from, 0x00838080);     // "rb"
    if (!f) return;
    crt_fseek(f, 0, 2);
    const int32_t len = crt_ftell(f);
    crt_fseek(f, 0, 0);
    uint8_t* buf = title_function<uint8_t* XB_CDECL(int32_t)>(0x00042c10)(len);  // Z_Malloc
    if (crt_fread(buf, 1, static_cast<uint32_t>(len), f) != len) fs_break(0, 0x00838060);
    crt_fclose(f);
    if (!FS_CreatePath(to)) {
        f = crt_fopen(to, 0x0083805c);  // "wb"
        if (f) {
            if (crt_fwrite(buf, 1, static_cast<uint32_t>(len), f) != len) fs_break(0, 0x0083803c);
            crt_fclose(f);
        }
    }
    Z_Free(buf);
}

void XB_REGS3 XB_CDECL FS_AddGameDirectory(uint32_t, uint32_t, const char* dir, const char* path, int32_t localized,
                                           int32_t language) {
    char gamedir[0x80];
    if (localized) {
        const uint32_t index = language >= 0 && language < 0xe ? static_cast<uint32_t>(language) : 0;
        const uint32_t args[3] = {0x0082d7d0, u32(dir), title_global<uint32_t>(0x008d4ca8 + index * 8)};  // "%s_%s"
        Com_sprintf(gamedir, 0x80, args);
    } else {
        crt_strncpy(gamedir, dir, 0x7f);
        gamedir[0x7f] = 0;
    }
    for (uint8_t* sp = title_global<uint8_t*>(kSearchPaths); sp; sp = pk_ptr(sp, 0)) {
        uint8_t* const d = pk_ptr(sp, 8);
        if (!d || !path) continue;
        if (Q_stricmpn(reinterpret_cast<const char*>(d), path, 99999)) continue;
        const char* const game = reinterpret_cast<const char*>(pk_ptr(sp, 8) + 0x80);
        if (!game) continue;
        if (!Q_stricmpn(game, gamedir, 99999)) return;
    }
    if (localized) {
        char ospath[0x80];
        FS_BuildOSPath(gamedir, 0, path, tstr(0x0082dfeb), ospath, 0);  // ""
        ospath[std::strlen(ospath) - 1] = 0;
        if (!FS_DirectoryHasFiles(ospath)) return;
    } else {
        crt_strncpy(&title_global<char>(kFsGamedir), gamedir, 0x7f);
        title_global<char>(kFsGamedir + 0x7f) = 0;
    }
    const auto z_malloc = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10);
    uint8_t* const sp = z_malloc(0x14);
    Com_Memset(sp, 0, 0x14);
    char* const d = reinterpret_cast<char*>(z_malloc(0x100));
    Com_Memset(reinterpret_cast<uint8_t*>(d), 0, 0x100);
    pk<uint32_t>(sp, 8) = u32(d);
    crt_strncpy(d, path, 0x7f);
    d[0x7f] = 0;
    char* const game = reinterpret_cast<char*>(pk_ptr(sp, 8)) + 0x80;
    crt_strncpy(game, gamedir, 0x7f);
    game[0x7f] = 0;
    pk<int32_t>(sp, 0xc) = localized;
    pk<int32_t>(sp, 0x10) = language;
    uint8_t* list = title_global<uint8_t*>(kSearchPaths);
    if (localized && list) {
        while (pk_ptr(list, 0) && !pk<uint32_t>(pk_ptr(list, 0), 0xc)) list = pk_ptr(list, 0);
        pk<uint32_t>(sp, 0) = pk<uint32_t>(list, 0);
        pk<uint32_t>(list, 0) = u32(sp);
        return;
    }
    pk<uint32_t>(sp, 0) = u32(list);
    title_global<uint8_t*>(kSearchPaths) = sp;
}

int32_t XB_CDECL FS_FOpenFileRead(const char* filename, int32_t* file, int32_t unique, int32_t quiet) {
    uint32_t hash = 0;
    char netpath[0x80];
    if (!file) {
        for (uint8_t* sp = title_global<uint8_t*>(kSearchPaths); sp; sp = pk_ptr(sp, 0)) {
            if (search_path_skipped(sp)) continue;
            uint8_t* const pack = pk_ptr(sp, 4);
            if (pack) hash = FS_HashFileName(filename, pk<int32_t>(pack, 0x194));
            if (pk_ptr(sp, 4)) {
                uint8_t* entry = pk_ptr(pk_ptr(pk_ptr(sp, 4), 0x198), hash * 4);
                if (entry) {
                    for (; entry; entry = pk_ptr(entry, 0x14))
                        if (!FS_FilenameCompare(filename, reinterpret_cast<const char*>(pk_ptr(entry, 0x10)))) return 1;
                    continue;
                }
            }
            uint8_t* const dir = pk_ptr(sp, 8);
            if (!dir) continue;
            FS_BuildOSPath(reinterpret_cast<const char*>(dir + 0x80), 0, reinterpret_cast<const char*>(dir), filename,
                           netpath, static_cast<uint32_t>(quiet));
            void* f = crt_fopen(netpath, 0x00838080);  // "rb"
            if (!f) {
                char copy[0x80];
                std::strcpy(copy, netpath);
                for (uint32_t i = 0; i < std::strlen(copy); ++i)
                    if (copy[i] == '@') copy[i] = '$';
                f = crt_fopen(netpath, 0x00838080);  // the unaltered path again
                if (!f) continue;
            }
            crt_fclose(f);
            return 1;
        }
        return 0;
    }
    if (crt_strstr(filename, 0x008380a4) || crt_strstr(filename, 0x008380a0)) {  // ".." "::"
        *file = 0;
        return -1;
    }
    *file = FS_HandleForFile(static_cast<uint32_t>(quiet));
    file_handle(*file).unique = unique;
    for (uint8_t* sp = title_global<uint8_t*>(kSearchPaths); sp; sp = pk_ptr(sp, 0)) {
        if (search_path_skipped(sp)) continue;
        uint8_t* const pack = pk_ptr(sp, 4);
        if (pack) hash = FS_HashFileName(filename, pk<int32_t>(pack, 0x194));
        if (crt_strstr(filename, 0x00837fac)) {  // ".bsp": remember where the map comes from
            const char* source = nullptr;
            if (pack && pk<uint32_t>(pk_ptr(pack, 0x198), hash * 4))
                source = reinterpret_cast<const char*>(pack + 0x100);
            else if (pk_ptr(sp, 8))
                source = reinterpret_cast<const char*>(pk_ptr(sp, 8) + 0x80);
            if (source) {
                crt_strncpy(&title_global<char>(0x00940200), source, 0x7f);
                title_global<char>(0x0094027f) = 0;
            }
        }
        if (pack && pk<uint32_t>(pk_ptr(pack, 0x198), hash * 4)) continue;  // pack files are not opened here
        if (!pk_ptr(sp, 8)) continue;
        const char* const ext = COM_GetExtension(0, filename);
        if ((cvar_int(0x0093fcc4) || title_global<uint32_t>(0x0093fcc8)) && !pk<uint32_t>(sp, 0xc) &&  // fs_restrict
            !FS_IsCfgMenuOrDatFile(ext))
            continue;
        uint8_t* const dir = pk_ptr(sp, 8);
        FS_BuildOSPath(reinterpret_cast<const char*>(dir + 0x80), 0, reinterpret_cast<const char*>(dir), filename,
                       netpath, static_cast<uint32_t>(quiet));
        file_handle(*file).file = crt_fopen(netpath, 0x00838080);  // "rb"
        if (!file_handle(*file).file) continue;
        char* const name = file_handle(*file).name;
        crt_strncpy(name, filename, 0xff);
        name[0xff] = 0;
        file_handle(*file).zip_file = 0;
        if (cvar_int(0x0093f80c) && !Q_stricmp(cvar_str(0x0093f810), 0, reinterpret_cast<const char*>(dir))) {
            char copypath[0x80];  // fs_copyfiles: copy from the cd path to the base path
            FS_BuildOSPath(reinterpret_cast<const char*>(dir + 0x80), 0, cvar_str(0x009403e4), filename, copypath,
                           static_cast<uint32_t>(quiet));
            FS_CopyFile(copypath, 0, netpath);
        }
        return FS_filelength(*file);
    }
    *file = 0;
    return -1;
}

PLATFORM_ASM_ENTRY(FS_FOpenFileAppend_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(FS_FOpenFileAppend) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL FS_FOpenFileAppend(const char* filename) {
    const int32_t f = FS_HandleForFile(0);
    FileHandle& handle = file_handle(f);
    handle.zip_file = 0;
    crt_strncpy(handle.name, filename, 0xff);
    handle.name[0xff] = 0;
    char ospath[0x80];
    FS_BuildOSPath(&title_global<char>(kFsGamedir), 0, cvar_str(0x00947798), filename, ospath, 0);  // fs_homepath
    if (FS_CreatePath(ospath)) return 0;
    handle.file = crt_fopen(ospath, 0x0083800c);  // "ab"
    handle.handle_sync = 0;
    return handle.file ? f : 0;
}

PLATFORM_ASM_ENTRY(FS_FOpenFileWrite_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(FS_FOpenFileWrite) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL FS_FOpenFileWrite(const char* filename) {
    const int32_t f = FS_HandleForFile(0);
    FileHandle& handle = file_handle(f);
    handle.zip_file = 0;
    char ospath[0x80];
    FS_BuildOSPath(&title_global<char>(kFsGamedir), 0, cvar_str(0x00947798), filename, ospath, 0);  // fs_homepath
    if (FS_CreatePath(ospath)) return 0;
    handle.file = crt_fopen(ospath, 0x0083805c);  // "wb"
    crt_strncpy(handle.name, filename, 0xff);
    handle.name[0xff] = 0;
    handle.handle_sync = 0;
    return handle.file ? f : 0;
}

int32_t XB_CDECL FS_FOpenFileByMode(const char* qpath, int32_t* f, int32_t mode) {
    int32_t r = 0x1b39;  // the original's value for a bad mode
    int32_t sync = 0;
    switch (mode) {
    case 0:  // FS_READ
        title_global<int32_t>(0x00940280) = 1;
        r = FS_FOpenFileRead(qpath, f, 1, 0);
        break;
    case 1:  // FS_WRITE
        *f = FS_FOpenFileWrite(qpath);
        r = *f ? 0 : -1;
        break;
    case 3:  // FS_APPEND_SYNC
        sync = 1;
        [[fallthrough]];
    case 2:  // FS_APPEND
        *f = FS_FOpenFileAppend(qpath);
        r = *f ? 0 : -1;
        break;
    default:
        fs_break(0, 0x00837db8);
        break;
    }
    if (!f) return r;
    if (*f) {
        FileHandle& handle = file_handle(*f);
        if (handle.zip_file)
            file_handle(*f).zip_file_pos = unztell(static_cast<UnzState*>(handle.file));
        else
            file_handle(*f).zip_file_pos = crt_ftell(handle.file);
        file_handle(*f).file_size = r;
        file_handle(*f).streamed = 0;
    }
    file_handle(*f).handle_sync = sync;
    return r;
}

int32_t XB_CDECL FS_ReadFile(const char* qpath, void** buffer) {
    if (!qpath || !*qpath) fs_break(0, 0x00837f74);  // "FS_ReadFile with empty name"
    const auto journal_mode = [] {
        uint8_t* const journal = cvar_ptr(0x00940a00);  // com_journal
        return journal ? pk<int32_t>(journal, 0x20) : 0;
    };
    int32_t& load_stack = title_global<int32_t>(0x00940a0c);  // fs_loadStack
    int32_t len;
    char path[0x100];
    const char* name = qpath;
    bool is_config = false;
    if (crt_strstr(qpath, 0x00833cb4)) {  // ".cfg"
        is_config = true;
        if (cvar_ptr(0x00940a00) && journal_mode() == 2) {
            if (FS_Read(&len, 4, title_global<int32_t>(0x0093fcd4)) != 4) {  // com_journalDataFile
                if (buffer) *buffer = nullptr;
                return -1;
            }
            if (!len) {
                if (!buffer) return 1;
                *buffer = nullptr;
                return -1;
            }
            if (!buffer) return len;
            uint8_t* const buf = static_cast<uint8_t*>(Z_MallocDebug(static_cast<uint32_t>(len + 1)));
            *buffer = buf;
            if (FS_Read(buf, len, title_global<int32_t>(0x0093fcd4)) != len)
                fs_break(0, 0x00837f34);  // "EXE_ERR_JOURNAL_FILE_READ"
            ++load_stack;
            buf[len] = 0;
            return len;
        }
        title_function<int32_t XB_CDECL(char*, const char*, ...)>(0x001ee901)(path, tstr(0x00837f28), qpath);  // "config\\%s"
        name = path;
    }
    int32_t h;
    title_global<int32_t>(0x00940280) = 1;
    len = FS_FOpenFileRead(name, &h, 0, 0);
    if (!h) {
        if (buffer) *buffer = nullptr;
        if (is_config && cvar_ptr(0x00940a00) && journal_mode() == 1) {
            len = 0;
            FS_Write(&len, 4, title_global<int32_t>(0x0093fcd4));
            crt_fflush(file_handle(title_global<int32_t>(0x0093fcd4)).file);
        }
        return -1;
    }
    if (!buffer) {
        if (is_config && cvar_ptr(0x00940a00) && journal_mode() == 1) {
            FS_Write(&len, 4, title_global<int32_t>(0x0093fcd4));
            FS_Flush(title_global<int32_t>(0x0093fcd4));
        }
        FS_FCloseFile(h);
        return len;
    }
    ++load_stack;
    uint8_t* const buf = static_cast<uint8_t*>(Z_MallocDebug(static_cast<uint32_t>(len + 1)));
    *buffer = buf;
    FS_Read(buf, len, h);
    buf[len] = 0;
    FS_FCloseFile(h);
    if (is_config && cvar_ptr(0x00940a00) && journal_mode() == 1) {
        FS_Write(&len, 4, title_global<int32_t>(0x0093fcd4));
        FS_Write(buf, len, title_global<int32_t>(0x0093fcd4));
        FS_Flush(title_global<int32_t>(0x0093fcd4));
    }
    return len;
}

PLATFORM_ASM_ENTRY(Cmd_ExecuteString_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Cmd_ExecuteString) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Cmd_ExecuteString(const char* text) {
    const uint32_t& argc = title_global<uint32_t>(0x00934248);  // cmd_argc
    const auto argv = [](uint32_t i) { return title_global<const char*>(0x0092df90 + i * 4); };
    title_global<const char*>(0x00927f50) = text;
    Cmd_TokenizeString(text, 0);
    if (!argc) return;
    // The command list (cmd_functions 0x00929b04; +0 next, +4 name, +8 function), found ones moved to the front.
    uint8_t* prev = &title_global<uint8_t>(0x00929b04);
    for (uint8_t* cmd = pk_ptr(prev, 0); cmd; prev = cmd, cmd = pk_ptr(cmd, 0)) {
        const char* const name = reinterpret_cast<const char*>(pk_ptr(cmd, 4));
        if (!argv(0) || !name || Q_stricmpn(argv(0), name, 99999)) continue;
        pk<uint32_t>(prev, 0) = pk<uint32_t>(cmd, 0);
        pk<uint32_t>(cmd, 0) = title_global<uint32_t>(0x00929b04);
        title_global<uint8_t*>(0x00929b04) = cmd;
        const uint32_t function = pk<uint32_t>(cmd, 8);
        if (function) {
            title_function<void XB_CDECL()>(function)();
            return;
        }
        break;
    }
    // Cvar_Command
    uint8_t* const var = Cvar_FindVar(argc ? argv(0) : tstr(0x0082dfeb));
    if (var) {
        if (argc == 1) return;
        Cvar_Set2(reinterpret_cast<const char*>(pk_ptr(var, 0)),
                  const_cast<char*>(argc > 1 ? argv(1) : tstr(0x0082dfeb)), 0);
        return;
    }
    using VmCall = uint32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, uint32_t);
    uint8_t* const cg_cvar = cvar_ptr(0x00947704);
    if (cg_cvar && pk<int32_t>(cg_cvar, 0x20) && title_global<uint8_t*>(0x0093f7f8)) {
        if (title_function<VmCall>(0x002a84e0)(0, title_global<uint8_t*>(0x0093f7f8), 2)) return;  // VM_Call(cgvm, 2)
    }
    uint8_t* const sv_running = cvar_ptr(0x0093fcf0);  // com_sv_running
    if (sv_running && pk<int32_t>(sv_running, 0x20) && title_global<int32_t>(0x00964cd4) == 2) {
        if (title_function<VmCall>(0x002a84e0)(0, title_global<uint8_t*>(0x0095f50c), 0xc)) return;  // VM_Call(gvm, 12)
    }
    const char* first = tstr(0x0082dfeb);
    if (argc) {
        first = argv(0);
        if (*first == '-') return;
    }
    if (!title_global<int32_t>(0x0092bdf0)) return;  // cls.state
    if (*first == '+') return;
    CL_AddReliableCommand(static_cast<int32_t>(argc) > 1 ? text : first);
}

namespace {

// Cbuf_Execute's and Cbuf_ExecuteBuffer's line splitter: the next line or ';'-separated command (';' inside
// quotes kept) copied out, at most 0xfff characters; the rest moved to the front.
void cbuf_take_line(char* line, uint32_t data_global, uint32_t size_global) {
    char* const data = title_global<char*>(data_global);
    int32_t size = title_global<int32_t>(size_global);
    int32_t quotes = 0;
    int32_t i = 0;
    for (; i < size; ++i) {
        const char c = data[i];
        if (c == '"') ++quotes;
        if (!(quotes & 1) && c == ';') break;
        if (c == '\n' || c == '\r') break;
    }
    if (i >= 0xfff) i = 0xfff;
    std::memcpy(line, data, static_cast<uint32_t>(i));
    line[i] = 0;
    if (i == size) {
        title_global<int32_t>(size_global) = 0;
    } else {
        ++i;
        size -= i;
        title_global<int32_t>(size_global) = size;
        title_function<void* XB_CDECL(void*, const void*, uint32_t)>(0x001ef0c0)(data, data + i,
                                                                               static_cast<uint32_t>(size));  // _memmove
    }
}

}  // namespace

void XB_CDECL Cbuf_ExecuteBuffer() {
    uint8_t* const sv_running = cvar_ptr(0x0093fcf0);  // com_sv_running
    if (sv_running && pk<int32_t>(sv_running, 0x20) && title_global<uint32_t>(0x0092f548)) return;
    char line[0x1000];
    while (title_global<int32_t>(0x00929c18)) {
        cbuf_take_line(line, 0x00929c10, 0x00929c18);
        Cmd_ExecuteRegisteredCommand(line);
    }
}

PLATFORM_ASM_ENTRY(Cbuf_Execute_entry,
                   "movl %esp, %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Cbuf_Execute) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Cbuf_Execute(uint32_t entry_esp) {
    // Cmd_ExecuteString leaves the line's address in 0x00927f50 (the current command text): the original's
    // line buffer is at ((entry - 4) & ~7) - 0x1000, so that address is what stays there afterwards.
    const uint32_t original_line = ((entry_esp - 4) & ~7u) - 0x1000;
    char line[0x1000];
    while (title_global<int32_t>(0x00935330)) {  // cmd_text.cursize
        int32_t& wait = title_global<int32_t>(0x00929bfc);  // cmd_wait
        if (wait) {
            --wait;
            break;
        }
        cbuf_take_line(line, 0x00935328, 0x00935330);
        Cmd_ExecuteString(line);
        if (title_global<uint32_t>(0x00927f50) == u32(line)) title_global<uint32_t>(0x00927f50) = original_line;
    }
    Cbuf_ExecuteBuffer();
}

PLATFORM_ASM_ENTRY(Cbuf_ExecuteText_entry,
                   "movl %esp, %eax\n subl $0x20000, %esp\n pushl %eax\n pushl %eax\n"
                   PLATFORM_ASM_CALL(Cbuf_ExecuteText) "addl $4, %esp\n popl %esp\n"
                   "testl %eax, %eax\n jz 1f\n jmp *%eax\n 1:\n ret")

uint32_t XB_CDECL Cbuf_ExecuteText(uint32_t entry_esp) {
    // Its Cbuf_Execute is a tail jump (at the entry ESP, where the line buffer address comes from): returned
    // as the target for the entry to jump to.
    int32_t* const args = reinterpret_cast<int32_t*>(uintptr_t{entry_esp + 4});
    const int32_t exec_when = args[0];
    const char* const text = reinterpret_cast<const char*>(uintptr_t{static_cast<uint32_t>(args[1])});
    switch (exec_when) {
    case 0:  // EXEC_NOW
        if (text) {
            const uint32_t length = static_cast<uint32_t>(std::strlen(text));
            args[0] = static_cast<int32_t>(length);  // the original keeps the length in this slot
            if (length) {
                Cmd_ExecuteString(text);
                return 0;
            }
        }
        return 0x002c62b0;  // Cbuf_Execute
    case 1:  // EXEC_INSERT
        Cbuf_InsertText(text);
        return 0;
    case 2:  // EXEC_APPEND
        Cbuf_AddText(text);
        return 0;
    default:
        args[0] = 0;
        args[1] = 0x00833cf4;
        fs_break(0, 0x00833cf4);  // "Cbuf_ExecuteText: bad exec_when"
        return 0;
    }
}

void XB_CDECL Com_ParseError(const char* format, ...) {
    char text[0x7d00];
    va_list args;
    va_start(args, format);
    title_function<int32_t XB_CDECL(char*, const char*, va_list)>(0x001eea95)(text, format, args);  // _vsprintf
    va_end(args);
    uint8_t* const parse = title_global<uint8_t*>(0x008d4ca0);  // current parse info: +0x80 line, +0x9c file name
    title_function<BreakV>(kBreak)(1, tstr(0x008338b0), parse + 0x9c, pk<int32_t>(parse, 0x80), text);
}

const char* XB_REGS1 XB_CDECL Loc_TrimQuotedValue(const char* text) {
    // A function-static std::string at 0x0098bcbc (+4 buffer or pointer, +0x14 size, +0x18 capacity).
    constexpr uint32_t kStr = 0x0098bcbc;
    using AssignN = void* XB_THISCALL(uint32_t, const char*, uint32_t);
    using Erase = void* XB_THISCALL(uint32_t, uint32_t, uint32_t);
    const auto c_str = [] {
        return title_global<uint32_t>(0x0098bcd4) >= 0x10 ? title_global<char*>(0x0098bcc0) : &title_global<char>(0x0098bcc0);
    };
    uint32_t& guard = title_global<uint32_t>(0x0098bcd8);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098bcd4) = 0xf;
        title_global<uint32_t>(0x0098bcd0) = 0;
        title_global<char>(0x0098bcc0) = 0;
        title_function<int32_t XB_CDECL(uint32_t)>(0x001eed36)(0x00379990);  // _atexit
    }
    title_function<AssignN>(0x00015920)(kStr, tstr(0x0082dfeb), 0);
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '"') ++text;
    title_function<AssignN>(0x00015920)(kStr, text, static_cast<uint32_t>(std::strlen(text)));
    if (*text) {
        for (;;) {
            const char* const s = c_str();
            const uint32_t n = static_cast<uint32_t>(std::strlen(s));
            const char last = s[n - 1];
            if (last == ' ' || last == '\t') {
                title_function<Erase>(0x00015110)(kStr, n - 1, 1);  // std_string_Erase
                continue;
            }
            if (last == '"') title_function<Erase>(0x00015110)(kStr, n - 1, 1);
            break;
        }
    }
    return c_str();
}

PLATFORM_ASM_ENTRY(Loc_ResetStringFileState_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Loc_ResetStringFileState) "addl $8, %esp\n"
                   "ret $4")

void* XB_CDECL Loc_ResetStringFileState(uint8_t* state, uint32_t keep_lists) {
    // +0 count, +4 / +0x20 std::string, +0x78 map<string>, +0x8c vector<string>, +0x98 set<string>.
    using EraseSubtree = void XB_THISCALL(uint8_t*, uint8_t*);
    using AssignN = void* XB_THISCALL(uint8_t*, const char*, uint32_t);
    const auto clear_tree = [](uint8_t* tree, uint32_t erase) {
        title_function<EraseSubtree>(erase)(tree, pk_ptr(pk_ptr(tree, 4), 4));
        uint8_t* head = pk_ptr(tree, 4);
        pk<uint32_t>(head, 4) = u32(head);
        head = pk_ptr(tree, 4);
        pk<uint32_t>(tree, 8) = 0;
        pk<uint32_t>(head, 0) = u32(head);
        head = pk_ptr(tree, 4);
        pk<uint32_t>(head, 8) = u32(head);
    };
    clear_tree(state + 0x78, 0x000200d0);  // std_map_string_EraseSubtree
    if (!keep_lists) {
        if (pk_ptr(state, 0x8c)) {
            std_string_DestroyRange(pk_ptr(state, 0x8c), pk_ptr(state, 0x90));
            Z_Free(pk_ptr(state, 0x8c));
        }
        pk<uint32_t>(state, 0x8c) = 0;
        pk<uint32_t>(state, 0x90) = 0;
        pk<uint32_t>(state, 0x94) = 0;
        clear_tree(state + 0x98, 0x00020610);  // std_set_string_EraseSubtree
    }
    pk<uint32_t>(state, 0) = 0;
    title_function<AssignN>(0x00015920)(state + 4, tstr(0x0082dfeb), 0);
    return title_function<AssignN>(0x00015920)(state + 0x20, tstr(0x0082dfeb), 0);
}

uint8_t XB_CDECL Save_HasEnoughFreeSpace() {
    uint32_t free_bytes[2] = {0, 0};
    const int32_t status = title_function<int32_t XB_CDECL(uint32_t*)>(0x000d1b40)(free_bytes);  // Save_GetFreeSpace
    const int32_t clusters = title_function<int32_t XB_CDECL(int32_t)>(0x000d1e10)(0x400);  // Save_ClustersForSize
    const auto cluster_size = title_function<int32_t XB_CDECL()>(0x000d1ec0);               // Save_GetClusterSize
    const int32_t partial = clusters * 4 + cluster_size() * 2;
    const int32_t size = cluster_size();
    const int32_t bytes = size + partial + size * 4;
    const int32_t needed = static_cast<int32_t>(BytesToBlocks(static_cast<uint32_t>(bytes), static_cast<uint32_t>(bytes >> 31)));
    const int32_t free_blocks = static_cast<int32_t>(BytesToBlocks(free_bytes[0], free_bytes[1]));
    if (status == 0) return needed <= free_blocks ? 1 : 0;
    return status == -10 ? 1 : 0;
}


void XB_CDECL FS_Startup(const char* game_name) {
    title_global<uint8_t*>(0x00940284) = Cvar_Get(tstr(0x00832c80), tstr(0x0082e098), 0);       // fs_debug "0"
    title_global<uint8_t*>(0x0093f80c) = Cvar_Get(tstr(0x00837ecc), tstr(0x0082e098), 0x10);    // fs_copyfiles "0"
    title_global<uint8_t*>(0x0093f810) = Cvar_Get(tstr(0x00837ec0), tstr(0x0083cad8), 0x10);    // fs_cdpath
    title_global<uint8_t*>(0x009403e4) = Cvar_Get(tstr(0x00837eb4), tstr(0x0082dfeb), 0x10);    // fs_basepath ""
    title_global<uint8_t*>(0x0093fc24) = Cvar_Get(tstr(0x00837ea8), tstr(0x0082dfeb), 0x10);    // fs_basegame ""
    title_global<uint8_t*>(0x00947798) = Cvar_Get(tstr(0x00837e9c), cvar_str(0x009403e4), 0x10);  // fs_homepath
    title_global<uint8_t*>(0x00940a08) = Cvar_Get(tstr(0x00837e94), tstr(0x0082dfeb), 0x18);    // fs_game ""
    title_global<uint8_t*>(0x0093fcc4) = Cvar_Get(tstr(0x00837e88), tstr(0x0082dfeb), 0x10);    // fs_restrict ""
    title_global<uint8_t*>(0x00940a04) = Cvar_Get(tstr(0x00837e74), tstr(0x0082e098), 0x220);   // "fs_ignoreLozalized" (sic)
    const auto add_all = [game_name](const char* path) {
        for (int32_t i = 0xd; i >= 0; --i) FS_AddGameDirectory(0, 0, game_name, path, 1, i);
        FS_AddGameDirectory(0, 0, game_name, path, 0, 0);
    };
    // FS_AddGameDirectories' C parameters are (EDI = the game folder, EBX = the base path).
    const auto add_dirs = [](const char* game, const char* path) { FS_AddGameDirectories(u32(game), u32(path)); };
    if (*cvar_str(0x0093f810)) add_all(cvar_str(0x0093f810));  // fs_cdpath
    if (*cvar_str(0x009403e4)) add_all(cvar_str(0x009403e4));  // fs_basepath
    if (*cvar_str(0x009403e4)) {
        const char* const home = cvar_str(0x00947798);
        const char* const base = cvar_str(0x009403e4);
        if (!home || !base || Q_stricmpn(home, base, 99999)) add_dirs(game_name, cvar_str(0x00947798));
    }
    const auto add_other = [&](uint32_t game_cvar) {
        if (*cvar_str(0x0093f810)) add_dirs(cvar_str(game_cvar), cvar_str(0x0093f810));
        if (*cvar_str(0x009403e4)) add_dirs(cvar_str(game_cvar), cvar_str(0x009403e4));
        if (!*cvar_str(0x00947798)) return;
        if (!Q_stricmp(cvar_str(0x009403e4), 0, cvar_str(0x00947798))) return;
        add_dirs(cvar_str(game_cvar), cvar_str(0x00947798));
    };
    if (*cvar_str(0x0093fc24) && game_name && !Q_stricmpn(game_name, tstr(0x0082dfeb), 99999)) {  // fs_basegame
        const char* const basegame = cvar_str(0x0093fc24);
        if (!basegame || Q_stricmpn(basegame, game_name, 99999)) add_other(0x0093fc24);
    }
    if (*cvar_str(0x00940a08) && game_name && !Q_stricmpn(game_name, tstr(0x0082dfeb), 99999)) {  // fs_game
        const char* const gamedir = cvar_str(0x00940a08);
        if (!gamedir || Q_stricmpn(gamedir, game_name, 99999)) add_other(0x00940a08);
    }
    pk<uint32_t>(cvar_ptr(0x00940a08), 0x14) = 0;  // fs_gamedirvar->modified
}

void XB_CDECL FS_Shutdown(int32_t close_files) {
    for (int32_t i = 1; i < 3; ++i)
        if (file_handle(i).file_size) FS_FCloseFile(i);
    uint32_t& search_paths = title_global<uint32_t>(kSearchPaths);
    uint32_t& packs = title_global<uint32_t>(0x0095f57c);
    if (close_files) {
        FS_FreeSearchPaths(reinterpret_cast<uint8_t*>(uintptr_t{search_paths}));
        FS_FreePackList(reinterpret_cast<uint8_t*>(uintptr_t{packs}));
        title_global<uint32_t>(0x0095f580) = 0;
        title_global<uint32_t>(0x0095f584) = 0;
    } else {
        if (title_global<uint32_t>(0x0095f580) != search_paths)
            FS_FreeSearchPaths(reinterpret_cast<uint8_t*>(uintptr_t{search_paths}));
        if (title_global<uint32_t>(0x0095f584) != packs) FS_FreePackList(reinterpret_cast<uint8_t*>(uintptr_t{packs}));
    }
    search_paths = 0;
    packs = 0;
    Cmd_RemoveCommand(tstr(0x00837f00));  // "path"
    Cmd_RemoveCommand(tstr(0x00837ef4));  // "fullpath"
    Cmd_RemoveCommand(tstr(0x00837ef0));  // "dir"
    Cmd_RemoveCommand(tstr(0x00837ee8));  // "fdir"
    Cmd_RemoveCommand(tstr(0x00837edc));  // "touchFile"
}

namespace {

// FS_Restart's and FS_InitFilesystem's last valid base path (0x00940360) and game (0x00947718), 0x80 each.
void fs_remember_valid_paths() {
    crt_strncpy(&title_global<char>(0x00940360), cvar_str(0x009403e4), 0x7f);
    title_global<char>(0x009403df) = 0;
    crt_strncpy(&title_global<char>(0x00947718), cvar_str(0x00940a08), 0x7f);
    title_global<char>(0x00947797) = 0;
}

}  // namespace

void XB_CDECL FS_Restart(int32_t checksum_feed) {
    FS_Shutdown(0);
    title_global<int32_t>(0x0093fcdc) = checksum_feed;
    FS_Startup(tstr(0x0082dfeb));
    if (FS_ReadFile(tstr(0x00837e5c), nullptr) <= 0) {  // "default.cfg"
        if (title_global<char>(0x00940360)) {
            Cvar_Set2(tstr(0x00837eb4), &title_global<char>(0x00940360), 1);  // fs_basepath
            Cvar_Set2(tstr(0x00837e00), &title_global<char>(0x00947718), 1);  // "fs_gamedirvar"
            title_global<char>(0x00940360) = 0;
            title_global<char>(0x00947718) = 0;
            Cvar_Set2(tstr(0x00837e88), const_cast<char*>(tstr(0x0082e098)), 1);  // fs_restrict "0"
            FS_Restart(checksum_feed);
            fs_break(1, 0x00837de8);  // "Invalid game folder\n"
        }
        title_function<BreakV>(kBreak)(0, tstr(0x00837e10), tstr(0x00837e5c));  // "Couldn't load %s. ..."
    }
    const char* const game = cvar_str(0x00940a08);
    if (!game || Q_stricmpn(game, &title_global<char>(0x00947718), 99999)) {
        if (!Com_SafeMode()) {
            const uint32_t args[2] = {0x00837dd4, 0x00837de0};  // "exec %s\n" "bro.cfg"
            Cbuf_AddText(va(args));
        }
    }
    fs_remember_valid_paths();
}

void XB_CDECL FS_InitFilesystem() {
    Com_StartupVariable(tstr(0x00837ec0));  // fs_cdpath
    Com_StartupVariable(tstr(0x00837eb4));  // fs_basepath
    Com_StartupVariable(tstr(0x00837e9c));  // fs_homepath
    Com_StartupVariable(tstr(0x00837e94));  // fs_game
    Com_StartupVariable(tstr(0x00837ecc));  // fs_copyfiles
    Com_StartupVariable(tstr(0x00837e88));  // fs_restrict
    Com_StartupVariable(tstr(0x00837e68));  // fs_usewolf
    Com_StartupVariable(tstr(0x0083349c));  // cl_language
    Loc_InitCvars();
    FS_Startup(tstr(0x0082dfeb));
    if (FS_ReadFile(tstr(0x00837e5c), nullptr) <= 0)  // "default.cfg"
        title_function<BreakV>(kBreak)(0, tstr(0x00837e10), tstr(0x00837e5c));
    fs_remember_valid_paths();
    std::memset(&title_global<uint8_t>(0x00940200), 0, 0x80);
}


// Calls a title function with EAX/EBX/ECX/EDX/ESI/EDI from regs and `count` stack words (args[0] pushed last);
// returns its EAX. ESP is restored from EBP, so stdcall callees work too.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL qc_call_regs(uint32_t function, const TitleRegs* regs, uint32_t count,
                                                          const uint32_t* args);
PLATFORM_ASM_ENTRY(qc_call_regs,
                   "pushl %ebp\n movl %esp, %ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n pushl 8(%ebp)\n"
                   "movl 16(%ebp), %ecx\n movl 20(%ebp), %edx\n"
                   "1:\n testl %ecx, %ecx\n jz 2f\n decl %ecx\n pushl (%edx,%ecx,4)\n jmp 1b\n"
                   "2:\n movl 12(%ebp), %eax\n movl 4(%eax), %ebx\n movl 8(%eax), %ecx\n movl 12(%eax), %edx\n"
                   "movl 16(%eax), %esi\n movl 20(%eax), %edi\n movl (%eax), %eax\n"
                   "call *-16(%ebp)\n"
                   "leal -12(%ebp), %esp\n popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret")

int32_t XB_CDECL Com_EventLoop() {
    // msg_t: +0 overflow flag, +4 data, +8 maxsize, +0xc cursize, +0x10 readcount.
    uint8_t data[0x800];
    std::memset(data, 0, sizeof(data));
    uint32_t buf[5] = {0, u32(data), 0x800, 0, 0};
    uint32_t from[5] = {0, 0, 0, 0, 0};  // netadr_t
    uint8_t* const msg = reinterpret_cast<uint8_t*>(buf);
    SysEvent ev;
    for (;;) {
        Com_GetEvent(&ev);
        if (!ev.type) break;
        switch (ev.type) {
        case 1:  // SE_KEY
            CL_KeyEvent(ev.value2, 0, ev.value, ev.time);
            break;
        case 2:  // SE_CHAR
            if (ev.value == '`' || ev.value == '~') break;
            if ((title_global<uint8_t>(0x0092bdf4) & 1) || !title_global<int32_t>(0x0092bdf0))  // keyCatchers, state
                CL_CharEvent(&title_global<uint8_t>(0x0092e830), ev.value);
            break;
        case 3:  // SE_MOUSE
            title_global<int32_t>(0x0096173c + title_global<uint32_t>(0x0096174c) * 4) += ev.value;
            title_global<int32_t>(0x00961744 + title_global<uint32_t>(0x0096174c) * 4) += ev.value2;
            break;
        case 4:  // SE_JOYSTICK_AXIS
            if (ev.value < 0 || ev.value >= 6) title_function<BreakV>(kBreak)(1, tstr(0x00835170), ev.value);
            title_global<int32_t>(0x00961750 + static_cast<uint32_t>(ev.value) * 4) = ev.value2;
            break;
        case 5: {  // SE_CONSOLE
            Cbuf_AddText(static_cast<const char*>(ev.ptr));
            Z_Free(static_cast<uint8_t*>(ev.ptr));
            const int32_t size = title_global<int32_t>(0x00935330);
            if (size + 1 < title_global<int32_t>(0x0093532c)) {
                title_global<char*>(0x00935328)[size] = title_global<char>(0x0082d288);  // '\n'
                title_global<int32_t>(0x00935330) = size + 1;
            }
            break;
        }
        case 6: {  // SE_PACKET: netadr_t, then the bytes
            const uint32_t* const packet = static_cast<const uint32_t*>(ev.ptr);
            for (int32_t i = 0; i < 5; ++i) from[i] = packet[i];
            const uint32_t len = static_cast<uint32_t>(ev.ptr_length - 0x14);
            buf[3] = len;
            if (len > buf[2]) {
                Z_Free(static_cast<uint8_t*>(ev.ptr));
                break;
            }
            uint8_t* const dst = reinterpret_cast<uint8_t*>(uintptr_t{buf[1]});
            std::memcpy(dst, packet + 5, len);
            Z_Free(static_cast<uint8_t*>(ev.ptr));
            if (cvar_int(0x0093fcf0)) {  // com_sv_running: Com_RunAndTimeServerPacket (from in ESI)
                const TitleRegs regs{0, from[0], 0, 0, u32(from), u32(dst + len)};
                const uint32_t args[1] = {u32(msg)};
                qc_call_regs(0x002a2510, &regs, 1, args);
            } else {
                CL_PacketEvent(from[0], from[1], from[2], from[3], from[4], msg);
            }
            break;
        }
        default:
            title_function<BreakV>(kBreak)(0, tstr(0x00837d1c), ev.type);  // "Com_EventLoop: bad event type %i"
            break;
        }
    }
    while (NET_GetLoopPacket(0, msg, from)) CL_PacketEvent(from[0], from[1], from[2], from[3], from[4], msg);
    while (NET_GetLoopPacket(1, msg, from)) {
        if (!cvar_int(0x0093fcf0)) continue;  // com_sv_running
        uint8_t* speeds = cvar_ptr(0x009403e8);  // com_speeds
        if (pk<int32_t>(speeds, 0x20)) title_function<int32_t XB_CDECL()>(kSysMilliseconds)();
        const TitleRegs regs{u32(msg), u32(msg), 0, 0, 0, static_cast<uint32_t>(ev.time)};
        qc_call_regs(0x002c9de0, &regs, 5, from);  // SV_PacketEvent (msg in EAX, from by value)
        speeds = cvar_ptr(0x009403e8);
        if (pk<int32_t>(speeds, 0x20)) title_function<int32_t XB_CDECL()>(kSysMilliseconds)();
    }
    return ev.time;
}

void XB_CDECL Com_Frame() {
    title_function<void XB_STDCALL(uint32_t)>(0x0021bae0)(0x0098aa90);  // Actor_UpdateBlockingPairs
    if (title_global<uint8_t>(0x008d50dd)) {
        // The pause screen: 0x008d50dc set = waiting to resume.
        if (!title_global<uint8_t>(0x008d50dc)) {
            if (title_global<uint32_t>(0x0095f8f4)) {
                if (!title_global<uint8_t>(0x0095f538)) {
                    S_PauseAllChannels(title_global<uint8_t*>(0x0095f4c4));
                    const int32_t was_paused = cvar_int(0x0093fcd8);
                    title_global<uint8_t>(0x0095f539) = was_paused ? 1 : 0;
                    Cvar_Set2(tstr(0x0082ca00), const_cast<char*>(tstr(0x0082e058)), 1);  // cl_paused "1"
                    title_global<uint8_t>(0x0095f538) = 1;
                    const TitleRegs regs{0, 0, 0, 0, 0x0095f8d0, static_cast<uint32_t>(was_paused)};
                    qc_call_regs(0x0030f190, &regs, 0, nullptr);  // FUN_0030f190 (g_uiRoot in ESI)
                }
                S_UpdateChannels(title_global<uint8_t*>(0x0095f4c4));
                S_CheckWaveBankNotifies(title_global<uint8_t*>(0x0095f4c0));  // rm_soundBanks
                title_function<void XB_CDECL()>(0x000ca690)();                // File_PumpRequests
                return;
            }
        } else if (title_global<uint8_t>(0x0095f538)) {
            IN_PadCreate();
            if (!IN_PadButtonPressed(title_global<int32_t>(0x008d50d8), 0xe)) return;
            S_ResumeAllChannels(title_global<uint8_t*>(0x0095f4c4));
            SCR_RedrawFrontBuffer();
            Cvar_Set2(tstr(0x0082ca00), const_cast<char*>(tstr(title_global<uint8_t>(0x0095f539) ? 0x0082e058 : 0x0082e098)),
                      1);  // cl_paused back
            uint8_t* const live = title_global<uint8_t*>(0x0095f6fc);  // g_liveMgr
            title_function<void XB_THISCALL(uint8_t*)>(pk<uint32_t>(pk_ptr(live, 0), 0x1c))(live);
            title_global<uint8_t>(0x0095f538) = 0;
            if (!title_global<uint8_t>(0x0095f919)) return;
            const int32_t state = pk<int32_t>(title_global<uint8_t*>(0x0095f8fc), 0xc);
            if (state == 2 || state == 3 || state == 4 || state == 5) title_global<uint8_t>(0x009403f4) = 1;
            return;
        }
    }
    if (cvar_int(0x0093fc20) && title_global<uint32_t>(0x00940280)) title_global<uint32_t>(0x00940280) = 0;
    if (pk<uint32_t>(cvar_ptr(0x0093fcb4), 0x14)) pk<uint32_t>(cvar_ptr(0x0093fcb4), 0x14) = 0;
    title_global<int32_t>(0x0095f4b8) = cvar_int(0x009472e8);
    const auto sys_milliseconds = title_function<int32_t XB_CDECL()>(kSysMilliseconds);
    if (cvar_int(0x009403e8)) sys_milliseconds();  // com_speeds
    int32_t min_msec = 1;
    if (cvar_int(0x0094770c) > 0) min_msec = 1000 / cvar_int(0x0094770c);  // com_maxfps
    int32_t& last_time = title_global<int32_t>(0x0098f2d4);
    int32_t time;
    int32_t raw_msec;
    do {
        time = Com_EventLoop();
        title_global<int32_t>(0x00947710) = time;  // com_frameTime
        int32_t last = last_time;
        if (last > time) {
            last = time;
            last_time = time;
        }
        raw_msec = time - last;
    } while (raw_msec < min_msec);
    last_time = time;
    title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute (its entry needs the real ESP)
    if (cvar_int(0x0093fcbc)) Cvar_Set2(tstr(0x0082ca00), const_cast<char*>(tstr(0x0082e098)), 1);  // cl_paused "0"
    int32_t msec = Com_ModifyMsec(0, 0, raw_msec);
    if (cvar_int(0x009403e8)) sys_milliseconds();
    if (title_global<uint32_t>(0x0095f6c0)) msec = 0;
    ++title_global<int32_t>(0x008dda98);
    title_global<int32_t>(0x008dda9c) = msec > 1 ? msec : 1;
    const float frame_msec = static_cast<float>(msec);
    const float thousand = title_global<float>(0x0083eb9c);
    const float seconds = frame_msec / thousand;
    title_global<float>(0x008ddaa4) = thousand / frame_msec;
    title_global<float>(0x008ddaa0) = seconds;
    title_global<float>(0x008ddaa8) = title_global<float>(0x008ddaa8) + seconds;
    title_function<void XB_CDECL()>(0x0029ed00)();  // Scr_PruneEntityWaitLists
    title_function<void XB_CDECL()>(0x00251f90)();  // G_ProcessCheckpointTriggers
    title_global<float>(0x0092708c) =
        static_cast<float>(static_cast<double>(msec) * title_global<double>(0x0083ef88) + title_global<float>(0x0092708c));
    if (title_global<uint32_t>(0x0095f474) && title_global<float>(0x0092708c) > title_global<float>(0x00926e24))
        Subtitle_Start(nullptr, tstr(0x00926e30));
    {
        const float step = pk<int32_t>(Cvar_Get(tstr(0x0082ca00), tstr(0x0082e098), 0), 0x20) ? 0.0f
                                                                                  : frame_msec * title_global<float>(0x0083eb4c);
        uint32_t step_bits;
        std::memcpy(&step_bits, &step, 4);
        const TitleRegs regs{0, static_cast<uint32_t>(raw_msec), 0, 0, 0x008dd1d8, static_cast<uint32_t>(msec)};
        qc_call_regs(0x0030b9e0, &regs, 1, &step_bits);  // Scr_RunThreads (ESI = 0x008dd1d8)
    }
    {
        const TitleRegs regs{0, static_cast<uint32_t>(raw_msec), 0, 0, static_cast<uint32_t>(msec), static_cast<uint32_t>(msec)};
        qc_call_regs(0x002f4400, &regs, 0, nullptr);  // SV_Frame (msec in ESI)
    }
    if (cvar_int(0x009403e8)) sys_milliseconds();
    Com_EventLoop();
    title_function<void XB_CDECL()>(0x002c62b0)();  // Cbuf_Execute
    if (cvar_int(0x009403e8)) sys_milliseconds();
    {
        const TitleRegs regs{static_cast<uint32_t>(raw_msec), static_cast<uint32_t>(raw_msec), 0, 0,
                             static_cast<uint32_t>(msec), static_cast<uint32_t>(msec)};
        qc_call_regs(0x002f2630, &regs, 0, nullptr);  // CL_Frame (msec in EAX)
    }
    if (cvar_int(0x0093fc20) || cvar_int(0x009403e8)) title_global<int32_t>(0x0098f2d0) = sys_milliseconds();
    ++title_global<int32_t>(0x00947b0c);  // com_frameNumber
    if (cvar_int(0x0093fcbc)) {  // frame advance: pause again
        Cvar_Set2(tstr(0x0082ca00), const_cast<char*>(tstr(0x0082e058)), 1);  // cl_paused "1"
        Cvar_Set2(tstr(0x00837c0c), const_cast<char*>(tstr(0x0082e098)), 1);  // cl_frameadvance "0"
    }
}


void XB_CDECL Com_InitJournaling() {
    Com_StartupVariable(tstr(0x00837d68));  // "journal"
    uint8_t* const journal = Cvar_Get(tstr(0x00837d68), tstr(0x0082e098), 0x10);
    title_global<uint8_t*>(0x00940a00) = journal;  // com_journal
    const int32_t mode = pk<int32_t>(journal, 0x20);
    if (!mode) return;
    int32_t& journal_file = title_global<int32_t>(0x0093fce0);
    int32_t& data_file = title_global<int32_t>(0x0093fcd4);
    if (mode == 1) {
        journal_file = FS_FOpenFileWrite(tstr(0x00838094));  // "journal.dat"
        data_file = FS_FOpenFileWrite(tstr(0x00838084));     // "journaldata.dat"
    } else if (mode == 2) {
        title_global<int32_t>(0x00940280) = 1;
        FS_FOpenFileRead(tstr(0x00838094), &journal_file, 1, 0);
        title_global<int32_t>(0x00940280) = 1;
        FS_FOpenFileRead(tstr(0x00838084), &data_file, 1, 0);
    }
    if (journal_file && data_file) return;
    Cvar_Set2(tstr(0x00837d5c), const_cast<char*>(tstr(0x0082e098)), 1);  // com_journal "0"
    journal_file = 0;
    data_file = 0;
}

void XB_CDECL Com_Quit_f() {
    if (title_global<uint32_t>(0x00947b08)) return;
    if (++title_global<int32_t>(0x0093fc28) == 0) title_global<int32_t>(0x0093fc28) = 1;
    title_global<uint32_t>(0x0095f510) = 0;
    title_global<uint32_t>(0x0095f4ec) = 0;
    CL_Shutdown();
    SV_ShutdownServer();
    Com_Shutdown();
    FS_Shutdown(1);
}

void XB_CDECL Cmd_Exec_f() {
    const uint32_t& argc = title_global<uint32_t>(0x00934248);  // cmd_argc
    if (argc != 2) return;
    if (crt_strstr(title_global<const char*>(0x0092df94), 0x00833cec)) {  // "style_": the control style
        const char* const arg = argc > 1 ? title_global<const char*>(0x0092df94) : tstr(0x0082dfeb);
        int32_t& style = title_global<int32_t>(0x008dce7c);
        if (!std::strcmp(arg, tstr(0x00833ce0)))         // "style_h.cfg"
            style = 0;
        else if (!std::strcmp(arg, tstr(0x00833cd4)))    // "style_e.cfg"
            style = 1;
        else if (!std::strcmp(arg, tstr(0x00833cc8)))    // "style_f.cfg"
            style = 2;
        else
            style = std::strcmp(Cmd_Argv(1), tstr(0x00833cbc)) ? 4 : 3;  // "style_g.cfg"
    }
    char filename[0x80];
    crt_strncpy(filename, argc > 1 ? title_global<const char*>(0x0092df94) : tstr(0x0082dfeb), 0x7f);
    filename[0x7f] = 0;
    COM_DefaultExtension(filename, 0x80, tstr(0x00833cb4));  // ".cfg"
    void* buffer = nullptr;
    FS_ReadFile(filename, &buffer);
    if (!buffer) return;
    Cbuf_InsertText(static_cast<const char*>(buffer));
    --title_global<int32_t>(0x00940a0c);  // fs_loadStack
    Z_Free(static_cast<uint8_t*>(buffer));
}

void XB_CDECL Cvar_Set_f() {
    const uint32_t& argc = title_global<uint32_t>(0x00934248);  // cmd_argc
    const auto argv = [&argc](uint32_t i) { return i < argc ? title_global<const char*>(0x0092df90 + i * 4) : tstr(0x0082dfeb); };
    if (static_cast<int32_t>(argc) < 3) return;
    char combined[0x1000];
    combined[0] = 0;
    int32_t length = 0;
    for (uint32_t i = 2; static_cast<int32_t>(i) < static_cast<int32_t>(argc); ++i) {
        const int32_t len = static_cast<int32_t>(std::strlen(argv(i) + 1));  // Q3's strlen(Cmd_Argv(i) + 1)
        if (len + length >= 0xffe) break;
        std::strcat(combined, argv(i));
        if (i != argc - 1) std::strcat(combined, tstr(0x0082d6e4));  // " "
        length += len;
    }
    Cvar_Set2(argv(1), combined, 0);
}

char** XB_CDECL FS_ListFilteredFiles(const char* path, const char* extension, const char* filter, int32_t* numfiles) {
    if (!path) {
        *numfiles = 0;
        return nullptr;
    }
    if (!extension) extension = tstr(0x0082dfeb);
    if (*path == '/' || *path == '\\') ++path;
    const int32_t is_dir_filter = extension && !Q_stricmpn(extension, tstr(0x0082d70c), 99999) ? 1 : 0;  // "/"
    char netpath[0x100];
    int32_t depth;
    FS_ReturnPath(0, 0, path, netpath, &depth);
    int32_t nfiles = 0;
    char* list[0x1000];
    for (uint8_t* sp = title_global<uint8_t*>(kSearchPaths); sp; sp = pk_ptr(sp, 0)) {
        if (search_path_skipped(sp)) continue;
        if (pk_ptr(sp, 4)) continue;  // packs are not listed
        if (!pk_ptr(sp, 8)) continue;
        if (cvar_int(0x0093fcc4) || title_global<uint32_t>(0x0093fcc8)) {  // fs_restrict: "svg" only
            if (!extension || Q_stricmpn(extension, tstr(0x00832f64), 99999)) continue;
        }
        uint8_t* const dir = pk_ptr(sp, 8);
        FS_BuildOSPath(reinterpret_cast<const char*>(dir + 0x80), 0, reinterpret_cast<const char*>(dir), path, netpath, 0);
        int32_t count = 0;
        using SysListFiles = char** XB_REGS3 XB_CDECL(uint32_t, const char*, const char*, const char*, int32_t*, int32_t);
        char** const files = title_function<SysListFiles>(0x00223bf0)(0, filter, extension, netpath, &count, is_dir_filter);
        for (int32_t i = 0; i < count; ++i) nfiles = FS_AddFileToList(files[i], list, nfiles);
        if (files) {
            for (char** f = files; *f; ++f) Z_Free(reinterpret_cast<uint8_t*>(*f));
            Z_Free(reinterpret_cast<uint8_t*>(files));
        }
    }
    *numfiles = nfiles;
    if (!nfiles) return nullptr;
    const uint32_t bytes = static_cast<uint32_t>(nfiles * 4 + 4);
    char** const result = title_function<char** XB_CDECL(uint32_t)>(0x00042c10)(bytes);  // Z_Malloc
    Com_Memset(reinterpret_cast<uint8_t*>(result), 0, bytes);
    for (int32_t i = 0; i < nfiles; ++i) result[i] = list[i];
    result[nfiles] = nullptr;
    return result;
}


// --- qcommon push 5 (high half): script containers ---

namespace {

uint32_t ptr_count4(const uint32_t* first, const uint32_t* last) {
    return static_cast<uint32_t>(static_cast<int32_t>(u32(last) - u32(first)) >> 2);
}

// 0x0045a330 (a copy in the PROLOGUE section): count 8-byte elements {0, g_scrUndefinedBool}.
uint32_t* alloc8_fill_undefined(uint32_t count) {
    const uint32_t bytes = count << 3;
    uint32_t* const data = Scr_Alloc(bytes, 4);
    uint8_t* const end = reinterpret_cast<uint8_t*>(data) + bytes;
    for (uint8_t* element = reinterpret_cast<uint8_t*>(data); element != end; element += 8) {
        if (!element) continue;
        *reinterpret_cast<uint32_t*>(element) = 0;
        element[4] = title_global<uint8_t>(kScrUndefinedBool);
    }
    return data;
}

}  // namespace

void XB_THISCALL DynArray_Resize24(DynArray* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            uint8_t* element = reinterpret_cast<uint8_t*>(array->data) + old_size * 24;
            uint8_t* const end = reinterpret_cast<uint8_t*>(array->data) + size * 24;
            for (; element != end; element += 24) {
                if (!element) continue;
                uint32_t* const words = reinterpret_cast<uint32_t*>(element);
                for (int32_t i = 0; i < 6; ++i) words[i] = title_global<uint32_t>(kScrUndefinedFloat);
            }
        }
        array->size = size;
        return;
    }
    // 0x0045a2a0 (a PROLOGUE-section copy of DynArray_Alloc24_FillUndefined).
    uint32_t* const data = DynArray_Alloc24_FillUndefined(capacity, 0);
    for (uint32_t i = 0; i < (array->size >= size ? size : array->size); ++i)
        for (int32_t w = 0; w < 6; ++w) data[i * 6 + w] = array->data[i * 6 + w];
    if (array->data) Scr_Free(array->data);
    array->data = data;
    array->capacity = capacity;
    array->size = size;
}

uint32_t* XB_THISCALL DynArray_At24(DynArray* array, uint32_t index) {
    if (index >= array->size) DynArray_Resize24(array, index + 1, index + 4);
    return array->data + index * 6;
}

void XB_THISCALL DynArray_Resize8(DynArray* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            uint8_t* element = reinterpret_cast<uint8_t*>(array->data) + old_size * 8;
            uint8_t* const end = reinterpret_cast<uint8_t*>(array->data) + size * 8;
            for (; element != end; element += 8) {
                if (!element) continue;
                *reinterpret_cast<uint32_t*>(element) = 0;
                element[4] = title_global<uint8_t>(kScrUndefinedBool);
            }
        }
        array->size = size;
        return;
    }
    uint32_t* const data = alloc8_fill_undefined(capacity);
    for (uint32_t i = 0; i < (array->size >= size ? size : array->size); ++i) {
        data[i * 2] = array->data[i * 2];
        data[i * 2 + 1] = array->data[i * 2 + 1];
    }
    if (array->data) Scr_Free(array->data);
    array->data = data;
    array->capacity = capacity;
    array->size = size;
}

uint32_t* XB_THISCALL DynArray_At(DynArray* array, uint32_t index) {
    if (index >= array->size) DynArray_Resize8(array, index + 1, index + 4);
    return array->data + index * 2;
}

uint8_t** XB_THISCALL std_list_Erase(uint8_t* list, uint8_t** out, uint8_t* node) {
    // Nodes: +0 next, +4 prev, +0xc the value's script block; list +4 head, +8 size.
    uint8_t* const next = pk_ptr(node, 0);
    if (node != pk_ptr(list, 4)) {
        pk<uint32_t>(pk_ptr(node, 4), 0) = pk<uint32_t>(node, 0);
        pk<uint32_t>(pk_ptr(node, 0), 4) = pk<uint32_t>(node, 4);
        if (pk_ptr(node, 0xc)) Scr_Free(pk_ptr(node, 0xc));  // 0x0045e770, the value's destructor
        Scr_Free(node);
        --pk<int32_t>(list, 8);
    }
    *out = next;
    return out;
}

void XB_THISCALL std_vector_InsertN4(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value_ptr) {
    // MSVC vector<dword>::_Insert_n; the value is copied first (into its argument slot in the original). The
    // seven instances differ only in which copies of the helpers they call. No exception handler in C: the
    // original's catch (free the new block, rethrow) needs an allocation failure Scr_Alloc never reports.
    const uint32_t value = *value_ptr;
    uint32_t* const first = vector->first;
    const uint32_t capacity = first ? ptr_count4(first, vector->end) : 0;
    if (!count) return;
    const auto size = [vector, first] { return first ? ptr_count4(first, vector->last) : 0; };
    if (0x3fffffff - size() < count) title_function<void XB_THISCALL(Vector4*)>(0x0017ce30)(vector);  // std_vector_Xlen
    if (capacity < size() + count) {
        uint32_t new_capacity = 0x3fffffff - capacity / 2 < capacity ? 0 : capacity + capacity / 2;
        if (new_capacity < size() + count) new_capacity = size() + count;
        const uint32_t bytes = new_capacity << 2;
        uint32_t* const data = Scr_Alloc(bytes, 4);
        uint32_t* const middle = std_UninitializedCopy4(vector->first, where, data);
        std_UninitializedFillN4(middle, count, &value);
        std_UninitializedCopy4(where, vector->last, middle + count);
        uint32_t* const old = vector->first;
        const uint32_t total = count + (old ? ptr_count4(old, vector->last) : 0);
        if (old) Scr_Free(old);
        vector->end = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(data) + bytes);
        vector->last = data + total;
        vector->first = data;
        return;
    }
    uint32_t* const last = vector->last;
    if (ptr_count4(where, last) < count) {
        std_UninitializedCopy4(where, last, where + count);
        std_UninitializedFillN4(vector->last, count - ptr_count4(where, vector->last), &value);
        vector->last = vector->last + count;
        std_Fill4(where, vector->last - count, &value);
        return;
    }
    uint32_t* const source = last - count;
    vector->last = std_UninitializedCopy4(source, last, last);
    std_CopyBackward4(where, source, last);
    std_Fill4(where, where + count, &value);
}

void XB_THISCALL std_vector_Resize4(Vector4* vector, uint32_t size, uint32_t value) {
    const uint32_t old_size = vector->first ? ptr_count4(vector->first, vector->last) : 0;
    if (old_size < size) {
        std_vector_InsertN4(vector, vector->last, size - old_size, &value);
        return;
    }
    if (vector->first && size < ptr_count4(vector->first, vector->last)) {
        uint32_t* out;
        std_vector_Erase4(vector, &out, vector->first + size, vector->last);
    }
}

uint8_t XB_THISCALL std_vector_Buy4(Vector4* vector, uint32_t count) {
    vector->first = nullptr;
    vector->last = nullptr;
    vector->end = nullptr;
    if (!count) return 0;
    if (count > 0x3fffffff) title_function<void XB_THISCALL(Vector4*)>(0x0017ce30)(vector);  // std_vector_Xlen
    uint32_t* const data = Scr_Alloc(count << 2, 4);
    vector->end = data + count;
    vector->first = data;
    vector->last = data;
    return 1;
}


// The other instances (same bodies, other copies of the helpers).
void XB_THISCALL std_vector_InsertN4_2(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value) {
    std_vector_InsertN4(vector, where, count, value);
}
void XB_THISCALL std_vector_Resize4_2(Vector4* vector, uint32_t size, uint32_t value) {
    std_vector_Resize4(vector, size, value);
}
void XB_THISCALL std_vector_InsertN4_3(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value) {
    std_vector_InsertN4(vector, where, count, value);
}
void XB_THISCALL std_vector_Resize4_3(Vector4* vector, uint32_t size, uint32_t value) {
    std_vector_Resize4(vector, size, value);
}
void XB_THISCALL std_vector_InsertN4_4(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value) {
    std_vector_InsertN4(vector, where, count, value);
}
void XB_THISCALL std_vector_Resize4_4(Vector4* vector, uint32_t size, uint32_t value) {
    std_vector_Resize4(vector, size, value);
}
void XB_THISCALL std_vector_InsertN4_5(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value) {
    std_vector_InsertN4(vector, where, count, value);
}
void XB_THISCALL std_vector_Resize4_5(Vector4* vector, uint32_t size, uint32_t value) {
    std_vector_Resize4(vector, size, value);
}
void XB_THISCALL std_vector_InsertN4_6(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value) {
    std_vector_InsertN4(vector, where, count, value);
}
void XB_THISCALL std_vector_Resize4_6(Vector4* vector, uint32_t size, uint32_t value) {
    std_vector_Resize4(vector, size, value);
}
void XB_THISCALL std_vector_InsertN4_7(Vector4* vector, uint32_t* where, uint32_t count, const uint32_t* value) {
    std_vector_InsertN4(vector, where, count, value);
}
void XB_THISCALL std_vector_Resize4_7(Vector4* vector, uint32_t size, uint32_t value) {
    std_vector_Resize4(vector, size, value);
}

// --- Script-heap hash map instances ---

namespace {

// Callee addresses per template instance (the instances share one body).
struct HashMapInstance {
    uint32_t buy_head;      // head node allocator (cdecl, no arguments)
    uint32_t clear;         // list clear (thiscall)
    uint32_t insert_range;  // list insert(where, first, last) (thiscall, callee pops 0x10)
    uint32_t resize;        // bucket vector resize(count, value) (thiscall, callee pops 8)
    uint32_t construct_n;   // bucket vector(count, &value) (thiscall, callee pops 8)
    uint32_t copy_from;     // this instance's HashMap_CopyFrom
};

HashMap* hash_map_ctor(HashMap* map, const uint8_t* traits, const HashMapInstance& inst) {
    map->traits = *traits;
    map->head = title_function<HashNode* XB_CDECL()>(inst.buy_head)();
    map->size = 0;
    const uint32_t head = u32(map->head);  // the original keeps it in its own argument slot
    title_function<void XB_THISCALL(uint32_t*, uint32_t, const uint32_t*)>(inst.construct_n)(
        &map->vector_allocator, 9, &head);
    map->mask = 1;
    map->max_index = 1;
    return map;
}

void hash_map_copy_from(HashMap* map, const HashMap* source, const HashMapInstance& inst) {
    const uint32_t count = source->buckets ? static_cast<uint32_t>(source->buckets_last - source->buckets) : 0;
    title_function<void XB_THISCALL(uint32_t*, uint32_t, uint32_t)>(inst.resize)(&map->vector_allocator, count,
                                                                                 u32(map->head));
    map->mask = source->mask;
    map->max_index = source->max_index;
    title_function<void XB_THISCALL(uint32_t*)>(inst.clear)(&map->list_allocator);
    title_function<void XB_THISCALL(uint32_t*, HashNode*, HashNode*, HashNode*, const HashMap*)>(inst.insert_range)(
        &map->list_allocator, map->head, source->head->next, source->head, source);
    map->traits = source->traits;
    HashNode* copy = map->head->next;
    HashNode* node = source->head->next;
    for (uint32_t i = 0; map->buckets && i < static_cast<uint32_t>(map->buckets_last - map->buckets);) {
        if (node == source->buckets[i]) {
            map->buckets[i] = copy;
            ++i;
        } else {
            copy = copy->next;
            node = node->next;
        }
    }
}

HashMap* hash_map_copy_ctor(HashMap* map, const HashMap* source, const HashMapInstance& inst) {
    map->traits = source->traits;
    map->head = title_function<HashNode* XB_CDECL()>(inst.buy_head)();
    map->size = 0;
    map->buckets = nullptr;
    map->buckets_last = nullptr;
    map->buckets_end = nullptr;
    title_function<void XB_THISCALL(HashMap*, const HashMap*)>(inst.copy_from)(map, source);
    return map;
}

// buy_head, clear, insert_range, resize, construct_n, copy_from
constexpr HashMapInstance kHashMap1{0x0017bc80, 0x0045f3a0, 0x0017e210, 0x0017ddd0, 0x0017e190, 0x0017e460};
constexpr HashMapInstance kHashMap2{0x0078ae80, 0x0045ca00, 0x0017e2c0, 0x0078c630, 0, 0x0017e570};
constexpr HashMapInstance kHashMap3{0x0045ca80, 0x00461490, 0x0017f4e0, 0x0017e370, 0, 0x0017f750};
constexpr HashMapInstance kHashMap4{0x0017bc80, 0x00462260, 0x0017f860, 0x0017ddd0, 0, 0x001800f0};
constexpr HashMapInstance kHashMap5{0x0017bc80, 0x004622e0, 0x0017f910, 0x0017ddd0, 0, 0x00180200};
// HashMap_Ctor/_2/_3 only: the head allocator and the bucket vector constructor.
constexpr HashMapInstance kHashMapCtor1{0x0017bc80, 0, 0, 0, 0x0017e190, 0};
constexpr HashMapInstance kHashMapCtor2{0x0017bc80, 0, 0, 0, 0x0017e090, 0};
constexpr HashMapInstance kHashMapCtor3{0x0017bc80, 0, 0, 0, 0x0017e110, 0};

void vector_construct_n4(Vector4* vector, uint32_t count, const uint32_t* value) {
    if (!std_vector_Buy4(vector, count)) return;
    uint32_t* const first = vector->first;
    std_UninitializedFillN4(first, count, value);
    vector->last = first + count;
}

}  // namespace

void XB_THISCALL std_vector_ConstructN4(Vector4* vector, uint32_t count, const uint32_t* value) {
    vector_construct_n4(vector, count, value);
}
void XB_THISCALL std_vector_ConstructN4_2(Vector4* vector, uint32_t count, const uint32_t* value) {
    vector_construct_n4(vector, count, value);
}
void XB_THISCALL std_vector_ConstructN4_3(Vector4* vector, uint32_t count, const uint32_t* value) {
    vector_construct_n4(vector, count, value);
}

HashMap* XB_THISCALL HashMap_Ctor(HashMap* map, const uint8_t* traits, uint32_t) {
    return hash_map_ctor(map, traits, kHashMapCtor1);
}
HashMap* XB_THISCALL HashMap_Ctor_2(HashMap* map, const uint8_t* traits, uint32_t) {
    return hash_map_ctor(map, traits, kHashMapCtor2);
}
HashMap* XB_THISCALL HashMap_Ctor_3(HashMap* map, const uint8_t* traits, uint32_t) {
    return hash_map_ctor(map, traits, kHashMapCtor3);
}

void XB_THISCALL HashMap_CopyFrom(HashMap* map, const HashMap* source) { hash_map_copy_from(map, source, kHashMap1); }
void XB_THISCALL HashMap_CopyFrom_2(HashMap* map, const HashMap* source) { hash_map_copy_from(map, source, kHashMap2); }
void XB_THISCALL HashMap_CopyFrom_3(HashMap* map, const HashMap* source) { hash_map_copy_from(map, source, kHashMap3); }
void XB_THISCALL HashMap_CopyFrom_4(HashMap* map, const HashMap* source) { hash_map_copy_from(map, source, kHashMap4); }
void XB_THISCALL HashMap_CopyFrom_5(HashMap* map, const HashMap* source) { hash_map_copy_from(map, source, kHashMap5); }

HashMap* XB_THISCALL HashMap_CopyCtor(HashMap* map, const HashMap* source) {
    return hash_map_copy_ctor(map, source, kHashMap1);
}
HashMap* XB_THISCALL HashMap_CopyCtor_2(HashMap* map, const HashMap* source) {
    return hash_map_copy_ctor(map, source, kHashMap2);
}
HashMap* XB_THISCALL HashMap_CopyCtor_3(HashMap* map, const HashMap* source) {
    return hash_map_copy_ctor(map, source, kHashMap3);
}
HashMap* XB_THISCALL HashMap_CopyCtor_4(HashMap* map, const HashMap* source) {
    return hash_map_copy_ctor(map, source, kHashMap4);
}
HashMap* XB_THISCALL HashMap_CopyCtor_5(HashMap* map, const HashMap* source) {
    return hash_map_copy_ctor(map, source, kHashMap5);
}

// --- Script-heap list instances (the hash maps' node lists) ---

namespace {

// MSVC's _Xlen/length_error throw: the message in a local std::string, a length_error built from it and
// _CxxThrowException (never returns; no caller in the title reaches it with valid sizes).
[[noreturn]] void std_throw_length_error(uint32_t message, uint32_t length) {
    alignas(4) uint8_t text[0x1c] = {};
    alignas(4) uint8_t error[0x28] = {};
    pk<uint32_t>(text, 0x18) = 0xf;
    pk<uint32_t>(text, 0x14) = 0;
    text[4] = 0;
    std_string_AssignN(text, tstr(message), length);
    std_exception_CtorString(error, text);
    pk<uint32_t>(error, 0) = 0x007f7784;  // std::length_error vtable
    title_function<void XB_STDCALL(uint8_t*, uint32_t)>(0x001f2302)(error, 0x00895af4);  // _CxxThrowException
    for (;;) {
    }
}

uint32_t list_inc_size(uint8_t* list, uint32_t count, uint32_t max_size) {
    const uint32_t size = pk<uint32_t>(list, 8);
    if (max_size - size < count) std_throw_length_error(0x00831848, 0x10);  // "list<T> too long"
    pk<uint32_t>(list, 8) = size + count;
    return size + count;
}

// list::erase(node): unlinks and frees any node but the head; `destroy` is the value's destructor (thiscall
// on the node).
uint8_t** list_erase(uint8_t* list, uint8_t** out, uint8_t* node, uint32_t destroy) {
    uint8_t* const next = pk_ptr(node, 0);
    if (node != pk_ptr(list, 4)) {
        pk<uint32_t>(pk_ptr(node, 4), 0) = pk<uint32_t>(node, 0);
        pk<uint32_t>(pk_ptr(node, 0), 4) = pk<uint32_t>(node, 4);
        title_function<void XB_THISCALL(uint8_t*)>(destroy)(node);
        Scr_Free(node);
        --pk<int32_t>(list, 8);
    }
    *out = next;
    return out;
}

// list::insert(where, first, last): one node per source node (copy of its key + value at +8) linked in
// before `where`; `buy_node` is stdcall (next, prev, &value), `inc_size` thiscall (count).
void list_insert_range(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t buy_node,
                       uint32_t inc_size) {
    for (uint8_t* node = first; node != last; node = pk_ptr(node, 0)) {
        uint8_t* const copy = title_function<uint8_t* XB_STDCALL(uint8_t*, uint8_t*, uint8_t*)>(buy_node)(
            where, pk_ptr(where, 4), node + 8);
        title_function<uint32_t XB_THISCALL(uint8_t*, uint32_t)>(inc_size)(list, 1);
        pk<uint32_t>(where, 4) = u32(copy);
        pk<uint32_t>(pk_ptr(copy, 4), 0) = u32(copy);
    }
}

// list node {next, prev, key, value at +0xc}: the value copy-constructed (HashMap, 0x34-byte node) or
// zeroed and assigned (DynArray of hash maps, 0x18-byte node).
uint8_t* list_buy_node_hash_map(uint8_t* next, uint8_t* prev, const uint32_t* value, uint32_t copy_ctor) {
    uint8_t* const node = reinterpret_cast<uint8_t*>(Scr_Alloc(0x34, 4));
    if (node) {
        pk<uint32_t>(node, 0) = u32(next);
        pk<uint32_t>(node, 4) = u32(prev);
        pk<uint32_t>(node, 8) = value[0];
        title_function<void XB_THISCALL(uint8_t*, const uint32_t*)>(copy_ctor)(node + 0xc, value + 1);
    }
    return node;
}

uint8_t* list_buy_node_key_array(uint8_t* next, uint8_t* prev, const uint32_t* value, uint32_t assign) {
    uint8_t* const node = reinterpret_cast<uint8_t*>(Scr_Alloc(0x18, 4));
    if (node) {
        pk<uint32_t>(node, 0) = u32(next);
        pk<uint32_t>(node, 4) = u32(prev);
        pk<uint32_t>(node, 8) = value[0];
        pk<uint32_t>(node, 0xc) = 0;
        pk<uint32_t>(node, 0x10) = 0;
        pk<uint32_t>(node, 0x14) = 0;
        title_function<void XB_THISCALL(uint8_t*, const uint32_t*)>(assign)(node + 0xc, value + 1);
    }
    return node;
}

}  // namespace

void XB_CDECL std_vector_Xlen() { std_throw_length_error(0x00831834, 0x12); }  // "vector<T> too long"

uint32_t XB_THISCALL std_list_IncSize(uint8_t* list, uint32_t count) { return list_inc_size(list, count, 0x05d1745d); }
uint32_t XB_THISCALL std_list_IncSize_2(uint8_t* list, uint32_t count) { return list_inc_size(list, count, 0x0fffffff); }

void XB_THISCALL std_list_InsertRange(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t) {
    list_insert_range(list, where, first, last, 0x0017cd90, 0x0017d8d0);  // BuyNodeKeyArray, IncSize_2
}
void XB_THISCALL std_list_InsertRange_2(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t) {
    list_insert_range(list, where, first, last, 0x0078aec0, 0x0078bd30);  // BUCHHOLZ copies
}
void XB_THISCALL std_list_InsertRange_3(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t) {
    list_insert_range(list, where, first, last, 0x0017f320, 0x0017d5e0);  // BuyNodeHashMap, IncSize
}
void XB_THISCALL std_list_InsertRange_4(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t) {
    list_insert_range(list, where, first, last, 0x0017f610, 0x0017d8d0);  // BuyNodeKeyArray_2, IncSize_2
}
void XB_THISCALL std_list_InsertRange_5(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t) {
    list_insert_range(list, where, first, last, 0x0017f6b0, 0x0017d8d0);  // BuyNodeKeyArray_3, IncSize_2
}

uint8_t** XB_THISCALL std_list_Erase_2(uint8_t* list, uint8_t** out, uint8_t* node) {
    return list_erase(list, out, node, 0x0045f600);
}
uint8_t** XB_THISCALL std_list_Erase_3(uint8_t* list, uint8_t** out, uint8_t* node) {
    return list_erase(list, out, node, 0x004610d0);
}
uint8_t** XB_THISCALL std_list_Erase_4(uint8_t* list, uint8_t** out, uint8_t* node) {
    return list_erase(list, out, node, 0x004614d0);
}

uint8_t* XB_STDCALL std_list_BuyNodeHashMap(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_hash_map(next, prev, value, 0x0017eb90);  // HashMap_CopyCtor_2
}
uint8_t* XB_STDCALL std_list_BuyNodeHashMap_2(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_hash_map(next, prev, value, 0x0017eb20);  // HashMap_CopyCtor
}
uint8_t* XB_STDCALL std_list_BuyNodeHashMap_3(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_hash_map(next, prev, value, 0x00180310);  // HashMap_CopyCtor_4
}
uint8_t* XB_STDCALL std_list_BuyNodeHashMap_4(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_hash_map(next, prev, value, 0x00180820);  // HashMap_CopyCtor_5
}
uint8_t* XB_STDCALL std_list_BuyNodeKeyArray_2(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_key_array(next, prev, value, 0x0017f020);  // DynArray_AssignHashMap
}
uint8_t* XB_STDCALL std_list_BuyNodeKeyArray_3(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_key_array(next, prev, value, 0x0017f230);  // DynArray_AssignHashMap_2
}

// --- Script-heap hash map insert / operator[] ---

namespace {

// hash_map::insert(value) (linear hashing): when the list holds 4+ nodes per bucket in use, first one split
// step - the bucket vector doubles when full (mask = 2 * count - 3), else the mask doubles once max_index
// passes it; the nodes of bucket max_index - mask/2 - 1 that hash elsewhere move to the list's end (the new
// bucket max_index + 1), and max_index grows. Then the value's bucket is searched from its end for the key
// (buckets are sorted); a new node goes before the first larger key. `out` = {node, inserted byte}.
// resize: thiscall (count, value); inc_size: thiscall (count); buy_node: stdcall (next, prev, &value).
template <typename Key>
uint8_t* hash_map_insert(HashMap* map, uint8_t* out, const uint32_t* value, uint32_t resize, uint32_t inc_size,
                         uint32_t buy_node) {
    const auto bucket_count = [map] {
        return map->buckets ? static_cast<uint32_t>(map->buckets_last - map->buckets) : 0;
    };
    const uint32_t max_index = map->max_index;
    if (max_index <= map->size >> 2) {
        if (bucket_count() - 1 <= max_index) {
            const uint32_t mask = bucket_count() * 2 - 3;
            map->mask = mask;
            title_function<void XB_THISCALL(uint32_t*, uint32_t, uint32_t)>(resize)(&map->vector_allocator, mask + 2,
                                                                                    u32(map->head));
        } else if (map->mask < max_index) {
            map->mask = map->mask * 2 + 1;
        }
        const uint32_t bucket = map->max_index - (map->mask >> 1) - 1;
        HashNode* node = map->buckets[bucket];
        if (node != map->buckets[bucket + 1]) {
            for (;;) {
                if (((node->key ^ 0xdeadbeef) & map->mask) == bucket) {
                    node = node->next;
                } else {
                    HashNode* const next = node->next;
                    HashNode* last = node;
                    if (next != map->head) {
                        if (node == map->buckets[bucket]) {
                            for (uint32_t i = bucket;;) {
                                map->buckets[i] = next;
                                if (!i) break;
                                --i;
                                if (node != map->buckets[i]) break;
                            }
                        }
                        // splice(end(), node): the original's same-list check (IncSize(0)) never runs.
                        HashNode* const head = map->head;
                        node->prev->next = next;
                        next->prev->next = head;
                        head->prev->next = node;
                        HashNode* const old_last = head->prev;
                        head->prev = next->prev;
                        next->prev = node->prev;
                        node->prev = old_last;
                        last = map->head->prev;
                        map->buckets[map->max_index + 1] = map->head;
                    }
                    for (uint32_t i = map->max_index; bucket < i; --i) {
                        if (map->buckets[i] != map->head) break;
                        map->buckets[i] = last;
                    }
                    if (next == map->head) break;
                    node = next;
                }
                if (node == map->buckets[bucket + 1]) break;
            }
        }
        ++map->max_index;
    }

    const uint32_t key = value[0];
    uint32_t index = (key ^ 0xdeadbeef) & map->mask;
    if (map->max_index <= index) index += 0xffffffffu - (map->mask >> 1);
    HashNode* node = map->buckets[index + 1];
    if (node != map->buckets[index]) {
        for (;;) {
            node = node->prev;
            const Key node_key = static_cast<Key>(node->key);
            if (node_key <= static_cast<Key>(key)) {
                if (node_key < static_cast<Key>(key)) {
                    node = node->next;
                    break;
                }
                pk<uint32_t>(out, 0) = u32(node);
                out[4] = 0;
                return out;
            }
            if (node == map->buckets[index]) break;
        }
    }
    HashNode* const fresh = title_function<HashNode* XB_STDCALL(HashNode*, HashNode*, const uint32_t*)>(buy_node)(
        node, node->prev, value);
    title_function<uint32_t XB_THISCALL(uint32_t*, uint32_t)>(inc_size)(&map->list_allocator, 1);
    node->prev = fresh;
    fresh->prev->next = fresh;
    HashNode* const prev = node->prev;
    if (node == map->buckets[index]) {
        for (;;) {
            map->buckets[index] = prev;
            if (!index) break;
            --index;
            if (node != map->buckets[index]) break;
        }
    }
    pk<uint32_t>(out, 0) = u32(prev);
    out[4] = 1;
    return out;
}

HashNode* hash_map_find_node(HashMap* map, const uint32_t* key) {
    HashNode* node = nullptr;
    HashMap_Find(map, &node, key);
    return node;
}

// operator[] with a DynArray value: {key, empty array} copy-assigned from an empty array, inserted, the
// pair destroyed (Scr_Free of its data when `destroy` is 0, else that thiscall destructor on the pair).
uint8_t* hash_map_index_array(HashMap* map, const uint32_t* key, HashNode* (*find)(HashMap*, const uint32_t*),
                              uint32_t assign, uint32_t insert, uint32_t destroy) {
    HashNode* const found = find(map, key);
    if (found != map->head) return reinterpret_cast<uint8_t*>(found) + 0xc;
    uint32_t empty[3] = {0, 0, 0};
    uint32_t pair[4] = {*key, 0, 0, 0};
    title_function<void XB_THISCALL(uint32_t*, const uint32_t*)>(assign)(pair + 1, empty);
    uint8_t out[8] = {};
    title_function<uint8_t* XB_THISCALL(HashMap*, uint8_t*, const uint32_t*)>(insert)(map, out, pair);
    uint8_t* const node = pk_ptr(out, 0);
    if (!destroy) {
        if (pair[1]) Scr_Free(reinterpret_cast<void*>(uintptr_t{pair[1]}));
    } else {
        title_function<void XB_THISCALL(uint32_t*)>(destroy)(pair);
    }
    return node + 0xc;
}

// operator[] with a HashMap value: a temporary map built with traits = the found iterator word (the
// original passes its own argument slot, which now holds the head), copied into {key, map}, inserted, then
// both maps destroyed.
uint8_t* hash_map_index_map(HashMap* map, const uint32_t* key, uint32_t ctor, uint32_t copy_ctor, uint32_t insert,
                            uint32_t dtor) {
    HashNode* found = hash_map_find_node(map, key);
    if (found != map->head) return reinterpret_cast<uint8_t*>(found) + 0xc;
    alignas(4) uint8_t temp[0x28] = {};
    title_function<void XB_THISCALL(uint8_t*, HashNode**, HashNode**)>(ctor)(temp, &found, &found);
    alignas(4) uint8_t pair[0x2c] = {};
    pk<uint32_t>(pair, 0) = *key;
    title_function<void XB_THISCALL(uint8_t*, const uint8_t*)>(copy_ctor)(pair + 4, temp);
    uint8_t out[8] = {};
    title_function<uint8_t* XB_THISCALL(HashMap*, uint8_t*, const uint8_t*)>(insert)(map, out, pair);
    uint8_t* const node = pk_ptr(out, 0);
    title_function<void XB_THISCALL(uint8_t*)>(dtor)(pair + 4);
    title_function<void XB_THISCALL(uint8_t*)>(dtor)(temp);
    return node + 0xc;
}

HashNode* hash_map_find_node_signed(HashMap* map, const uint32_t* key) {
    HashNode* node = nullptr;
    HashMap_Find_2(map, &node, reinterpret_cast<const int32_t*>(key));
    return node;
}

}  // namespace

uint8_t* XB_THISCALL HashMap_Insert_2(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017dc30, 0x0017d8d0, 0x0017bc40);
}
uint8_t* XB_THISCALL HashMap_Insert_3(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017ddd0, 0x0017d8d0, 0x0017cd90);
}
uint8_t* XB_THISCALL HashMap_Insert_4(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017e370, 0x0017d5e0, 0x0017f320);
}
uint8_t* XB_THISCALL HashMap_Insert_5(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017dca0, 0x0017d5e0, 0x0017f3b0);
}
uint8_t* XB_THISCALL HashMap_Insert_6(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017ddd0, 0x0017d8d0, 0x0017f610);
}
uint8_t* XB_THISCALL HashMap_Insert_7(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017ddd0, 0x0017d8d0, 0x0017f6b0);
}
uint8_t* XB_THISCALL HashMap_Insert_8(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017dbc0, 0x0017d5e0, 0x00180a10);
}
uint8_t* XB_THISCALL HashMap_Insert_9(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<uint32_t>(map, out, value, 0x0017dd10, 0x0017d5e0, 0x00180aa0);
}
uint8_t* XB_THISCALL HashMap_Insert_11(HashMap* map, uint8_t* out, const uint32_t* value) {
    return hash_map_insert<int32_t>(map, out, value, 0x001e6760, 0x001e6470, 0x001e63d0);
}

uint8_t* XB_THISCALL HashMap_Index(HashMap* map, const uint32_t* key) {
    HashNode* const found = hash_map_find_node(map, key);
    if (found != map->head) return reinterpret_cast<uint8_t*>(found) + 0xc;
    // {key, three script "undefined" floats} (bits copied).
    const uint32_t undefined = title_global<uint32_t>(kScrUndefinedFloat);
    const uint32_t pair[4] = {*key, undefined, undefined, undefined};
    uint8_t out[8] = {};
    HashMap_Insert_2(map, out, pair);
    return pk_ptr(out, 0) + 0xc;
}
uint8_t* XB_THISCALL HashMap_Index_2(HashMap* map, const uint32_t* key) {
    return hash_map_index_array(map, key, hash_map_find_node, 0x0017ba30, 0x0017e8d0, 0);
}
uint8_t* XB_THISCALL HashMap_Index_3(HashMap* map, const uint32_t* key) {
    return hash_map_index_array(map, key, hash_map_find_node_signed, 0x0045c270, 0x001e6a00, 0);
}
uint8_t* XB_THISCALL HashMap_Index_4(HashMap* map, const uint32_t* key) {
    return hash_map_index_map(map, key, 0x0078c930, 0x0017eb90, 0x0017fa30, 0x0045f1a0);
}
uint8_t* XB_THISCALL HashMap_Index_5(HashMap* map, const uint32_t* key) {
    return hash_map_index_map(map, key, 0x0017e3e0, 0x0017eb20, 0x0017fc80, 0x00460f10);
}
uint8_t* XB_THISCALL HashMap_Index_6(HashMap* map, const uint32_t* key) {
    return hash_map_index_map(map, key, 0x0017f590, 0x00180310, 0x00180b30, 0x00463c70);
}
uint8_t* XB_THISCALL HashMap_Index_7(HashMap* map, const uint32_t* key) {
    return hash_map_index_map(map, key, 0x0017fed0, 0x00180820, 0x00180d80, 0x00464a20);
}
uint8_t* XB_THISCALL HashMap_Index_8(HashMap* map, const uint32_t* key) {
    return hash_map_index_array(map, key, hash_map_find_node, 0x0017f020, 0x00180380, 0x00460370);
}
uint8_t* XB_THISCALL HashMap_Index_9(HashMap* map, const uint32_t* key) {
    return hash_map_index_array(map, key, hash_map_find_node, 0x0017f230, 0x001805d0, 0x004610e0);
}

// --- Localized-string tables (0x0095fe70) ---

void XB_CDECL StaticInit_LocStrings() {
    TitleRegs regs{};
    regs.esi = 0x0095fe70;
    qc_call_regs(0x0001d930, &regs, 0, nullptr);  // constructor (object in ESI)
    title_function<int32_t XB_CDECL(uint32_t)>(0x001eed36)(0x00379950);  // _atexit(AtExit_LocStrings)
}

void XB_CDECL AtExit_LocStrings() {
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x0001d9d0)(0x0095fe70);  // destructor
}

// --- qcommon push 7 (high half): arrays of hash maps, shuffles, 28/52-byte records ---

namespace {

// Arrays of hash maps: ctor thiscall (traits, unused), dtor and copy_from (operator=) thiscall; `dtor_assign` is
// the destructor address the assign instance calls (a jump thunk in one instance).
struct HashMapArrayInstance {
    uint32_t ctor, dtor, dtor_assign, copy_from;
    uint8_t* (XB_STDCALL* alloc)(uint32_t);
};

void hash_map_construct(uint8_t* element, uint32_t ctor) {
    // The original passes its own argument slot twice, holding the element's address at that point.
    uint32_t slot = u32(element);
    title_function<void XB_THISCALL(uint8_t*, uint32_t*, uint32_t*)>(ctor)(element, &slot, &slot);
}

uint8_t* dynarray_alloc_hash_map(uint32_t count, uint32_t ctor) {
    const uint32_t bytes = count * 0x28;
    uint8_t* const data = reinterpret_cast<uint8_t*>(Scr_Alloc(bytes, 4));
    for (uint8_t* element = data; element != data + bytes; element += 0x28)
        if (element) hash_map_construct(element, ctor);
    return data;
}

void dynarray_resize_hash_map(DynArray* array, uint32_t size, uint32_t capacity, const HashMapArrayInstance& inst) {
    uint8_t* const data = reinterpret_cast<uint8_t*>(array->data);
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            for (uint8_t* element = data + old_size * 0x28; element != data + size * 0x28; element += 0x28)
                if (element) hash_map_construct(element, inst.ctor);
        } else if (size < old_size) {
            for (uint8_t* element = data + size * 0x28; element != data + old_size * 0x28; element += 0x28)
                title_function<void XB_THISCALL(uint8_t*)>(inst.dtor)(element);
        }
        array->size = size;
        return;
    }
    uint8_t* const fresh = inst.alloc(capacity);
    uint8_t* copy = fresh;
    for (uint32_t i = 0, offset = 0;; ++i, offset += 0x28, copy += 0x28) {
        const uint32_t old_size = array->size;
        if (i >= (old_size >= size ? size : old_size)) break;
        uint8_t* const source = reinterpret_cast<uint8_t*>(array->data) + offset;
        if (copy != source) title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(inst.copy_from)(copy, source);
    }
    uint8_t* const old = reinterpret_cast<uint8_t*>(array->data);
    if (old) {
        for (uint8_t* element = old; element != old + array->size * 0x28; element += 0x28)
            title_function<void XB_THISCALL(uint8_t*)>(inst.dtor)(element);
        Scr_Free(array->data);
    }
    array->data = reinterpret_cast<uint32_t*>(fresh);
    array->capacity = capacity;
    array->size = size;
}

void dynarray_assign_hash_map(DynArray* array, const DynArray* source, const HashMapArrayInstance& inst) {
    uint8_t* const old = reinterpret_cast<uint8_t*>(array->data);
    if (old) {
        for (uint8_t* element = old; element != old + array->size * 0x28; element += 0x28)
            title_function<void XB_THISCALL(uint8_t*)>(inst.dtor_assign)(element);
        Scr_Free(array->data);
    }
    if (!source->size) {
        array->data = nullptr;
        array->size = 0;
        array->capacity = 0;
        return;
    }
    uint8_t* const fresh = inst.alloc(source->capacity);
    uint8_t* copy = fresh;
    for (int32_t i = 0, offset = 0; i < static_cast<int32_t>(source->size); ++i, offset += 0x28, copy += 0x28) {
        uint8_t* const from = reinterpret_cast<uint8_t*>(source->data) + offset;
        if (copy != from) title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(inst.copy_from)(copy, from);
    }
    array->data = reinterpret_cast<uint32_t*>(fresh);
    array->size = source->size;
    array->capacity = source->capacity;
}

const HashMapArrayInstance kHashMapArray1{0x0045f8b0, 0x0045ebe0, 0x0011aa90, 0x0045f650, &DynArray_AllocHashMap};
const HashMapArrayInstance kHashMapArray2{0x0045fc30, 0x0045ec50, 0x0045f220, 0x0045f760, &DynArray_AllocHashMap_2};

// Shuffle: element i swaps with element Scr_RandomInt(size), each access through At (resize thiscall
// (index + 1, index + 4) when index >= size).
template <uint32_t kSize>
void dynarray_shuffle(DynArray* array, uint32_t resize) {
    const auto at = [array, resize](uint32_t index) {
        if (index >= array->size)
            title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(resize)(array, index + 1, index + 4);
        return reinterpret_cast<uint8_t*>(array->data) + index * kSize;
    };
    if (static_cast<int32_t>(array->size) <= 0) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i) {
        const uint32_t j = static_cast<uint32_t>(title_function<int32_t XB_CDECL(int32_t)>(
            title_global<uint32_t>(0x0092222c))(static_cast<int32_t>(array->size)));  // Scr_RandomInt
        uint32_t temp[kSize / 4];
        std::memcpy(temp, at(i), kSize);
        const uint32_t* const other = reinterpret_cast<const uint32_t*>(at(j));
        uint32_t* const dest = reinterpret_cast<uint32_t*>(at(i));
        for (uint32_t w = 0; w < kSize / 4; ++w) dest[w] = other[w];
        std::memcpy(at(j), temp, kSize);
    }
}

// 28-byte record assignment: words copied, the two script strings assigned.
void record28_assign(uint8_t* dest, uint8_t* source) {
    pk<uint32_t>(dest, 0) = pk<uint32_t>(source, 0);
    title_function<void XB_THISCALL(uint8_t*, const uint8_t*)>(0x0011d600)(dest + 4, source + 4);  // ScriptString_Assign
    pk<uint32_t>(dest, 8) = pk<uint32_t>(source, 8);
    title_function<void XB_THISCALL(uint8_t*, const uint8_t*)>(0x0011d600)(dest + 0xc, source + 0xc);
    pk<uint32_t>(dest, 0x10) = pk<uint32_t>(source, 0x10);
    pk<uint32_t>(dest, 0x14) = pk<uint32_t>(source, 0x14);
    pk<uint32_t>(dest, 0x18) = pk<uint32_t>(source, 0x18);
}

void record28_release(uint8_t* record) {
    title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(record + 0xc);  // ScriptString_Release
    title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(record + 4);
}

constexpr uint32_t kRecord28Ctor = 0x0045bcd0;
constexpr uint32_t kRecord28Dtor = 0x00458fd0;

}  // namespace

uint8_t* XB_STDCALL DynArray_AllocHashMap(uint32_t count) { return dynarray_alloc_hash_map(count, 0x0045f8b0); }
uint8_t* XB_STDCALL DynArray_AllocHashMap_2(uint32_t count) { return dynarray_alloc_hash_map(count, 0x0045fc30); }

void XB_THISCALL DynArray_ResizeHashMap(DynArray* array, uint32_t size, uint32_t capacity) {
    dynarray_resize_hash_map(array, size, capacity, kHashMapArray1);
}
void XB_THISCALL DynArray_ResizeHashMap_2(DynArray* array, uint32_t size, uint32_t capacity) {
    dynarray_resize_hash_map(array, size, capacity, kHashMapArray2);
}
void XB_THISCALL DynArray_AssignHashMap(DynArray* array, const DynArray* source) {
    dynarray_assign_hash_map(array, source, kHashMapArray1);
}
void XB_THISCALL DynArray_AssignHashMap_2(DynArray* array, const DynArray* source) {
    dynarray_assign_hash_map(array, source, kHashMapArray2);
}

uint8_t* XB_THISCALL DynArray_AtHashMap(DynArray* array, uint32_t index) {
    if (index >= array->size) DynArray_ResizeHashMap(array, index + 1, index + 4);
    return reinterpret_cast<uint8_t*>(array->data) + index * 0x28;
}

void XB_CDECL DynArray_Shuffle(DynArray* array) { dynarray_shuffle<4>(array, 0x00015a90); }
void XB_CDECL DynArray_Shuffle_2(DynArray* array) { dynarray_shuffle<4>(array, 0x0045cad0); }
void XB_CDECL DynArray_Shuffle12(DynArray* array) { dynarray_shuffle<12>(array, 0x0045cde0); }
void XB_CDECL DynArray_ShuffleInt(DynArray* array) { dynarray_shuffle<4>(array, 0x0045cad0); }
void XB_CDECL DynArray_Shuffle_3(DynArray* array) { dynarray_shuffle<4>(array, 0x0045c4a0); }

void XB_CDECL DynArray_ShuffleString(DynArray* array) {
    constexpr uint32_t kResize = 0x0045c2e0;
    const auto at = [array](uint32_t index) {
        if (index >= array->size)
            title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(kResize)(array, index + 1, index + 4);
        return reinterpret_cast<uint8_t*>(array->data) + index * 4;
    };
    if (static_cast<int32_t>(array->size) <= 0) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i) {
        const uint32_t j = static_cast<uint32_t>(title_function<int32_t XB_CDECL(int32_t)>(
            title_global<uint32_t>(0x0092222c))(static_cast<int32_t>(array->size)));  // Scr_RandomInt
        uint32_t temp = 0;
        title_function<void XB_THISCALL(uint32_t*, uint8_t*)>(0x0011d5e0)(&temp, at(i));  // ScriptString_Copy
        uint8_t* const other = at(j);
        title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(0x0011d600)(at(i), other);  // ScriptString_Assign
        title_function<void XB_THISCALL(uint8_t*, uint32_t*)>(0x0011d600)(at(j), &temp);
        title_function<void XB_THISCALL(uint32_t*)>(0x0011d640)(&temp);  // ScriptString_Release
    }
}

void XB_THISCALL DynArray_Erase28(DynArray* array, uint8_t* where) {
    uint8_t* const end = reinterpret_cast<uint8_t*>(array->data) + array->size * 0x1c;
    if (where == end) return;
    uint8_t* const last = end - 0x1c;
    for (uint8_t* record = where; record != last; record += 0x1c) record28_assign(record, record + 0x1c);
    title_function<void XB_THISCALL(uint8_t*)>(kRecord28Dtor)(last);
    --array->size;
}

uint8_t* XB_STDCALL DynArray_Alloc28(uint32_t count, uint32_t) {
    const uint32_t bytes = count * 0x1c;
    uint8_t* const data = reinterpret_cast<uint8_t*>(Scr_Alloc(bytes, 4));
    for (uint8_t* record = data; record != data + bytes; record += 0x1c)
        if (record) title_function<void XB_THISCALL(uint8_t*)>(kRecord28Ctor)(record);
    return data;
}

void XB_THISCALL DynArray_Reserve28(DynArray* array, uint32_t size, uint32_t capacity) {
    uint8_t* const data = reinterpret_cast<uint8_t*>(array->data);
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            for (uint8_t* record = data + old_size * 0x1c; record != data + size * 0x1c; record += 0x1c)
                if (record) title_function<void XB_THISCALL(uint8_t*)>(kRecord28Ctor)(record);
        } else if (size < old_size) {
            for (uint8_t* record = data + size * 0x1c; record != data + old_size * 0x1c; record += 0x1c)
                record28_release(record);
        }
        array->size = size;
        return;
    }
    uint8_t* const fresh = title_function<uint8_t* XB_STDCALL(uint32_t)>(0x0045c120)(capacity);
    for (uint32_t i = 0;; ++i) {
        const uint32_t old_size = array->size;
        if (i >= (old_size >= size ? size : old_size)) break;
        record28_assign(fresh + i * 0x1c, reinterpret_cast<uint8_t*>(array->data) + i * 0x1c);
    }
    uint8_t* const old = reinterpret_cast<uint8_t*>(array->data);
    if (old) {
        for (uint8_t* record = old; record != old + array->size * 0x1c; record += 0x1c) record28_release(record);
        Scr_Free(array->data);
    }
    array->data = reinterpret_cast<uint32_t*>(fresh);
    array->capacity = capacity;
    array->size = size;
}

void XB_THISCALL DynArray_PushBack28(DynArray* array, const uint8_t* value) {
    if (array->size >= array->capacity) {
        const uint32_t capacity = array->size + 4;
        uint8_t* const fresh = DynArray_Alloc28(capacity, array->size + 1);
        for (uint32_t i = 0; i < array->size; ++i)
            record28_assign(fresh + i * 0x1c, reinterpret_cast<uint8_t*>(array->data) + i * 0x1c);
        uint8_t* const old = reinterpret_cast<uint8_t*>(array->data);
        if (old) {
            for (uint8_t* record = old; record != old + array->size * 0x1c; record += 0x1c)
                title_function<void XB_THISCALL(uint8_t*)>(kRecord28Dtor)(record);
            Scr_Free(array->data);
        }
        array->data = reinterpret_cast<uint32_t*>(fresh);
        array->capacity = capacity;
    }
    const uint32_t size = array->size;
    uint8_t* const record = reinterpret_cast<uint8_t*>(array->data) + size * 0x1c;
    array->size = size + 1;
    record28_assign(record, const_cast<uint8_t*>(value));
}

void XB_THISCALL DynArray_Resize52(DynArray* array, uint32_t size, uint32_t capacity) {
    if (size <= array->capacity) {
        const uint32_t old_size = array->size;
        if (size > old_size) {
            uint8_t* const data = reinterpret_cast<uint8_t*>(array->data);
            for (uint8_t* element = data + old_size * 0x34; element != data + size * 0x34; element += 0x34) {
                if (!element) continue;
                uint32_t* const words = reinterpret_cast<uint32_t*>(element);
                words[0] = title_global<uint32_t>(kScrUndefinedInt);
                words[1] = 0;
                words[2] = 0;
                words[3] = title_global<uint32_t>(0x00922eb4);
                for (int i = 4; i < 7; ++i) words[i] = title_global<uint32_t>(kScrUndefinedVecFloat);
                for (int i = 7; i < 13; ++i) words[i] = title_global<uint32_t>(kScrUndefinedFloat);
            }
        }
        array->size = size;
        return;
    }
    uint8_t* const fresh = title_function<uint8_t* XB_STDCALL(uint32_t)>(0x0045c1a0)(capacity);
    for (uint32_t i = 0;; ++i) {
        const uint32_t old_size = array->size;
        if (i >= (old_size >= size ? size : old_size)) break;
        std::memcpy(fresh + i * 0x34, reinterpret_cast<uint8_t*>(array->data) + i * 0x34, 0x34);
    }
    if (array->data) Scr_Free(array->data);
    array->data = reinterpret_cast<uint32_t*>(fresh);
    array->capacity = capacity;
    array->size = size;
}


// --- qcommon push 7 (high half), batch 2: list/map instances, string throws, Sys_ListFiles, game commands ---

namespace {

// _Xran/_Xlen: a std::string from the message, a logic_error-derived exception from it, _CxxThrowException.
[[noreturn]] void std_throw_string_error(uint32_t message, uint32_t vtable, uint32_t throw_info) {
    alignas(4) uint8_t text[0x1c] = {};
    alignas(4) uint8_t error[0x28] = {};
    std_string_CtorCStr(text, tstr(message));
    std_exception_CtorString(error, text);
    pk<uint32_t>(error, 0) = vtable;
    title_function<void XB_STDCALL(uint8_t*, uint32_t)>(0x001f2302)(error, throw_info);  // _CxxThrowException
    for (;;) {
    }
}

// buy_head, clear, insert_range, resize, construct_n, copy_from
constexpr HashMapInstance kHashMap6{0x0045ca60, 0x0045ca00, 0x001e67d0, 0x0045f250, 0, 0x001e6880};

}  // namespace

uint8_t* XB_STDCALL std_list_BuyNodeKeyArray_4(uint8_t* next, uint8_t* prev, const uint32_t* value) {
    return list_buy_node_key_array(next, prev, value, 0x0045c270);  // DynArray_Assign4 (PROLOGUE copy)
}
uint32_t XB_THISCALL std_list_IncSize_3(uint8_t* list, uint32_t count) { return list_inc_size(list, count, 0x0fffffff); }
void XB_THISCALL std_list_InsertRange_6(uint8_t* list, uint8_t* where, uint8_t* first, uint8_t* last, uint32_t) {
    list_insert_range(list, where, first, last, 0x0045caa0, 0x0045ead0);  // PROLOGUE BuyNode, IncSize
}
void XB_THISCALL HashMap_CopyFrom_6(HashMap* map, const HashMap* source) { hash_map_copy_from(map, source, kHashMap6); }
HashMap* XB_THISCALL HashMap_CopyCtor_6(HashMap* map, const HashMap* source) {
    return hash_map_copy_ctor(map, source, kHashMap6);
}

void XB_CDECL std_string_Xran() { std_throw_string_error(0x008278d0, 0x00832778, 0x00895b68); }  // out_of_range
void XB_CDECL std_string_Xlen() { std_throw_string_error(0x008278e8, 0x007f7784, 0x00895af4); }  // length_error

// --- Sys_ file listing (FindFirstFile/FindNextFile) ---

namespace {

using FindFirst = uint32_t XB_STDCALL(const char*, uint8_t*);
using FindNext = int32_t XB_STDCALL(uint32_t, uint8_t*);
using CloseHandleFn = int32_t XB_STDCALL(uint32_t);
constexpr uint32_t kFindFirstFile = 0x001e79ad;
constexpr uint32_t kFindNextFile = 0x001e7aba;
constexpr uint32_t kCloseHandle = 0x001e7b0b;

bool is_dot_or_cvs(const char* name) {
    return !Q_stricmpn(name, tstr(0x0082d700), 99999) ||  // "."
           !Q_stricmpn(name, tstr(0x008380a4), 99999) ||  // ".."
           !Q_stricmpn(name, tstr(0x0083cac4), 99999);    // "CVS"
}

// The shared tail: a zeroed Z_Malloc copy of the first `count` names plus a null terminator.
char** copy_file_list(char* const* list, int32_t count) {
    const uint32_t bytes = static_cast<uint32_t>(count) * 4 + 4;
    char** const copy = reinterpret_cast<char**>(Z_Malloc(bytes));
    Com_Memset(reinterpret_cast<uint8_t*>(copy), 0, bytes);
    int32_t i = 0;
    if (count > 0) {
        for (; i < count; ++i) copy[i] = list[i];
    }
    copy[i] = nullptr;
    return copy;
}

}  // namespace

void XB_REGS1 XB_CDECL Sys_ListFilteredFiles(const char* basedir, const char* subdirs, const char* filter,
                                            char** list, int32_t* numfiles) {
    if (*numfiles >= 0xfff) return;
    char search[0x80];
    if (std::strlen(subdirs)) {
        const uint32_t args[3] = {0x0083cad0, u32(basedir), u32(subdirs)};  // "%s\%s\*"
        Com_sprintf(search, 0x80, args);
    } else {
        const uint32_t args[2] = {0x0083cac8, u32(basedir)};  // "%s\*"
        Com_sprintf(search, 0x80, args);
    }
    uint8_t data[0x140] = {};  // WIN32_FIND_DATAA: attributes +0, cFileName +0x2c
    const uint32_t handle = title_function<FindFirst>(kFindFirstFile)(search, data);
    if (handle == 0xffffffff) return;
    const char* const name = reinterpret_cast<const char*>(data + 0x2c);
    do {
        if ((data[0] & 0x10) && is_dot_or_cvs(name)) continue;
        if (*numfiles >= 0xfff) break;
        char filename[0x80];
        if (subdirs) {
            const uint32_t args[3] = {0x0083cabc, u32(subdirs), u32(name)};  // "%s\%s"
            Com_sprintf(filename, 0x80, args);
        } else {
            const uint32_t args[2] = {0x0082ddd4, u32(name)};  // "%s"
            Com_sprintf(filename, 0x80, args);
        }
        if (!Com_FilterPath(filename, filter, 0)) continue;
        list[*numfiles] = CopyString(filename);
        ++*numfiles;
    } while (title_function<FindNext>(kFindNextFile)(handle, data));
    title_function<CloseHandleFn>(kCloseHandle)(handle);
}

char** XB_REGS3 XB_CDECL Sys_ListFiles(uint32_t, const char* filter, const char* extension, const char* directory,
                                       int32_t* numfiles, int32_t wantsubs) {
    char* list[0x1000];
    if (filter) {
        int32_t nfiles = 0;
        Sys_ListFilteredFiles(directory, tstr(0x0082dfeb), filter, list, &nfiles);
        list[nfiles] = nullptr;
        *numfiles = nfiles;
        if (!nfiles) return nullptr;
        return copy_file_list(list, nfiles);
    }
    uint32_t flag = 0x10;
    if (!extension) {
        extension = tstr(0x0082dfeb);
    } else if (extension[0] == '/' && !extension[1]) {
        extension = tstr(0x0082dfeb);
        flag = 0;
    }
    char search[0x80];
    const uint32_t args[3] = {0x0083cab4, u32(directory), u32(extension)};  // "%s\*%s"
    Com_sprintf(search, 0x80, args);
    uint8_t data[0x140] = {};
    int32_t nfiles = 0;
    const uint32_t handle = title_function<FindFirst>(kFindFirstFile)(search, data);
    if (handle == 0xffffffff) {
        *numfiles = 0;
        return nullptr;
    }
    const char* const name = reinterpret_cast<const char*>(data + 0x2c);
    do {
        const uint32_t subdir = data[0] & 0x10u;
        if (!wantsubs ? !(subdir ^ flag) : !subdir) continue;
        if (subdir && is_dot_or_cvs(name)) continue;
        list[nfiles] = CopyString(name);
        if (++nfiles == 0xfff) break;
    } while (title_function<FindNext>(kFindNextFile)(handle, data));
    list[nfiles] = nullptr;
    title_function<CloseHandleFn>(kCloseHandle)(handle);
    *numfiles = nfiles;
    if (!nfiles) return nullptr;
    return copy_file_list(list, nfiles);
}

// --- Boot: small-block pools ---

PLATFORM_ASM_ENTRY(Mem_InitSmallBlockPools_entry,
                   "pushl %ebx\n pushl %eax\n" PLATFORM_ASM_CALL(Mem_InitSmallBlockPools) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL Mem_InitSmallBlockPools(uint32_t effect_pool_size, uint32_t large) {
    const uint8_t large_pools = static_cast<uint8_t>(large);
    title_global<uint8_t>(0x0095f6dc) = large_pools;
    if (large_pools) effect_pool_size = 0x400000;
    FxSet_8bb860(effect_pool_size);
    FxSet_8bb864(500);
    FxSet_8bb866(250);
    FxSet_8bb868(200);
    FX_InitParticleLibrary();
    if (large_pools) {
        Mem_InitLargeBlockPools();
    } else {
        apsAddPool(400, 0x10);
        apsAddPool(400, 0x20);
        apsAddPool(400, 0x40);
        apsAddPool(400, 0x80);
    }
    uint8_t* const heap = Z_Malloc(0x494);
    if (!heap) {
        title_global<uint32_t>(0x0095f6d8) = 0;
        return heap;
    }
    uint8_t* const created = Heap_CreateFromZone(heap, 0x40000);
    title_global<uint32_t>(0x0095f6d8) = u32(created);
    return created;
}

// --- Game console commands (ent = the client's gentity) ---

namespace {

constexpr uint32_t kCmdArgc = 0x00934248;
constexpr uint32_t kCmdArgv = 0x0092df90;  // char* [], argv[0] first
constexpr uint32_t kStrncpy = 0x001eed80;
constexpr uint32_t kAtol = 0x001eeb81;
constexpr uint32_t kAtof = 0x001ef056;  // returns a double in ST0; stored as a float

void title_strncpy(char* dest, const char* source, uint32_t count) {
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(kStrncpy)(dest, source, count);
}
int32_t title_atol(const char* text) { return title_function<int32_t XB_CDECL(const char*)>(kAtol)(text); }
float title_atof(const char* text) { return title_function<float XB_CDECL(const char*)>(kAtof)(text); }

const char* cmd_argv(uint32_t index) { return title_global<const char*>(kCmdArgv + index * 4); }

void send_to_client(uint8_t* ent, uint32_t text) {
    SV_GameSendServerCommand(tstr(text), pk<uint32_t>(ent, 0x224));
}

void send_va_to_client(uint8_t* ent, uint32_t format) {
    const uint32_t handle = pk<uint32_t>(ent, 0x224);
    const uint32_t args[1] = {format};
    SV_GameSendServerCommand(va(args), handle);
}

// TeleportPlayer (ent in EAX, origin in ECX), SetClientViewAngle (angles in EAX, ent in EDX), SV_LinkEntity.
void teleport_with_angles(uint8_t* ent, const float* origin, const float* angles) {
    TitleRegs regs{};
    regs.eax = u32(ent);
    regs.ecx = u32(origin);
    qc_call_regs(0x0024eec0, &regs, 0, nullptr);
    regs = TitleRegs{};
    regs.eax = u32(angles);
    regs.edx = u32(ent);
    qc_call_regs(0x0024ebf0, &regs, 0, nullptr);
    SV_LinkEntity(ent);
}

}  // namespace

void XB_CDECL Cmd_MenuResponse_f(uint8_t* ent) {
    char id[0x80];
    char response[0x80];
    if (title_global<int32_t>(kCmdArgc) != 3) {
        id[0] = 0;
        std::memcpy(response, "bad", 4);
    } else {
        title_strncpy(id, cmd_argv(1), 0x7f);
        id[0x7f] = 0;
        const int32_t menu = title_atol(id);
        if (menu >= 0 && menu < 1) SV_GetConfigstring(menu + 0x272, 0x80, id);
        const char* const text = title_global<uint32_t>(kCmdArgc) > 2 ? cmd_argv(2) : tstr(0x0082dfeb);
        title_strncpy(response, text, 0x7f);
        response[0x7f] = 0;
    }
    ScriptString name{};
    ScriptString value{};
    using FromCString = ScriptString* XB_THISCALL(ScriptString*, const char*);
    ScriptString* const name_ref = title_function<FromCString>(0x0011eb50)(&name, id);
    ScriptString* const value_ref = title_function<FromCString>(0x0011eb50)(&value, response);
    TitleRegs regs{};
    regs.ebx = u32(ent);
    const uint32_t args[3] = {title_global<uint32_t>(0x0097a71c), u32(value_ref), u32(name_ref)};  // "menuresponse"
    qc_call_regs(0x00029000, &regs, 3, args);  // G_NotifyWithTwoStrings (ent in EBX)
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&value);  // ScriptString_Release
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&name);
}

void XB_CDECL Cmd_JumpToNode_f(uint8_t* ent) {
    if (!title_global<int32_t>(kCheatsEnabled)) {
        send_to_client(ent, 0x0083bd78);  // "print \"GAME_CHEATSNOTENABLED\""
        return;
    }
    if (title_global<int32_t>(kCmdArgc) != 2) {
        send_to_client(ent, 0x0083bc64);  // usage
        return;
    }
    char text[0x80];
    title_strncpy(text, cmd_argv(1), 0x7f);
    text[0x7f] = 0;
    const int32_t node_index = title_atol(text);
    uint8_t* const blocks = title_global<uint8_t*>(0x0095f4f4);  // g_pathNodeBlocks
    if (node_index < 0 || node_index >= pk<int32_t>(pk_ptr(blocks, 0x194), 4)) {
        const uint32_t handle = pk<uint32_t>(ent, 0x224);
        const uint32_t args[2] = {0x0083bc3c, pk<uint32_t>(pk_ptr(blocks, 0x194), 4)};  // node count message
        SV_GameSendServerCommand(va(args), handle);
        return;
    }
    const uint16_t handle = static_cast<uint16_t>(node_index);
    uint8_t* const node = PathNode_FromHandleInTable(&handle, blocks);
    float angles[3];
    angles[0] = 0.0f;
    std::memcpy(&angles[1], node + 0x50, 4);  // yaw
    angles[2] = 0.0f;
    teleport_with_angles(ent, reinterpret_cast<const float*>(node + 0x44), angles);
}

void XB_CDECL Cmd_SetViewpos_f(uint8_t* ent) {
    if (!title_global<int32_t>(kCheatsEnabled)) {
        send_to_client(ent, 0x0083bd78);
        return;
    }
    if (title_global<int32_t>(kCmdArgc) != 5) {
        send_to_client(ent, 0x0083bc90);  // usage
        return;
    }
    float angles[3];
    angles[2] = 0.0f;
    angles[0] = 0.0f;
    float origin[3];
    char text[0x80];
    for (uint32_t i = 1; i <= 3; ++i) {
        const char* const arg = i < title_global<uint32_t>(kCmdArgc) ? cmd_argv(i) : tstr(0x0082dfeb);
        title_strncpy(text, arg, 0x7f);
        text[0x7f] = 0;
        origin[i - 1] = title_atof(text);
    }
    const char* const yaw = title_global<uint32_t>(kCmdArgc) > 4 ? cmd_argv(4) : tstr(0x0082dfeb);
    title_strncpy(text, yaw, 0x7f);
    text[0x7f] = 0;
    angles[1] = title_atof(text);
    teleport_with_angles(ent, origin, angles);
}

void XB_CDECL Cmd_DropWeapon_f(uint8_t* ent) {
    if (!title_global<int32_t>(0x00955184)) return;
    if (!title_global<int32_t>(kCheatsEnabled)) return;
    TitleRegs regs{};
    regs.edi = u32(ent);
    const uint32_t args[2] = {ent[3], 0};
    if (qc_call_regs(0x00268350, &regs, 2, args)) G_AddEvent(ent, 0x97, 0);  // Drop_Weapon (ent in EDI)
}

void XB_CDECL Cmd_Kill_f(uint8_t* ent) {
    if (title_global<int32_t>(0x0095bf9c)) return;
    pk<uint32_t>(ent, 0x2a4) &= ~1u;
    uint8_t* const client = pk_ptr(ent, 0x23c);
    pk<int32_t>(ent, 0x350) = 0;
    pk<int32_t>(client, 0x100) = 0;
    using PlayerDie = void XB_CDECL(uint8_t*, uint8_t*, uint8_t*, int32_t, int32_t, uint32_t, uint32_t, uint32_t,
                                    uint32_t);
    title_function<PlayerDie>(0x00269450)(ent, ent, ent, 100000, 0x15, 0, 0, 0, 0);  // player_die
}

void XB_CDECL Cmd_Give_f(uint8_t* ent) {
    if (!title_global<int32_t>(kCheatsEnabled)) {
        send_va_to_client(ent, 0x0083bd78);
        return;
    }
    if (pk<int32_t>(ent, 0x350) <= 0) {
        send_va_to_client(ent, 0x0083bd58);  // "print \"GAME_MUSTBEALIVECOMMAND\""
        return;
    }
    using ConcatArgs = const char* XB_REGS3 XB_CDECL(uint32_t, uint32_t, int32_t);
    const int32_t amount = title_atol(title_function<ConcatArgs>(0x0024d250)(0, 0, 2));
    const char* const name = title_function<ConcatArgs>(0x0024d250)(0, 0, 1);
    if (!name || !std::strlen(name)) return;
    Q_stricmpn(name, tstr(0x0083bd34), 99999);  // "all" (the result is not used)
    int32_t health;
    if (amount) {
        health = pk<int32_t>(ent, 0x350) + amount;
    } else {
        pk<uint32_t>(pk_ptr(ent, 0x23c), 0x108) = title_global<uint32_t>(0x00950cec);
        health = title_global<int32_t>(0x00950cec);
    }
    const int32_t last_weapon = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    pk<int32_t>(ent, 0x350) = health;
    title_global<uint32_t>(0x00986018) = 1;
    for (int32_t weapon = 1; weapon <= last_weapon; ++weapon)
        BG_GivePlayerWeapon(static_cast<uint32_t>(weapon), pk_ptr(ent, 0x23c));
    title_global<uint32_t>(0x00986018) = 0;
    using AddAmmo = void XB_REGS1 XB_CDECL(uint8_t*, int32_t, int32_t, int32_t);
    const auto has_weapon = [ent](int32_t weapon) {
        return (pk<uint32_t>(pk_ptr(ent, 0x23c), 0x3f0 + (weapon >> 5) * 4) & (1u << (weapon & 0x1f))) != 0;
    };
    if (amount) {
        const int32_t current = pk<int32_t>(pk_ptr(ent, 0x23c), 0x80);
        if (current) title_function<AddAmmo>(0x0024e7a0)(ent, current, amount, 1);
    } else {
        for (int32_t weapon = 1; weapon <= last_weapon; ++weapon)
            if (has_weapon(weapon)) title_function<AddAmmo>(0x0024e7a0)(ent, weapon, 998, 1);
    }
    if (Q_stricmpn(name, tstr(0x0083bd24), 7) || !amount) return;  // "allammo"
    const int32_t last = title_global<int32_t>(0x0095f4d4);
    for (int32_t weapon = 1; weapon <= last; ++weapon)
        if (has_weapon(weapon)) title_function<AddAmmo>(0x0024e7a0)(ent, weapon, amount, 1);
}

// Cmd_Activate_f (ent in ESI): every title callee gets the original's registers (use/touch callbacks and the
// notify helpers read ESI/EDI/EBX).
PLATFORM_ASM_ENTRY(Cmd_Activate_f_entry,
                   "pushl %edi\n pushl %ebx\n pushl %esi\n" PLATFORM_ASM_CALL(Cmd_Activate_f) "addl $12, %esp\n"
                   "ret")

int32_t XB_CDECL Cmd_Activate_f(uint8_t* ent, uint32_t entry_ebx, uint32_t entry_edi) {
    TitleRegs regs{};
    regs.esi = u32(ent);
    regs.ebx = entry_ebx;
    regs.edi = entry_edi;
    const auto call_func = [&regs](uint32_t table, uint8_t* self, uint8_t* other, uint8_t* third) {
        const uint32_t index = pk<uint32_t>(self, table == 0x008da814 ? 0x338 : 0x33c);
        regs.eax = index;
        regs.ecx = index;
        const uint32_t args[3] = {u32(self), u32(other), u32(third)};
        qc_call_regs(title_global<uint32_t>(table + index * 4), &regs, 3, args);
    };
    constexpr uint32_t kUseFuncs = 0x008da840;    // entityUseFuncs
    constexpr uint32_t kTouchFuncs = 0x008da814;  // entityTouchFuncs
    const auto notify = [&regs, ent](uint8_t* target, uint32_t name) {
        regs.eax = u32(ent);
        regs.edi = u32(target);
        const uint32_t args[1] = {name};
        qc_call_regs(0x0024f570, &regs, 1, args);  // G_NotifyWithOwnHandle (ent in EAX, notified in EDI)
    };
    if (pk<uint32_t>(ent, 0x2a4) & 0x100000) return 0;
    pk<uint32_t>(pk_ptr(ent, 0x23c), 0x628) = 0;
    regs.ecx = u32(ent);
    uint8_t* const turret = reinterpret_cast<uint8_t*>(qc_call_regs(0x0024bb30, &regs, 0, nullptr));  // G_GetPlayerTurret
    if (turret) {
        call_func(kUseFuncs, turret, ent, ent);
        return 1;
    }
    if (ent[0x297]) {
        ent[0x297] = (pk<uint32_t>(pk_ptr(ent, 0x23c), 0xc0) & 0x6000) ? 2 : 0;
        return 1;
    }
    {
        const uint32_t args[1] = {u32(ent)};
        qc_call_regs(0x00267e10, &regs, 1, args);  // Player_UpdateCursorHints
    }
    uint8_t* const ps = pk_ptr(ent, 0x23c);
    regs.ebx = u32(ps);
    if (!pk<uint32_t>(ps, 0x4c0)) return 0;
    uint8_t* const target = G_EntHandleToEnt(reinterpret_cast<const uint32_t*>(ps + 0x4c0));
    regs.edi = u32(target);
    if (pk<uint32_t>(target, 0x240)) {
        notify(target, title_global<uint32_t>(0x0097a818));  // "trigger"
        return 1;
    }
    if (!pk<uint32_t>(target, 0x25c)) return 1;
    if (title_function<uint8_t XB_THISCALL(uint8_t*)>(0x00011200)(target + 0x25c)) return 1;  // ScriptString_IsEmpty
    const uint32_t classname = pk<uint32_t>(target, 0x260);
    if (classname == title_global<uint32_t>(0x0097a6dc) || classname == title_global<uint32_t>(0x0097a6e0)) {
        regs.eax = u32(target);
        const uint32_t args[1] = {u32(ent)};
        qc_call_regs(0x002710e0, &regs, 1, args);  // TryDoor (door in EAX)
        return 1;
    }
    if (classname == title_global<uint32_t>(0x0097a81c)) {  // "trigger_use"
        notify(target, title_global<uint32_t>(0x0097a818));
        call_func(kUseFuncs, target, ent, ent);
        return 1;
    }
    switch (target[0]) {
    case 0xe:
        regs.ecx = u32(ent);
        regs.edx = u32(target);
        if (!qc_call_regs(0x0024dc10, &regs, 0, nullptr)) return 0;
        call_func(kUseFuncs, target, ent, ent);
        return 1;
    case 0x2:
        regs.eax = u32(target);
        if (qc_call_regs(0x00248e10, &regs, 0, nullptr)) {
            pk<uint32_t>(ps, 0x628) = pk<uint32_t>(target, 0x224);
            pk<uint32_t>(pk_ptr(ent, 0x23c), 0x62c) = title_global<uint32_t>(0x0098540c);  // level.time
            return 0;
        }
        notify(target, title_global<uint32_t>(0x0097a814));  // "touch"
        if (!pk<uint32_t>(target, 0x338)) return 0;
        target[0x297] = 1;
        call_func(kTouchFuncs, target, ent, nullptr);
        return 1;
    case 0xa:
        if (!Turret_IsUsableBy(ent, 0, target)) return 0;
        call_func(kUseFuncs, target, ent, ent);
        return 1;
    default:
        break;
    }
    if (classname != title_global<uint32_t>(0x0097a784)) return 1;  // "script_brushmodel"
    notify(target, title_global<uint32_t>(0x0097a818));
    if (!pk<uint32_t>(target, 0x33c)) return 0;
    call_func(kUseFuncs, target, ent, ent);
    return 1;
}


// --- qcommon push 7 (high half), batch 3: clock, boot heaps, localized-string lookups ---

int32_t XB_CDECL Sys_Milliseconds() {
    // The RDTSC sites (0x002b1caf / 0x002b1ce9) are patched by the loader; C reads the same clock through XAPI
    // QueryPerformanceCounter (0x001ed6ca).
    const auto clock = [] {
        uint32_t counter[2] = {0, 0};
        title_function<uint32_t XB_STDCALL(uint32_t*)>(0x001ed6ca)(counter);
        return (uint64_t{counter[1]} << 32) | counter[0];
    };
    uint32_t& base_low = title_global<uint32_t>(0x0098b7f8);
    uint32_t& base_high = title_global<uint32_t>(0x0098b7fc);
    if (title_global<uint8_t>(0x008fef19)) {
        const uint64_t now = clock();
        base_low = static_cast<uint32_t>(now);
        base_high = static_cast<uint32_t>(now >> 32);
        title_global<uint8_t>(0x008fef19) = 0;
    }
    const uint64_t ticks = clock() - ((uint64_t{base_high} << 32) | base_low);
    // Unsigned 64-bit to x87: FILD of the low 63 bits plus 2^63 for the top bit, times 1 / 733333.3, _ftol.
    const uint32_t low_part[2] = {static_cast<uint32_t>(ticks), static_cast<uint32_t>(ticks >> 32) & 0x7fffffffu};
    const uint32_t sign_part[2] = {0, static_cast<uint32_t>(ticks >> 32) & 0x80000000u};
    long double ms;
    __asm__("fildll %1\n\tfildll %2\n\tfchs\n\tfaddp %%st, %%st(1)\n\tfmuls %3"
            : "=t"(ms)
            : "m"(low_part), "m"(sign_part), "m"(title_global<float>(0x0083ed64)));
    return static_cast<int32_t>(static_cast<int64_t>(ms));
}

void XB_CDECL Com_PrintfStub(const char* format, ...) {
    // Formats into a 32000-byte local (_vsprintf) and drops the text.
    static_assert(sizeof(char) == 1);
    char buffer[0x7d00];
    va_list args;
    va_start(args, format);
    uint32_t list = u32(args);
    va_end(args);
    title_function<int32_t XB_CDECL(char*, const char*, uint32_t)>(0x001eea95)(buffer, format, list);  // _vsprintf
}

void XB_CDECL FS_InitPackBuffers() {
    uint32_t config[5] = {0x400, 0x20, 0x400, 3, 0};
    uint8_t* const memory = Z_Malloc(File_Configure(config));
    title_global<uint32_t>(0x0095f63c) = u32(memory);
    File_InitSystem(memory);
    uint32_t info[3] = {0, 0, 0};
    File_GetDeviceInfoForBit(title_global<uint32_t>(0x0095f6f4), info);
    // The largest of the three device sizes (0x008d9d44).
    const uint32_t larger = info[0] > info[1] ? info[0] : info[1];
    title_global<uint32_t>(0x008d9d44) = larger <= info[2] ? info[2] : larger;
}

void XB_CDECL Com_InitSmallHeap() {
    // {count, 16 x {size, align, count}}: five pools of 0x14 / 0x20 / 0x40 / 0x80 / 0x100 bytes.
    uint32_t config[1 + 16 * 3];
    config[0] = 0;
    for (int32_t i = 0; i < 16; ++i) {
        config[1 + i * 3] = 0;
        config[2 + i * 3] = 4;
        config[3 + i * 3] = 0;
    }
    static const uint32_t kPools[5][3] = {
        {0x14, 4, 0x1000}, {0x20, 0x10, 0x800}, {0x40, 0x10, 0x800}, {0x80, 0x10, 0x400}, {0x100, 0x10, 0x1c2}};
    for (const auto& pool : kPools) {
        const uint32_t n = config[0];
        if (static_cast<int32_t>(n) >= 16) continue;
        config[1 + n * 3] = pool[0];
        config[2 + n * 3] = pool[1];
        config[3 + n * 3] = pool[2];
        config[0] = n + 1;
    }
    uint8_t* heap = Z_Malloc(0x3c);
    if (heap) heap = SmallHeap_Init(heap, config, 0, 0);
    static const uint32_t kHeapPointers[] = {0x00947700, 0x0095f570, 0x0095f568, 0x0095f574, 0x0095f56c,
                                             0x0095f5cc, 0x0095f494, 0x0095f488, 0x0095f48c, 0x0095f6d4,
                                             0x0095f598, 0x0095f5bc, 0x0095f5c8, 0x0095f520, 0x0095f498};
    for (uint32_t address : kHeapPointers) title_global<uint32_t>(address) = u32(heap);
    Stream_InitSmallHeaps();
    title_function<void XB_CDECL()>(0x002f67a0)();  // ScrThread_InitDataHeap
}

// --- Localized strings: map 0x0095fee8 (head 0x0095feec, size 0x0095fef0) of name -> {text +0x28, alternate
// text +0x44 (used while 0x0095fef4), value +0x60} ---

namespace {

// A temporary std::string of `text` (the original's local), handed to a title tree search that takes the
// tree in EDI, the key in ESI and the result slot in EBX; the string's heap buffer freed after.
uint8_t* loc_tree_find(uint32_t find, uint8_t* tree, const char* text, uint8_t** out) {
    alignas(4) uint8_t key[0x1c] = {};
    pk<uint32_t>(key, 0x18) = 0xf;
    pk<uint32_t>(key, 0x14) = 0;
    key[4] = 0;
    std_string_AssignN(key, text, static_cast<uint32_t>(std::strlen(text)));
    TitleRegs regs{};
    regs.edi = u32(tree);
    regs.esi = u32(key);
    regs.ebx = u32(out);
    qc_call_regs(find, &regs, 0, nullptr);
    if (pk<uint32_t>(key, 0x18) >= 0x10) Z_Free(pk_ptr(key, 4));
    return *out;
}

constexpr uint32_t kLocMap = 0x0095fee8;
constexpr uint32_t kMapStringFind = 0x0001f2e0;  // std_map_string_Find
constexpr uint32_t kSetStringFind = 0x0001f500;  // std_set_string_Find

const char* std_string_c_str(uint8_t* text) {
    return pk<uint32_t>(text, 0x18) >= 0x10 ? reinterpret_cast<const char*>(pk_ptr(text, 4))
                                            : reinterpret_cast<const char*>(text + 4);
}

}  // namespace

uint32_t XB_REGS2 XB_CDECL Loc_FindEntryValue(uint32_t, const char* name) {
    uint8_t* node = nullptr;
    loc_tree_find(kMapStringFind, &title_global<uint8_t>(kLocMap), name, &node);
    return node == title_global<uint8_t*>(kLocMap + 4) ? 0 : pk<uint32_t>(node, 0x60);
}

const char* XB_REGS2 XB_CDECL Loc_FindStringEntry(uint32_t, const char* name, int32_t alternate) {
    uint8_t* node = nullptr;
    loc_tree_find(kMapStringFind, &title_global<uint8_t>(kLocMap), name, &node);
    if (node == title_global<uint8_t*>(kLocMap + 4)) return nullptr;
    if (alternate && title_global<uint32_t>(0x0095fef4)) return std_string_c_str(node + 0x44);
    return std_string_c_str(node + 0x28);
}

uint32_t XB_REGS2 XB_STDCALL Loc_FindFlagBit(uint32_t, const char* name, uint8_t* state) {
    uint8_t* node = nullptr;  // the original uses its argument slot
    loc_tree_find(kSetStringFind, state + 0x98, name, &node);
    return node == pk_ptr(state, 0x9c) ? 0 : pk<uint32_t>(node, 0x28);
}

void XB_CDECL Loc_ClearStrings() {
    std_map_string_EraseSubtree(&title_global<uint8_t>(kLocMap), pk_ptr(title_global<uint8_t*>(kLocMap + 4), 4));
    pk<uint32_t>(title_global<uint8_t*>(kLocMap + 4), 4) = title_global<uint32_t>(kLocMap + 4);
    uint8_t* const head = title_global<uint8_t*>(kLocMap + 4);
    title_global<uint32_t>(0x0095fef0) = 0;
    pk<uint32_t>(head, 0) = u32(head);
    pk<uint32_t>(title_global<uint8_t*>(kLocMap + 4), 8) = title_global<uint32_t>(kLocMap + 4);
    title_global<uint32_t>(0x0095fe70) = 0;
    std_string_AssignN(&title_global<uint8_t>(0x0095fe74), tstr(0x0082dfeb), 0);
    std_string_AssignN(&title_global<uint8_t>(0x0095fe90), tstr(0x0082dfeb), 0);
}

PLATFORM_ASM_ENTRY(Loc_PopSemicolonToken_entry,
                   "pushl %ecx\n pushl %esi\n" PLATFORM_ASM_CALL(Loc_PopSemicolonToken) "addl $4, %esp\n"
                   "popl %ecx\n ret")

char* XB_CDECL Loc_PopSemicolonToken(uint8_t* text) {
    if (!std::strlen(std_string_c_str(text))) return nullptr;
    char* const token = &title_global<char>(0x0098bb78);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(token, std_string_c_str(text), 0x7f);
    title_global<char>(0x0098bbf7) = 0;
    char* const semicolon = title_function<char* XB_CDECL(const char*, int32_t)>(0x001ef5e0)(token, ';');  // strchr
    if (semicolon) {
        *semicolon = 0;
        std_string_Erase(text, 0, static_cast<uint32_t>(semicolon - token) + 1);
    } else {
        std_string_Erase(text, 0, 0xffffffff);
    }
    return token;
}

const char* XB_REGS1 XB_CDECL Loc_ReplaceNewlineEscapes(const char* text) {
    uint8_t* const buffer = &title_global<uint8_t>(0x0098bcdc);  // function-static std::string
    if (!(title_global<uint32_t>(0x0098bcf8) & 1)) {
        title_global<uint32_t>(0x0098bcf8) |= 1;
        title_global<uint32_t>(0x0098bcf4) = 0xf;
        title_global<uint32_t>(0x0098bcf0) = 0;
        title_global<uint8_t>(0x0098bce0) = 0;
        title_function<int32_t XB_CDECL(uint32_t)>(0x001eed36)(0x00379960);  // _atexit
    }
    std_string_AssignN(buffer, text, static_cast<uint32_t>(std::strlen(text)));
    for (uint32_t pos = std_string_Find(0, buffer, tstr(0x0082d278), 2); pos != 0xffffffff;  // "\n" escape
         pos = std_string_Find(0, buffer, tstr(0x0082d278), 2)) {
        const_cast<char*>(std_string_c_str(buffer))[pos] = '\n';
        std_string_Erase(buffer, pos + 1, 1);
    }
    return std_string_c_str(buffer);
}

// Loc_BeginStringFile (path in EAX, state in EDI).
PLATFORM_ASM_ENTRY(Loc_BeginStringFile_entry,
                   "pushl 4(%esp)\n pushl %edi\n pushl %eax\n" PLATFORM_ASM_CALL(Loc_BeginStringFile)
                   "addl $12, %esp\n ret $4")

void XB_CDECL Loc_BeginStringFile(const char* path, uint8_t* state, uint32_t value) {
    char name[0x80];
    const char* const base = COM_SkipPath(0, Loc_StripFileExtension(path));
    std::strcpy(name, base);
    title_function<char* XB_CDECL(char*)>(0x001fa3d5)(name);  // _strupr
    std_string_AssignN(state + 0x3c, name, static_cast<uint32_t>(std::strlen(name)));
    const char* const dir = COM_SkipPath(0, Com_DirName(path));
    std_string_AssignN(state + 0x58, dir, static_cast<uint32_t>(std::strlen(dir)));
    const int32_t differs = title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(  // _stricmp
        std_string_c_str(state + 0x58), tstr(0x00833518));
    pk<uint32_t>(state, 0x74) = differs ? 0 : 1;
    pk<uint32_t>(state, 0x84) = value;
}

void XB_CDECL Loc_LoadStringDir_r(const char* extension, const char* dir, uint8_t* out) {
    const auto free_list = [](char** list) {
        if (!list) return;
        for (char** name = list; *name; ++name) Z_Free(reinterpret_cast<uint8_t*>(*name));
        Z_Free(reinterpret_cast<uint8_t*>(list));
    };
    const auto title_sprintf = title_function<int32_t XB_CDECL(char*, const char*, const char*, const char*)>(0x001ee901);
    char path[0x80];
    int32_t ndirs = 0;
    char** const dirs = FS_ListFilteredFiles(dir, tstr(0x0082d70c), nullptr, &ndirs);  // "/"
    for (int32_t i = 0; i < ndirs; ++i) {
        const char* const name = dirs[i];
        if (!name[0] || name[0] == '.') continue;
        title_sprintf(path, tstr(0x00833208), dir, name);  // "%s/%s"
        Loc_LoadStringDir_r(extension, path, out);
    }
    int32_t nfiles = 0;
    char** const files = FS_ListFilteredFiles(dir, extension, nullptr, &nfiles);
    for (int32_t i = 0; i < nfiles; ++i) {
        title_sprintf(path, tstr(0x00833208), dir, files[i]);
        std_string_AppendN(static_cast<uint32_t>(std::strlen(path)), out, path);
        std_string_AppendNChar(out, 1, ';');
        ++title_global<int32_t>(0x00929cd8);
    }
    free_list(files);
    free_list(dirs);
}

// ---- Localized string file loaders (push 8). The whole chain hangs off Loc_LoadStrings and
// Loc_ParseStringBuffer, which have no callers, no stored pointer and no immediate in the XBE: dead code (the
// title reads its localized strings from the zones instead). Kept as C, untested.

namespace {

constexpr uint32_t kLocFileState = 0x0095fe70;  // the string file parser's state (Loc_ResetStringFileState)

// A std::string local in its empty state (capacity 15, size 0).
struct LocTempString {
    alignas(4) uint8_t s[0x1c] = {};
    LocTempString() {
        pk<uint32_t>(s, 0x18) = 0xf;
        pk<uint32_t>(s, 0x14) = 0;
        s[4] = 0;
    }
    void assign(const char* text) { std_string_AssignN(s, text, static_cast<uint32_t>(std::strlen(text))); }
    void tidy() {
        if (pk<uint32_t>(s, 0x18) >= 0x10) Z_Free(pk_ptr(s, 4));
    }
};

// va("%s_%s", the file's base name (state +0x3c), name).
char* loc_scoped_name(uint8_t* state, const char* name) {
    const uint32_t args[3] = {0x0082d7d0, u32(std_string_c_str(state + 0x3c)), u32(name)};
    return va(args);
}

int32_t crt_stricmp(const char* a, const char* b) {
    return title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(a, b);
}

}  // namespace

void XB_THISCALL Loc_AddStringEntry(const char* key, uint8_t* state, const char* text, int32_t own_language) {
    uint8_t* node = nullptr;  // the original uses the state argument's slot
    loc_tree_find(kMapStringFind, state + 0x78, loc_scoped_name(state, key), &node);
    if (node == pk_ptr(state, 0x7c)) return;
    uint8_t* const value = node + 0x28;
    if (!own_language && !pk<uint32_t>(state, 0x74)) {
        if (crt_stricmp(text, tstr(0x00833278))) {  // "#same"
            std_string_AssignCStr(0, text, value);
            return;
        }
        // "#same": the text of the previous (English) entry, marked "[#same]" in the alternate text.
        std_string_AssignSubstr(value, state + 0x20, 0, 0xffffffff);
        if (!pk<uint32_t>(state, 0x84)) return;
        std_string_AssignCStr(0, tstr(0x00833284), value + 0x1c);  // "["
        std_string_AppendCStr(0, 0, tstr(0x00833278), value + 0x1c);
        std_string_AppendCStr(0, 0, tstr(0x00833280), value + 0x1c);  // "]"
        return;
    }
    std_string_AssignN(value, text, static_cast<uint32_t>(std::strlen(text)));
    if (pk<uint32_t>(state, 0x84)) {
        std_string_AssignN(value + 0x1c, tstr(0x00833284), 1);  // "[" text "]"
        std_string_AppendN(static_cast<uint32_t>(std::strlen(text)), value + 0x1c, text);
        std_string_AppendN(1, value + 0x1c, tstr(0x00833280));
    }
    std_string_AssignN(state + 0x20, text, static_cast<uint32_t>(std::strlen(text)));
}

void XB_STDCALL Loc_BeginReference(uint8_t* state, const char* name) {
    uint8_t* node = nullptr;  // the original uses the state argument's slot
    loc_tree_find(kMapStringFind, state + 0x78, loc_scoped_name(state, name), &node);
    if (node == pk_ptr(state, 0x7c)) {
        // A new entry: {string, string, int 0} copied in through operator[].
        LocTempString entry_text;
        LocTempString entry_alt;
        alignas(4) uint8_t entry[0x3c] = {};
        std::memcpy(entry, entry_text.s, 0x1c);
        std::memcpy(entry + 0x1c, entry_alt.s, 0x1c);
        pk<uint32_t>(entry, 0x38) = 0;
        LocTempString key;
        key.assign(loc_scoped_name(state, name));
        uint8_t* const value = std_map_string_Subscript(key.s, state + 0x78);
        std_string_AssignSubstr(value, entry, 0, 0xffffffff);
        std_string_AssignSubstr(value + 0x1c, entry + 0x1c, 0, 0xffffffff);
        pk<uint32_t>(value, 0x38) = pk<uint32_t>(entry, 0x38);
        key.tidy();
        LocEntry_Dtor(entry);
    }
    std_string_AssignN(state + 4, name, static_cast<uint32_t>(std::strlen(name)));
}

uint8_t* XB_REGS1 XB_STDCALL Loc_RegisterFlag(const char* flag, uint8_t* state, const char* name) {
    uint32_t bit = Loc_FindFlagBit(0, flag, state);
    if (!bit) {
        LocTempString text;
        text.assign(flag);
        TitleRegs regs{};
        regs.eax = u32(state + 0x88);
        regs.ecx = u32(text.s);
        qc_call_regs(0x0001f370, &regs, 0, nullptr);  // std_vector_string_PushBack (vector EAX, value ECX)
        text.tidy();
        int32_t count = 0;
        if (pk_ptr(state, 0x8c))
            count = static_cast<int32_t>(pk<uint32_t>(state, 0x90) - pk<uint32_t>(state, 0x8c)) / 0x1c;
        bit = 1u << ((count - 1) & 31);
        LocTempString key;
        key.assign(flag);
        pk<uint32_t>(std_set_string_Subscript(key.s, state + 0x98), 0) = bit;
        key.tidy();
    }
    uint8_t* node = nullptr;  // the original uses the name argument's slot
    loc_tree_find(kMapStringFind, state + 0x78, loc_scoped_name(state, name), &node);
    if (node != pk_ptr(state, 0x7c)) pk<uint32_t>(node, 0x60) |= bit;
    return node;
}

// Loc_ParseStringFile (state in EBX; line on the stack, callee pops it).
PLATFORM_ASM_ENTRY(Loc_ParseStringFile_entry,
                   "pushl 4(%esp)\n pushl %ebx\n" PLATFORM_ASM_CALL(Loc_ParseStringFile) "addl $8, %esp\n"
                   "ret $4")

const char* XB_CDECL Loc_ParseStringFile(uint8_t* state, char* line) {
    if (!line) return nullptr;
    char* cursor = line;  // the original advances its argument slot
    if (Loc_MatchKeyword(tstr(0x008333a0), &cursor)) {  // "VERSION"
        const int32_t version = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(  // _atol
            Loc_TrimQuotedValue(cursor));
        if (version == 1) return nullptr;
        const uint32_t args[3] = {0x00833370, static_cast<uint32_t>(version), 1};  // "Unexpected version number"
        return va(args);
    }
    if (Loc_MatchKeyword(tstr(0x00833368), &cursor)) return nullptr;  // "CONFIG"
    if (Loc_MatchKeyword(tstr(0x0083335c), &cursor)) return nullptr;  // "FILENOTES"
    if (Loc_MatchKeyword(tstr(0x00833354), &cursor)) return nullptr;  // "NOTES"
    if (Loc_MatchKeyword(tstr(0x00833348), &cursor)) {               // "REFERENCE"
        Loc_BeginReference(state, Loc_TrimQuotedValue(cursor));
        return nullptr;
    }
    const auto strtok = title_function<char* XB_CDECL(char*, const char*)>(0x001eeea4);
    if (Loc_MatchKeyword(tstr(0x00833340), &cursor)) {  // "FLAGS"
        const char* const reference = std_string_c_str(state + 4);
        if (!*reference) return tstr(0x00833314);  // "Error parsing file: Unexpected "FLAGS""
        char buf[0x400];
        std::memset(buf, 0, sizeof(buf));
        crt_strncpy(buf, cursor, 0x3ff);
        for (char* flag = strtok(buf, tstr(0x0083333c)); flag; flag = strtok(nullptr, tstr(0x0083333c))) {  // " \t"
            title_function<char* XB_CDECL(char*)>(0x001fa3d5)(flag);  // _strupr
            Loc_RegisterFlag(flag, state, reference);
        }
        return nullptr;
    }
    if (Loc_MatchKeyword(tstr(0x00833308), &cursor)) {  // "ENDMARKER"
        pk<uint32_t>(state, 0) = 1;
        return nullptr;
    }
    const char* text = cursor;
    if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001f1486)(tstr(0x00833300), text, 5)) {
        const uint32_t args[2] = {0x00833288, u32(text)};  // "Unknown keyword at linestart"
        return va(args);
    }
    const char* const reference = std_string_c_str(state + 4);
    if (!*reference) return tstr(0x008332ac);  // "Error parsing file: Unexpected "LANG_""
    text += 5;
    const char* end = text;
    while (*end && *end != ' ' && *end != '\t') ++end;
    char language[0x400];
    std::memset(language, 0, sizeof(language));
    uint32_t length = static_cast<uint32_t>(end - text);
    if (length > 0x3ff) length = 0x3ff;
    crt_strncpy(language, text, length);
    text += std::strlen(language);
    const char* const value = Loc_ReplaceNewlineEscapes(Loc_TrimQuotedValue(text));
    if (pk<uint32_t>(state, 0x74)) {
        Loc_AddStringEntry(reference, state, value, 0);
        return nullptr;
    }
    const int32_t english = crt_stricmp(language, tstr(0x00833518)) == 0 ? 1 : 0;  // "english"
    if (!english && crt_stricmp(std_string_c_str(state + 0x58), language)) {
        const uint32_t args[3] = {0x008332d4, u32(language), u32(std_string_c_str(state + 0x58))};
        const char* const error = va(args);  // "Language "%s" found when expecting "%s"!"
        if (error) return error;
    }
    Loc_AddStringEntry(reference, state, value, english);
    return nullptr;
}

// The parse loop over `cursor` with the state at 0x0095fe70: the first error, else null.
namespace {
const char* loc_parse_lines(const char** cursor) {
    uint8_t* const state = &title_global<uint8_t>(kLocFileState);
    char line[0x4000];
    const char* error = nullptr;
    while (!error) {
        if (!Loc_ReadLine(cursor, line)) return nullptr;
        if (std::strlen(line)) error = Loc_ParseStringFile(state, line);
    }
    return error;
}
}  // namespace

const char* XB_REGS3 XB_CDECL Loc_LoadStringFile(uint32_t, uint32_t, const char* path, uint32_t value, int32_t quiet) {
    void* buffer = nullptr;
    if (FS_ReadFile(path, &buffer) <= 0 || !buffer) {
        if (quiet) return nullptr;
        const uint32_t args[2] = {0x0083322c, u32(path)};  // "Unable to load "%s"!"
        return va(args);
    }
    Loc_BeginStringFile(path, &title_global<uint8_t>(kLocFileState), value);
    const char* cursor = static_cast<const char*>(buffer);
    const char* const error = loc_parse_lines(&cursor);
    --title_global<int32_t>(0x00940a0c);  // fs_loadStack (FS_FreeFile inlined)
    Z_Free(static_cast<uint8_t*>(buffer));
    if (error) return error;
    if (title_global<uint32_t>(kLocFileState)) return nullptr;
    const uint32_t args[2] = {0x00833244, 0x00833308};  // "Truncated file, failed to find "ENDMARKER" at file end!"
    return va(args);
}

const char* XB_CDECL Loc_ParseStringBuffer(const char* path, const char* text) {
    Loc_BeginStringFile(path, &title_global<uint8_t>(kLocFileState), 0);
    const char* cursor = text;
    const char* const error = loc_parse_lines(&cursor);
    if (error) return error;
    // Without an error: the end marker flag itself (EAX keeps it), or the truncation message.
    const uint32_t ended = title_global<uint32_t>(kLocFileState);
    if (ended) return reinterpret_cast<const char*>(static_cast<uintptr_t>(ended));
    const uint32_t args[2] = {0x00833244, 0x00833308};
    return va(args);
}

// Loc_LoadStringFileOrSte (path in EAX, value in EBX).
PLATFORM_ASM_ENTRY(Loc_LoadStringFileOrSte_entry,
                   "pushl %ebx\n pushl %eax\n" PLATFORM_ASM_CALL(Loc_LoadStringFileOrSte) "addl $8, %esp\n ret")

const char* XB_CDECL Loc_LoadStringFileOrSte(const char* path, uint32_t value) {
    const char* const error = Loc_LoadStringFile(0, 0, path, value, 0);
    if (error) return error;
    // Loaded: the matching ".ste" file as well, when the extension has three letters (quietly).
    char name[0x80];
    crt_strncpy(name, path, 0x7f);
    name[0x7f] = 0;
    char* const dot = title_function<char* XB_CDECL(const char*, int32_t)>(0x001efd40)(name, '.');  // strrchr
    if (!dot || std::strlen(dot) != 4) return nullptr;
    std::memcpy(dot, tstr(0x00833224), 5);  // ".ste"
    return Loc_LoadStringFile(0, 0, name, value, 1);
}

const char* XB_CDECL Loc_LoadStrings(const char* language, uint32_t value) {
    const char* result = nullptr;
    if (!language || !*language) return nullptr;
    Loc_ClearStrings();
    LocTempString list;
    title_global<int32_t>(0x00929cd8) = 0;
    std_string_AssignN(list.s, tstr(0x0082dfeb), 0);
    Loc_LoadStringDir_r(tstr(0x00833200), tstr(0x00833210), list.s);  // ".str", "localizedstrings"
    const auto strrchr = title_function<char* XB_CDECL(const char*, int32_t)>(0x001efd40);
    char* const dir = &title_global<char>(0x0098b2a0);
    char* const base = &title_global<char>(0x0098b1a0);
    for (char* path = Loc_PopSemicolonToken(list.s); path && !result; path = Loc_PopSemicolonToken(list.s)) {
        // The file's folder name (Com_DirName and COM_SkipPath inlined).
        std::strcpy(dir, path);
        char* const back = strrchr(dir, '\\');
        char* const slash = strrchr(dir, '/');
        char* const cut = u32(back) > u32(slash) ? back : slash;
        if (cut) *cut = 0;
        const char* start = dir;
        for (const char* c = dir; *c; ++c)
            if (*c == '/' || *c == '\\') start = c + 1;
        std::strcpy(base, start);
        if (!crt_stricmp(language, base)) result = Loc_LoadStringFileOrSte(path, value);
    }
    list.tidy();
    return result;
}

// ---- Push 8, batch 1: ref-counted strings, hunk usage, info fields, "take".

namespace {

// RcString: {length, references beyond the first, text (Heap_Alloc16), capacity}, 0x10 bytes from Z_Malloc.
uint8_t* rc_string_new() {
    uint8_t* const rc = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c10)(0x10);  // Z_Malloc
    if (!rc) return nullptr;
    pk<uint32_t>(rc, 0) = 0;
    pk<uint32_t>(rc, 4) = 0;
    pk<uint32_t>(rc, 8) = 0;
    pk<uint32_t>(rc, 0xc) = 0;
    return rc;
}

}  // namespace

PLATFORM_ASM_ENTRY(RcString_MakeUnique_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(RcString_MakeUnique) "addl $4, %esp\n ret")

void XB_CDECL RcString_MakeUnique(uint8_t** holder) {
    uint8_t* const old = *holder;
    if (!pk<uint32_t>(old, 4)) return;
    const uint32_t length = old ? pk<uint32_t>(old, 0) : 0;
    *holder = rc_string_new();
    RcString_Reserve(holder, length + 1, 0);
    crt_strncpy(reinterpret_cast<char*>(pk_ptr(*holder, 8)), reinterpret_cast<const char*>(pk_ptr(old, 8)), length + 1);
    pk<uint32_t>(*holder, 0) = length;
    const int32_t refs = pk<int32_t>(old, 4) - 1;
    pk<int32_t>(old, 4) = refs;
    if (refs >= 0) return;
    if (pk_ptr(old, 8)) Z_Free(pk_ptr(old, 8));
    Z_Free(old);
}

void XB_THISCALL RcString_Reserve(uint8_t** holder, int32_t size, uint8_t keep) {
    if (!*holder) *holder = rc_string_new();
    RcString_MakeUnique(holder);
    uint8_t* const rc = *holder;
    const bool had_buffer = pk<int32_t>(rc, 0xc) != 0;
    if (size < pk<int32_t>(rc, 0xc)) return;
    if (size == 1) {
        pk<int32_t>(rc, 0xc) = size;
    } else {
        const int32_t rem = size % 20;  // whole multiples of 20 bytes
        pk<int32_t>(rc, 0xc) = rem ? size - rem + 20 : size;
    }
    char* const text = reinterpret_cast<char*>(
        title_function<uint8_t* XB_CDECL(uint32_t)>(0x00042c30)(pk<uint32_t>(*holder, 0xc)));  // Heap_Alloc16
    if (had_buffer && keep) std::strcpy(text, reinterpret_cast<const char*>(pk_ptr(*holder, 8)));
    if (pk_ptr(*holder, 8)) Z_Free(pk_ptr(*holder, 8));
    pk<uint32_t>(*holder, 8) = u32(text);
}

void XB_CDECL Com_ReadHunkUsage(const char* map_name) {
    int32_t f = 0;
    const int32_t length = FS_FOpenFileByMode(tstr(0x00832ca8), &f, 0);  // "hunkusage.dat"
    if (length >= 0) {
        uint8_t* const buffer = title_function<uint8_t* XB_CDECL(int32_t)>(0x00042c10)(length + 1);  // Z_Malloc
        Com_Memset(buffer, 0, static_cast<uint32_t>(length + 1));
        std::memset(buffer, 0, static_cast<uint32_t>(length + 1));
        FS_Read(buffer, length, f);
        FS_FCloseFile(f);
        uint8_t* const info = title_global<uint8_t*>(0x008d4ca0);  // the parse info (an ungot token restored)
        const char* cursor = reinterpret_cast<const char*>(buffer);
        const auto next_token = [&]() {
            if (pk<uint32_t>(info, 0x84)) {
                pk<uint32_t>(info, 0x84) = 0;
                cursor = reinterpret_cast<const char*>(pk_ptr(info, 0x98));
                pk<uint32_t>(info, 0x80) = pk<uint32_t>(info, 0x94);
            }
            return COM_ParseExt(&cursor, 1);
        };
        for (;;) {
            const char* const name = next_token();
            if (!name || !*name) break;
            if (Q_stricmpn(name, map_name, 99999)) continue;  // inlined
            const char* const value = next_token();
            if (!value || !*value) continue;
            Cvar_Set2(tstr(0x00832c90), const_cast<char*>(value), 1);  // "com_expectedhunkusage"
            Z_Free(buffer);
            return;
        }
        Z_Free(buffer);
    }
    Cvar_Set2(tstr(0x00832c90), const_cast<char*>(tstr(0x00832c8c)), 1);  // "-1"
}

uint32_t XB_REGS2 XB_CDECL Info_ParseFieldsToStruct(uint32_t, const uint8_t* fields, uint8_t* out, int32_t count,
                                                    uint8_t* map, int32_t custom_types, uint32_t parse_custom,
                                                    uint32_t parse_callback) {
    int32_t i = 0;
    for (; i < count; ++i) {
        const uint8_t* const field = fields + i * 0xc;  // {name, offset, type}
        uint32_t key = pk<uint32_t>(const_cast<uint8_t*>(field), 0);
        TitleRegs regs{};
        regs.ebx = u32(map + 8);
        const uint32_t key_arg = u32(&key);
        const auto found = reinterpret_cast<const char**>(
            static_cast<uintptr_t>(qc_call_regs(0x000249a0, &regs, 1, &key_arg)));  // StringMap_Find
        if (!found || !*found || !**found) continue;
        const char* const value = *found;
        const int32_t type = pk<int32_t>(const_cast<uint8_t*>(field), 8);
        uint8_t* const dest = out + pk<uint32_t>(const_cast<uint8_t*>(field), 4);
        if (type >= 8) {
            if (custom_types > 0 && type < custom_types) {
                if (!title_function<int32_t XB_CDECL(uint8_t*, const char*, int32_t)>(parse_custom)(out, value, type))
                    return 0;
            } else {
                title_function<void XB_CDECL(int32_t, const char*, int32_t)>(0x002938f0)(1, tstr(0x00833688), type);
            }
            continue;
        }
        switch (type) {
        case 0:
            title_function<void XB_CDECL(uint8_t*, const char*)>(parse_callback)(dest, value);
            break;
        case 1:  // char[256]
            crt_strncpy(reinterpret_cast<char*>(dest), value, 0xff);
            dest[0xff] = 0;
            break;
        case 2:
        case 3:  // char[128]
            crt_strncpy(reinterpret_cast<char*>(dest), value, 0x7f);
            dest[0x7f] = 0;
            break;
        case 4:
            pk<int32_t>(dest, 0) = title_atol(value);
            break;
        case 5:
            pk<int32_t>(dest, 0) = title_atol(value) ? 1 : 0;
            break;
        case 6:  // atof's double stored as float
            pk<float>(dest, 0) = title_function<float XB_CDECL(const char*)>(0x001ef056)(value);
            break;
        case 7: {  // seconds -> milliseconds: FMUL by 1000.0f on the x87, _ftol
            const long double seconds = title_function<long double XB_CDECL(const char*)>(0x001ef056)(value);
            pk<int32_t>(dest, 0) = static_cast<int32_t>(static_cast<int64_t>(seconds * title_global<float>(0x0083eb9c)));
            break;
        }
        }
    }
    return i == count ? 1 : 0;
}

void XB_CDECL Cmd_Take_f(uint8_t* ent) {
    if (!title_global<int32_t>(kCheatsEnabled)) {
        send_va_to_client(ent, 0x0083bd78);  // "print \"GAME_CHEATSNOTENABLED\""
        return;
    }
    if (pk<int32_t>(ent, 0x350) <= 0) {
        send_va_to_client(ent, 0x0083bd58);  // "print \"GAME_MUSTBEALIVECOMMAND\""
        return;
    }
    using ConcatArgs = const char* XB_REGS3 XB_CDECL(uint32_t, uint32_t, int32_t);
    const int32_t amount = title_atol(title_function<ConcatArgs>(0x0024d250)(0, 0, 2));
    const char* const name = title_function<ConcatArgs>(0x0024d250)(0, 0, 1);
    if (!name || !std::strlen(name)) return;
    // ps +0x80 current weapon, +0x110 ammo per ammo index, +0x280 clip per clip index; weapon def +0x598 ammo
    // index, +0x5a0 clip index.
    const auto ps = [ent]() { return pk_ptr(ent, 0x23c); };
    const auto def = [](int32_t weapon) { return title_global<uint8_t**>(0x0095f4d8)[weapon]; };  // bg_weaponDefs
    const auto ammo = [&](int32_t weapon) -> int32_t& { return pk<int32_t>(ps(), 0x110 + pk<int32_t>(def(weapon), 0x598) * 4); };
    const auto clip = [&](int32_t weapon) -> int32_t& { return pk<int32_t>(ps(), 0x280 + pk<int32_t>(def(weapon), 0x5a0) * 4); };
    const auto take_ammo = [&](int32_t weapon) {
        ammo(weapon) -= amount;
        if (ammo(weapon) >= 0) return;
        clip(weapon) += ammo(weapon);  // the shortfall comes out of the clip
        ammo(weapon) = 0;
        if (clip(weapon) < 0) clip(weapon) = 0;
    };
    const bool all = Q_stricmpn(name, tstr(0x0083bd34), 99999) == 0;  // "all"
    bool weapons = false;
    bool ammo_block = false;
    if (all || !Q_stricmpn(name, tstr(0x008367c8), 6)) {  // "health"
        if (amount) {
            const int32_t health = pk<int32_t>(ent, 0x350) - amount;
            pk<int32_t>(ent, 0x350) = health;
            if (health < 1) pk<int32_t>(ent, 0x350) = 1;
        } else {
            pk<int32_t>(ent, 0x350) = 1;
        }
        if (!all) return;
        weapons = true;
    } else if (!Q_stricmp(tstr(0x0083bd2c), 0, name)) {  // "weapons"
        weapons = true;
    } else if (!Q_stricmpn(name, tstr(0x008367c0), 4)) {  // "ammo"
        ammo_block = true;
    }
    if (weapons) {
        const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
        for (int32_t weapon = 1; weapon <= last; ++weapon) {
            TitleRegs regs{};
            regs.eax = static_cast<uint32_t>(weapon);
            regs.ecx = u32(ps());
            qc_call_regs(0x002b5530, &regs, 0, nullptr);  // BG_RemoveWeaponFromSlot
            ammo(weapon) = 0;
            clip(weapon) = 0;
        }
        if (pk<int32_t>(ps(), 0x80)) {
            pk<int32_t>(ps(), 0x80) = 0;
            TitleRegs regs{};
            regs.esi = 0;
            qc_call_regs(0x002cde20, &regs, 0, nullptr);  // CG_SelectWeapon(0)
        }
        if (!all) return;
        ammo_block = true;
    }
    if (ammo_block) {
        if (amount) {
            const int32_t current = pk<int32_t>(ps(), 0x80);
            if (current) take_ammo(current);
        } else {
            const int32_t last = title_global<int32_t>(0x0095f4d4);
            for (int32_t weapon = 1; weapon <= last; ++weapon) {
                ammo(weapon) = 0;
                clip(weapon) = 0;
            }
        }
        if (!all) return;
    }
    if (Q_stricmpn(name, tstr(0x0083bd24), 7) || !amount) return;  // "allammo"
    const int32_t last = title_global<int32_t>(0x0095f4d4);
    for (int32_t weapon = 1; weapon <= last; ++weapon) take_ammo(weapon);
}

// ---- Push 8, batch 2: profile saves.

namespace {

constexpr uint32_t kScriptString_FromCString = 0x0011eb50;  // thiscall (string slot, text)
constexpr uint32_t kScriptString_ConcatCString = 0x0011eec0;  // cdecl (out slot, a slot, text)
constexpr uint32_t kScriptString_Release = 0x0011d640;      // thiscall (string slot)

void script_string_from(uint32_t* slot, const char* text) {
    title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(kScriptString_FromCString)(slot, text);
}
void script_string_release(uint32_t* slot) {
    title_function<void XB_THISCALL(uint32_t*)>(kScriptString_Release)(slot);
}
// A ScriptString's characters start 6 bytes into its record; null for none.
const char* script_string_text(uint32_t slot) {
    return slot ? reinterpret_cast<const char*>(static_cast<uintptr_t>(slot + 6)) : nullptr;
}

// A title callee with the given registers (EAX, ECX, EDX, ESI; EBX/EDI as handed in).
uint32_t profile_call(uint32_t function, uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t ebx,
                      uint32_t edi, uint32_t count = 0, const uint32_t* args = nullptr) {
    TitleRegs regs{};
    regs.eax = eax;
    regs.ecx = ecx;
    regs.edx = edx;
    regs.esi = esi;
    regs.ebx = ebx;
    regs.edi = edi;
    return qc_call_regs(function, &regs, count, args);
}

}  // namespace

uint8_t XB_CDECL Save_ProfilesDirExists() {
    static_assert(0x1db0 / 0x4c == 100);
    uint8_t entries[0x1db0];  // 100 save entries of 0x4c bytes, the name first
    const int32_t count = static_cast<int32_t>(Save_EnumSaveGames(entries));
    if (count < 0) return 0;
    for (int32_t i = 0; i < count; ++i)
        if (!std::memcmp(entries + i * 0x4c, tstr(0x0082aaec), 9)) return 1;  // "Profiles"
    return 0;
}

int32_t XB_CDECL Save_GetBlocksNeeded() {
    const int32_t clusters = title_function<int32_t XB_CDECL(int32_t)>(0x000d1e10)(0x400);  // Save_ClustersForSize
    const auto cluster_size = title_function<int32_t XB_CDECL()>(0x000d1ec0);               // Save_GetClusterSize
    int32_t needed = clusters * 4 + cluster_size() * 2;
    const int32_t size = cluster_size();
    needed = needed + size * 4 + size;
    uint32_t free_bytes[2] = {0, 0};
    title_function<int32_t XB_CDECL(uint32_t*)>(0x000d1b40)(free_bytes);  // Save_GetFreeSpace
    const int32_t bytes = needed - static_cast<int32_t>(free_bytes[0]);
    const int32_t blocks = static_cast<int32_t>(
        BytesToBlocks(static_cast<uint32_t>(bytes), static_cast<uint32_t>(bytes >> 31)));
    return blocks > 0 ? blocks : 1;
}

PLATFORM_ASM_ENTRY(ProfileSave_PrepareRequest_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(ProfileSave_PrepareRequest) "addl $4, %esp\n ret")

void XB_CDECL ProfileSave_PrepareRequest(uint8_t* self) {
    // self +4 the save request, +0x11c0 four profile buffers (Heap_AllocAligned), +0x11e7 four "read" flags.
    uint8_t* const request = self + 4;
    SaveRequest_SetFileName(request, tstr(0x0082aaec));  // "Profiles"
    const uint32_t clusters = static_cast<uint32_t>(
        title_function<int32_t XB_CDECL(int32_t)>(0x000d1e10)(0x400));  // Save_ClustersForSize
    uint32_t prefix = 0;
    uint32_t dir = 0;
    script_string_from(&prefix, reinterpret_cast<const char*>(static_cast<uintptr_t>(Save_GetData())));
    title_function<uint32_t* XB_CDECL(uint32_t*, uint32_t*, const char*)>(kScriptString_ConcatCString)(
        &dir, &prefix, tstr(0x0082aaec));  // the save path + "Profiles"
    script_string_release(&prefix);
    pk<uint32_t>(self, 0x11e7) = 0;
    char numbered[0x80];
    char first[0x80];
    for (int32_t i = 0; i < 4; ++i) {
        uint8_t*& buffer = pk<uint8_t*>(self, 0x11c0 + i * 4);
        if (!buffer)
            buffer = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00042c50)(0x20, clusters)));  // Heap_AllocAligned
        std::memset(buffer, 0, clusters);
        const uint32_t args[2] = {u32(script_string_text(dir)), static_cast<uint32_t>(i)};
        const char* const name = i > 0 ? ShortString_Printf(numbered, tstr(0x0082aae4), const_cast<uint32_t*>(args))  // "%s%d"
                                       : ShortString_Printf(first, tstr(0x0082ddd4), const_cast<uint32_t*>(args));  // "%s"
        SaveRequest_AddFile(request, name, u32(buffer), clusters);
    }
    script_string_release(&dir);
}

PLATFORM_ASM_ENTRY(ProfileSave_Construct_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(ProfileSave_Construct) "addl $4, %esp\n ret")

uint8_t* XB_CDECL ProfileSave_Construct(uint8_t* self) {
    pk<uint32_t>(self, 0) = 0x008287dc;  // vtable
    SaveRequest_Init(self + 4, tstr(0x0082dfeb));
    for (int32_t i = 0; i < 4; ++i) {  // four profiles of 0x3cc bytes from +0x2a8
        uint8_t* const profile = self + 0x2a8 + i * 0x3cc;
        profile[0] = 0;
        pk<int32_t>(profile, 0xe8) = -1;
        Profile_SetDefaults(profile);
        Profile_SetDefaults(profile);
        pk<uint32_t>(profile, 0x3a4) = 0;
        pk<uint32_t>(profile, 0x3a8) = 0;
        pk<uint32_t>(profile, 0x3ac) = 0;
    }
    self[0x11e5] = 0;
    pk<uint32_t>(self, 0x11bc) = 0;
    for (int32_t i = 0; i < 4; ++i) {
        self[0x11e7 + i] = 0;
        pk<uint32_t>(self, 0x11c0 + i * 4) = 0;
    }
    Save_Init(0);
    uint32_t prefix = 0;
    script_string_from(&prefix, tstr(0x0082dfeb));
    Save_SetPathPrefix(script_string_text(prefix));
    Save_SetListener(u32(self));
    crt_strncpy(reinterpret_cast<char*>(self + 0x11d0), tstr(0x0082ab0c), 0xc);  // "Alex"
    self[0x11db] = 0;
    self[0x11e4] = 0;
    pk<uint32_t>(self, 0x11e0) = 0;
    pk<uint32_t>(self, 0x11ec) = 0;
    script_string_release(&prefix);
    return self;
}

PLATFORM_ASM_ENTRY(ProfileSave_OnProfilesRead_entry,
                   "pushl %edi\n pushl %ebx\n pushl 12(%esp)\n" PLATFORM_ASM_CALL(ProfileSave_OnProfilesRead)
                   "addl $12, %esp\n ret $4")

void XB_CDECL ProfileSave_OnProfilesRead(uint8_t* self, uint32_t entry_ebx, uint32_t entry_edi) {
    for (int32_t i = 0; i < 4; ++i) {
        uint8_t* const profile = self + 0x28c + i * 0x3cc;
        std::memcpy(profile, pk_ptr(self, 0x11c0 + i * 4), 0xf3 * 4);
        self[0x11e7 + i] = Save_CheckVersion(reinterpret_cast<const char*>(profile));
    }
    self[0x11e5] = 0;
    uint8_t* const profile_menu = title_global<uint8_t*>(0x0095f8f8);
    if (profile_menu) {
        if (pk<uint32_t>(profile_menu, 0xc) != 0xf) return;
        uint8_t* const page = pk_ptr(profile_menu, 4);
        const uint32_t menu = pk<uint32_t>(page, 0x3c);
        profile_call(0x0033a2a0, u32(page), menu, u32(self), static_cast<uint32_t>(0), entry_ebx, entry_edi, 1,
                     &menu);  // ProfileMenu_OnProfilesRead (tail jump, menu in the argument slot)
        return;
    }
    uint8_t* const page = pk_ptr(title_global<uint8_t*>(0x0095f8fc), 4);
    profile_call(0x003381c0, u32(page), u32(self), u32(title_global<uint8_t*>(0x0095f8fc)), 0,
                 pk<uint32_t>(page, 0x1c), entry_edi);  // AutosaveMenu_OnProfilesRead (menu in EBX)
}

PLATFORM_ASM_ENTRY(ProfileSave_OnSaveOpComplete_entry,
                   "pushl %edi\n pushl %ebx\n pushl 12(%esp)\n pushl %ecx\n" PLATFORM_ASM_CALL(ProfileSave_OnSaveOpComplete)
                   "addl $16, %esp\n ret $4")

void XB_CDECL ProfileSave_OnSaveOpComplete(uint8_t* self, int32_t operation, uint32_t entry_ebx, uint32_t entry_edi) {
    // operation: 1 read profiles, 2 save, 3 delete, 4 format. 0x0095f8f8 = the profile menu (page +4, state +0xc,
    // menu +0x3c), 0x0095f8fc = the autosave menu (page +4, menu +0x1c).
    title_global<uint8_t>(0x008d50dd) = 1;
    const uint32_t result = Save_GetLastResult();
    uint8_t* const profile_menu = title_global<uint8_t*>(0x0095f8f8);
    const auto autosave_page = []() { return pk_ptr(title_global<uint8_t*>(0x0095f8fc), 4); };
    const uint32_t op = static_cast<uint32_t>(operation);
    if (result && !(result == 0xfffffff3 && operation == 2)) {
        if (operation == 1) self[0x11e5] = 0;
        if (profile_menu) {
            if (pk<uint32_t>(profile_menu, 0xc) != 0xf) return;
            uint8_t* const page = pk_ptr(profile_menu, 4);
            profile_call(0x0033a130, result, op, u32(page), pk<uint32_t>(page, 0x3c), entry_ebx,
                         entry_edi);  // ProfileMenu_OnSaveOpFailed
            return;
        }
        uint8_t* const page = autosave_page();
        profile_call(0x00333860, result, op, u32(page), pk<uint32_t>(page, 0x1c), entry_ebx,
                     entry_edi);  // AutosaveMenu_OnSaveOpFailed
        return;
    }
    switch (operation) {
    case 1: {
        const uint32_t arg = u32(self);
        profile_call(0x0033a490, op - 1, op, 0, u32(self), entry_ebx, entry_edi, 1, &arg);  // ProfileSave_OnProfilesRead
        return;
    }
    case 2:
        if (profile_menu) {
            if (pk<uint32_t>(profile_menu, 0xc) != 0xf) return;
            uint8_t* const menu = pk_ptr(pk_ptr(profile_menu, 4), 0x3c);
            const uint32_t ecx = (op & ~0xffu) | menu[0x34];
            if (menu[0x34])
                profile_call(0x00339fa0, u32(menu), ecx, 0, u32(self), entry_ebx, entry_edi);  // ProfileMenu_ShowDeleteSuccessDialog
            else
                profile_call(0x00334fe0, u32(menu), ecx, 0, u32(self), entry_ebx, entry_edi);  // ProfileMenu_ShowSaveSuccessDialog
            return;
        } else {
            uint8_t* const page = autosave_page();
            uint8_t* const menu = pk_ptr(page, 0x1c);
            const uint32_t ecx = (u32(title_global<uint8_t*>(0x0095f8fc)) & ~0xffu) | menu[0x39];
            if (menu[0x39])
                profile_call(0x00339c70, u32(menu), ecx, u32(page), u32(self), entry_ebx, entry_edi);  // AutosaveMenu_ShowDeleteSuccessDialog
            else
                profile_call(0x003339f0, u32(menu), ecx, u32(page), u32(self), entry_ebx, entry_edi);  // AutosaveMenu_OnSaveSucceeded
            return;
        }
    case 3:
        profile_call(0x00339fa0, op - 1, op, 0, u32(self), entry_ebx, entry_edi);  // ProfileMenu_ShowDeleteSuccessDialog
        return;
    case 4:
        if (profile_menu) {
            if (pk<uint32_t>(profile_menu, 0xc) != 0xf) return;
            profile_call(0x003388f0, u32(profile_menu), op, 0, u32(self), entry_ebx, entry_edi);  // ProfileMenu_ShowFormatSuccessDialog
        } else {
            profile_call(0x00338100, 0, op, 0, u32(self), entry_ebx, entry_edi);  // AutosaveMenu_ShowFormatSuccessDialog
        }
        return;
    default:
        return;
    }
}

char* XB_CDECL SEH_LocalizeTextMessage(const char* msg, const char* context, int32_t mode) {
    // Segments between the markers 0x14 (translate on), 0x15 (translate off) and 0x16 (the next segment's "%s"
    // is not a substitution) are translated and appended; a segment's first "%s" stays for the next segment
    // to fill in. Two rotating 256-byte results (0x0098bf58).
    int32_t& index = title_global<int32_t>(0x0098ad24);
    index = (index + 1) % 2;
    char* const out = &title_global<char>(0x0098bf58 + static_cast<uint32_t>(index) * 0x100);
    std::memset(out, 0, 0x100);
    int32_t length = 0;
    int32_t pending = 0;    // "%s" waiting in out
    int32_t translate = 1;
    int32_t escaped = 0;    // some "%s" turned into 0x16
    int32_t allow = 1;
    int32_t inserted = 0;   // this segment left a "%s" in out
    char segment[0x100];
    char rest[0x100] = {};
    const char* start = msg;
    const char* p = msg;
    if (!*msg) return out;
    do {
        const char c = *p;
        if (c && c != 0x14 && c != 0x15 && c != 0x16) {
            ++p;
            continue;
        }
        if (p > start) {
            int32_t n = static_cast<int32_t>(p - start);
            crt_strncpy(segment, start, static_cast<uint32_t>(n));
            segment[n] = 0;
            if (translate) {
                if (!Loc_TranslateString(segment, segment, context, mode)) return nullptr;
                n = static_cast<int32_t>(std::strlen(segment));
            }
            if (static_cast<uint32_t>(n + length) >= 0x100) {
                uint8_t* const a = title_global<uint8_t*>(0x0093f804);
                uint8_t* const b = title_global<uint8_t*>(0x00927b2c);
                if (a && pk<uint32_t>(a, 0x20) && b && pk<uint32_t>(b, 0x20) && mode != 1)
                    title_function<void XB_CDECL(int32_t, const char*, const char*, const char*)>(0x002938f0)(
                        1, tstr(0x008333b0), context, msg);  // "%s too long when translated"
            }
            for (int32_t i = 0; i < n - 1; ++i) {
                if (segment[i] != '%' || segment[i + 1] != 's') continue;
                if (allow && !inserted) {
                    ++pending;
                    inserted = 1;
                } else {
                    segment[i] = 0x16;
                    escaped = 1;
                }
            }
            if (pending - inserted) {
                // Fill the first "%s" of out with this segment.
                int32_t i = 0;
                bool found = false;
                for (; i < length - 1; ++i) {
                    if (out[i] == '%' && out[i + 1] == 's') {
                        std::strcpy(rest, out + i + 2);
                        out[i] = 0;
                        found = true;
                        break;
                    }
                }
                if (!found && length - 1 <= 0) i = 0;
                std::strcpy(out + i, segment);
                std::strcpy(out + i + n, rest);
                length -= 2;
                --pending;
            } else {
                std::strcpy(out + length, segment);
            }
            length += n;
            inserted = 0;
        }
        allow = 1;
        if (*p == 0x14) {
            translate = 1;
            ++p;
        } else if (*p == 0x15) {
            translate = 0;
            ++p;
        }
        if (*p == 0x16) {
            allow = 0;
            ++p;
        }
        start = p;
    } while (*start);
    if (escaped)
        for (int32_t i = 0; i < length; ++i)
            if (out[i] == 0x16) out[i] = '%';
    return out;
}

void XB_CDECL Sys_InitHeaps() {
    // {count, 16 x {size, align, count}}: pools of 0x20 / 0x40 / 0x80 bytes.
    uint32_t config[1 + 16 * 3];
    config[0] = 0;
    for (int32_t i = 0; i < 16; ++i) {
        config[1 + i * 3] = 0;
        config[2 + i * 3] = 4;
        config[3 + i * 3] = 0;
    }
    static const uint32_t kPools[3][2] = {{0x20, 30000}, {0x40, 2000}, {0x80, 100}};
    for (const auto& pool : kPools) {
        const uint32_t n = config[0];
        if (static_cast<int32_t>(n) >= 16) continue;
        config[1 + n * 3] = pool[0];
        config[2 + n * 3] = 4;
        config[3 + n * 3] = pool[1];
        config[0] = n + 1;
    }
    uint8_t* small = Z_Malloc(0x3c);
    title_global<uint8_t*>(0x0095f4a4) = small ? SmallHeap_Init(small, config, 0, 1) : nullptr;
    uint8_t* heap = Z_Malloc(0x494);
    title_global<uint8_t*>(0x0095f4a0) = heap ? Heap_CreateFromZone(heap, 0x0004b000) : nullptr;
    // One of 32 shutdown callbacks (0x00988784, count 0x00988780).
    const int32_t n = title_global<int32_t>(0x00988780);
    if (n < 32) {
        title_global<uint32_t>(0x00988784 + static_cast<uint32_t>(n) * 4) = 0x002fd6e0;
        ++title_global<int32_t>(0x00988780);
    }
}

namespace {

// The packed normalize used by Matrix_Orthonormalize (as Vec3A_Normalize): every lane times the Newton step of
// the rsqrt estimate of |x^2 + y^2 + z^2|, or times 0 for a zero sum (a NaN sum takes the Newton path).
Lanes ortho_normalize(const Lanes& in) {
    const Lanes squares = mul(in, in);
    platform_keep(squares);
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    float scale;
    if (sum == 0.0f) {
        scale = title_global<float>(0x0082e6a0);
    } else {
        const float a = abs_bits(sum);
        const float r = platform_rsqrt_estimate(a);
        scale = ((title_global<float>(0x0082e660) - (r * r) * a) * r) * title_global<float>(0x0082e670);  // 3, 0.5
    }
    return mul(in, scale);
}

// SHUFPS 0xc9: lanes (1, 2, 0, 3).
Lanes yzx(const Lanes& a) { return {{a.v[1], a.v[2], a.v[0], a.v[3]}}; }

// a x b as the title computes it: yzx(a * yzx(b) - yzx(a) * b), all four lanes.
Lanes cross_lanes(const Lanes& a, const Lanes& b) {
    const Lanes t = mul(yzx(a), b);
    const Lanes u = mul(a, yzx(b));
    return yzx(sub(u, t));
}

}  // namespace

void XB_THISCALL Matrix_Orthonormalize(float* m) {
    // Rows of four floats: row 0 normalized, row 2 = normalize(row 0 x row 1), row 1 = normalize(row 2 x row 0).
    const Lanes x = ortho_normalize(lanes(m));
    for (int32_t i = 0; i < 4; ++i) m[i] = x.v[i];
    const Lanes z = ortho_normalize(cross_lanes(x, lanes(m + 4)));
    for (int32_t i = 0; i < 4; ++i) m[8 + i] = z.v[i];
    const Lanes y = ortho_normalize(cross_lanes(lanes(m + 8), x));
    for (int32_t i = 0; i < 4; ++i) m[4 + i] = y.v[i];
}

// RotatePointAroundVector (dir in EAX, point in EBX; dst, degrees on the stack, cdecl).
PLATFORM_ASM_ENTRY(RotatePointAroundVector_entry,
                   "pushl 8(%esp)\n pushl 8(%esp)\n pushl %ebx\n pushl %eax\n" PLATFORM_ASM_CALL(RotatePointAroundVector)
                   "addl $16, %esp\n ret")

void XB_CDECL RotatePointAroundVector(const float* dir, const float* point, float* dst, float degrees) {
    // Q3's RotatePointAroundVector, inlined matrix products in the title's sum order: m = {vr, vup, dir} as
    // columns, zrot about z, rot = (m * zrot) * transpose(m); the zero and one entries are multiplied too.
    const float f0 = dir[0];
    const float f1 = dir[1];
    const float f2 = dir[2];
    float vr[3];
    PerpendicularVector(dir, vr);
    const float r0 = vr[0];
    const float r1 = vr[1];
    const float r2 = vr[2];
    const float rad = static_cast<float>(static_cast<long double>(degrees) * title_global<float>(0x0083ed00) /
                                         title_global<float>(0x0083eb60));  // * pi / 180 on the x87
    const float u0 = (r1 * f2) - (r2 * f1);
    const float u1 = (r2 * f0) - (r0 * f2);
    const float u2 = (r0 * f1) - (r1 * f0);
    float C = 0.0f;
    float Sn = 0.0f;
    FastSinCos(&Sn, 0, &C, rad);
    const float Z = 0.0f;
    const float One = title_global<float>(0x0083eb20);
    const float nS = Z - Sn;
    const float T00 = ((nS * u0) + (Z * f0)) + (C * r0);
    const float T01 = ((Z * f0) + (C * u0)) + (Sn * r0);
    const float T02 = ((Z * u0) + (Z * r0)) + (One * f0);
    const float T10 = ((nS * u1) + (Z * f1)) + (C * r1);
    const float T11 = ((Z * f1) + (C * u1)) + (Sn * r1);
    const float T12 = ((Z * u1) + (Z * r1)) + (One * f1);
    const float T20 = ((nS * u2) + (Z * f2)) + (C * r2);
    const float T21 = ((C * u2) + (Z * f2)) + (Sn * r2);
    const float T22 = ((Z * u2) + (Z * r2)) + (One * f2);
    const float R02 = ((f2 * T02) + (T01 * u2)) + (T00 * r2);
    const float R00 = ((r0 * T00) + (T02 * f0)) + (T01 * u0);
    const float R01 = ((u1 * T01) + (T02 * f1)) + (T00 * r1);
    const float R12 = ((f2 * T12) + (T11 * u2)) + (T10 * r2);
    const float R10 = ((T10 * r0) + (T11 * u0)) + (T12 * f0);
    const float R11 = ((u1 * T11) + (T10 * r1)) + (T12 * f1);
    const float R22 = ((f2 * T22) + (T21 * u2)) + (T20 * r2);
    const float R20 = ((T20 * r0) + (T21 * u0)) + (T22 * f0);
    const float R21 = ((u1 * T21) + (T20 * r1)) + (T22 * f1);
    dst[0] = ((point[2] * R02) + (point[0] * R00)) + (R01 * point[1]);
    dst[1] = ((point[2] * R12) + (point[0] * R10)) + (R11 * point[1]);
    dst[2] = ((point[2] * R22) + (point[0] * R20)) + (R21 * point[1]);
}

void XB_STDCALL StringTable_Load(uint8_t** tables, uint8_t* table, int32_t index) {
    // Zone `index` (of 99, 0x0095f638 +0x38): its pak header (+0x7c) lists sections {name, .., +0xe count, +0x10
    // files, +0x14 records} of 0x18 bytes; the "STRING" section's record for the current language (".en." ..
    // in its name) is read through one stream memory slot over `table`. Then the table is relocated in place.
    uint8_t* const zone = index >= 0 && index < 99 ? pk_ptr(title_global<uint8_t*>(0x0095f638), 0x38 + index * 4) : nullptr;
    uint8_t* const header = pk_ptr(zone, 0x7c);
    for (uint32_t i = 0; i < pk<uint32_t>(header, 0xc); ++i) {
        uint8_t* const section = pk_ptr(header, 8) + i * 0x18;
        if (title_function<int32_t XB_CDECL(const char*, const char*, uint32_t)>(0x001eeaf0)(  // _strncmp
                reinterpret_cast<const char*>(section), tstr(0x008379ac), 6))  // "STRING"
            continue;
        const auto has = [](uint8_t* record, uint32_t suffix) {
            return crt_strstr(pk<const char*>(record, 0), suffix) != nullptr;
        };
        for (uint32_t r = 0; r < pk<uint16_t>(section, 0xe); ++r) {
            uint8_t* const record = pk_ptr(section, 0x14) + r * 0x14;  // {name, .., +8 size, +0xc file, +0x10 offset}
            const int32_t& language = title_global<int32_t>(0x0095f5dc);  // g_languageIndex
            if (!((language == 0 && has(record, 0x008379a4)) || (language == 2 && has(record, 0x0083799c)) ||
                  (language == 1 && has(record, 0x00837994)) || (language == 3 && has(record, 0x0083798c)) ||
                  (language == 4 && has(record, 0x00837984))))
                continue;
            const uint32_t offset =
                pk<uint32_t>(pk_ptr(section, 0x10), pk<uint32_t>(record, 0xc) * 0x1c) + pk<uint32_t>(record, 0x10);
            const float one = title_global<float>(0x0083eb20);
            float block[4] = {one, one, one, 0.0f};
            uint32_t bits[2] = {0, 0};
            StreamMem_Alloc(title_global<uint32_t*>(0x0095f62c), bits, block);
            uint32_t slot[2] = {0, 0};  // {address, size}
            StreamMem_GetBlock(bits, title_global<uint8_t*>(0x0095f62c), slot, 0);
            // Positional as the original pushes them: (file, pak offset, memory, size) - File_ReadAt's declared
            // parameter names have buffer and offset the other way round.
            File_ReadAt(pk<uint32_t>(zone, 0x50), offset, slot[0], (pk<uint32_t>(record, 8) + 0x7fff) & 0xffff8000u);
            platform_rep_movs(table, reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(slot[0])), pk<uint32_t>(record, 8));
            StreamMem_MoveSlots(0, bits, title_global<uint32_t*>(0x0095f62c), -1);
            break;
        }
        break;
    }
    pk<uint32_t>(table, 0x18) += u32(table);
    Pack_RelocateTable(reinterpret_cast<PackRelocTable*>(pk_ptr(table, 0x18)), u32(table));
    tables[index + 1] = table;
}

// ---- Push 8, batch 3: boot.

void XB_CDECL Sys_Main() {
    // The XAPI main (mainXapiStartup): allocator hooks, launch data, devices, renderer, effects, heaps, Com_Init,
    // the Live UI (UIXLayout.uix), then the frame loop forever. Its frame (0x4618 bytes) holds the hooks (+0x14),
    // the launch data (+0x2c, handed to Com_Init) and the Live manager (+0x12c, 0x44d4 bytes).
    static uint8_t frame[0x4624];  // never returns: the frame lives as long as the program
    uint8_t* const live = frame + 0x12c;
    uint32_t* const hooks = reinterpret_cast<uint32_t*>(frame + 0x14);
    static const uint32_t kHooks[6] = {0x00293080, 0x002968e0, 0x00296760, 0x00296770, 0x00296820, 0x00296780};
    for (int32_t i = 0; i < 6; ++i) hooks[i] = kHooks[i];
    Mem_SetAllocatorHooks(hooks);
    TitleRegs regs{};
    regs.eax = u32(live);
    qc_call_regs(0x00224920, &regs, 0, nullptr);  // LiveMgr_Construct (EAX)
    title_function<void XB_CDECL(uint8_t*)>(0x002221b0)(frame + 0x2c);  // Sys_ReadLaunchInfo
    title_function<void XB_CDECL(int32_t)>(0x00042ac0)(0);               // Heap_SelectByIndex
    FS_InitPackBuffers();
    title_function<void XB_CDECL()>(0x000ce070)();  // SndSys_InitDevice
    Sys_Milliseconds();
    title_function<void XB_CDECL()>(0x00094160)();  // FxShaderClass_Create
    title_function<void XB_CDECL()>(0x00045790)();  // nglInitShaders
    const auto frame_memory = title_function<void XB_CDECL(int32_t, uint32_t, int32_t)>(0x000778b0);  // nglSetFrameMemorySize
    frame_memory(0, 0xc0000, 1);
    frame_memory(1, 0x8000, 1);
    frame_memory(3, 0x20000, 1);
    frame_memory(2, 0x10000, 1);
    frame_memory(4, 0x80000, 1);
    const auto vblank = title_function<void XB_CDECL(int32_t)>(0x000645c0);  // nglSetVBlankMode
    vblank(2);
    uint32_t* const mode = reinterpret_cast<uint32_t*>(frame + 0x10);
    mode[0] = 2;
    mode[1] = 0;
    title_function<void XB_CDECL(uint32_t*, int32_t)>(0x000649e0)(mode, 2);  // nglSelectVideoMode
    Cvar_Set2(tstr(0x00839874), const_cast<char*>(tstr(title_global<uint8_t>(0x00902899) ? 0x0082e058 : 0x0082e098)),
              1);  // cg_widescreen "1" / "0"
    title_function<void XB_CDECL()>(0x00064bc0)();  // nglInit
    title_function<void XB_CDECL()>(0x002982d0)();  // SCR_InitLoadingSpinner
    regs = TitleRegs{};
    regs.ebx = 1;
    qc_call_regs(0x00298170, &regs, 0, nullptr);  // SCR_UpdateLoadingIcon_2 (BL = 1)
    title_function<void XB_CDECL(int32_t, uint32_t, uint32_t)>(0x00094610)(0, 0x2000000, 0xfa0);  // FX_InitSystem
    regs = TitleRegs{};
    regs.eax = 0x80000;
    regs.ebx = 0;
    qc_call_regs(0x00236c80, &regs, 0, nullptr);  // Mem_InitSmallBlockPools (BL = 0, EAX)
    vblank(2);
    uint8_t* allocator = Z_Malloc(0x49c);
    if (allocator) {
        pk<uint32_t>(allocator, 0) = 0x008376c4;  // HeapAllocator vtable
        uint8_t* const memory = Z_Malloc(0x100000);
        pk<uint32_t>(allocator, 4) = u32(memory);
        title_function<void XB_CDECL(uint8_t*, uint8_t*, uint8_t*, int32_t)>(0x00042470)(  // Heap_Init
            allocator + 8, memory, memory + 0x100000, 0);
    }
    title_function<void XB_CDECL(uint8_t*)>(0x0007bac0)(allocator);  // Anim_InitManagers
    Sys_InitHeaps();
    title_function<void XB_CDECL(uint8_t*)>(0x002a7490)(frame + 0x2c);  // Com_Init(launch data)
    title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e7767)(0x00223130);  // SetUnhandledExceptionFilter
    const uint32_t device = title_global<uint32_t>(0x00902874);
    uint8_t* stream = Z_Malloc(8);
    if (stream) {
        pk<uint32_t>(stream, 0) = 0x0083c720;
        pk<uint32_t>(stream, 4) = 0;
    }
    title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x0002c442)(0x0083cc30, 0x10000, u32(stream) + 4);
    regs = TitleRegs{};
    regs.esi = u32(live);
    const uint32_t init_args[3] = {device, 0x0083cc18, u32(stream)};  // "d:\media\UIXLayout.uix"
    qc_call_regs(0x00224a50, &regs, 3, init_args);  // Live_Init (manager in ESI)
    uint32_t ebx = regs.ebx;
    const int32_t launch_mode = title_global<int32_t>(0x008dd128);
    pk<uint32_t>(frame, 0x459c) = title_global<uint32_t>(0x008dd124);
    if (launch_mode == 2) {
        regs = TitleRegs{};
        regs.eax = 0x40140;
        regs.ecx = u32(live);
        regs.esi = u32(live);
        const uint32_t arg = 0x008dcf1c;
        qc_call_regs(0x00222d20, &regs, 1, &arg);
        regs.ecx = title_global<uint32_t>(0x008dcf18);
        regs.ebx = 1;
        const uint32_t voice = title_global<uint8_t>(0x008dd1cd);
        qc_call_regs(0x00221920, &regs, 1, &voice);  // Voice_InitNotifications
        ebx = 1;
    } else if (launch_mode == 1) {
        regs = TitleRegs{};
        regs.eax = 0x40140;
        regs.esi = u32(live);
        if (title_global<uint8_t>(0x008dd120)) {
            regs.ecx = u32(live);
            const uint32_t arg = 0x008dcf1c;
            qc_call_regs(0x00222d20, &regs, 1, &arg);
        } else {
            regs.ebx = 0x40140;
            qc_call_regs(0x00221fc0, &regs, 0, nullptr);
            ebx = 0x40140;
        }
    } else {
        pk<uint32_t>(title_global<uint8_t*>(0x0095f6fc), 0x446c) = title_global<uint32_t>(0x008dd12c);  // g_liveMgr
    }
    (void)ebx;
    const auto set_input = title_function<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x000d35a9);  // CLiveEngine_SetInput
    const auto clear_input = title_function<void XB_STDCALL(uint32_t)>(0x000d35c2);               // LiveEngine_ClearInput
    for (;;) {
        IN_Frame();
        const uint32_t input = pk<uint32_t>(frame, 0x45fc);
        if (title_global<uint8_t>(0x0095f538)) {
            set_input(pk<uint32_t>(live, 0x14), input, 0);
            clear_input(pk<uint32_t>(live, 0x14));
        }
        regs = TitleRegs{};
        regs.ecx = u32(live);
        qc_call_regs(0x00223730, &regs, 0, nullptr);  // Live_Frame
        if (title_global<uint8_t>(0x0095f538)) {
            set_input(pk<uint32_t>(live, 0x14), input, 0);
            clear_input(pk<uint32_t>(live, 0x14));
        }
        title_function<void XB_CDECL(uint32_t, uint32_t)>(0x0005e650)(0x00224970, 0);  // nglSetPreSwapCallback(Live_Render)
        Com_Frame();
    }
}

namespace {

// A boot callee by title address with the given registers (EBX, ESI, EDI, ECX, EDX, EAX).
uint32_t boot_call(uint32_t function, uint32_t eax = 0, uint32_t ebx = 0, uint32_t ecx = 0, uint32_t edx = 0,
                   uint32_t esi = 0, uint32_t edi = 0, uint32_t count = 0, const uint32_t* args = nullptr) {
    TitleRegs regs{};
    regs.eax = eax;
    regs.ebx = ebx;
    regs.ecx = ecx;
    regs.edx = edx;
    regs.esi = esi;
    regs.edi = edi;
    return qc_call_regs(function, &regs, count, args);
}

// Heap_AllocAligned (0x00042c60: alignment, size).
uint8_t* boot_alloc(uint32_t alignment, uint32_t size) {
    return reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00042c60)(alignment, size)));
}

// A manager constructed in a Heap_AllocAligned block (object in `reg`: 'a' EAX, 'b' EBX, 'd' EDX, 'D' EDI, else
// ESI); the global gets its result, 0 when the allocation failed.
void boot_construct(uint32_t global, uint32_t alignment, uint32_t size, uint32_t ctor, char reg = 'S') {
    uint8_t* const block = boot_alloc(alignment, size);
    if (!block) {
        title_global<uint32_t>(global) = 0;
        return;
    }
    const uint32_t b = u32(block);
    title_global<uint32_t>(global) = boot_call(ctor, reg == 'a' ? b : 0, reg == 'b' ? b : 0, 0, reg == 'd' ? b : 0,
                                               reg == 'S' ? b : 0, reg == 'D' ? b : 0);
}

// Cbuf_AddText inlined: appended only when it fits (no message), without the terminator.
void boot_cbuf_append(uint32_t text, uint32_t length) {
    const int32_t size = title_global<int32_t>(0x00935330);
    if (size + static_cast<int32_t>(length) >= title_global<int32_t>(0x0093532c)) return;
    std::memcpy(title_global<uint8_t*>(0x00935328) + size, tstr(text), length);
    title_global<int32_t>(0x00935330) = size + static_cast<int32_t>(length);
}

}  // namespace

void XB_CDECL Com_Init(uint8_t* launch) {
    title_global<uint8_t>(0x0093fcd0) = 0;
    Com_InitSmallHeap();
    boot_call(0x000280a0, 0, 0x40, 0, 0, 0x00984cec);  // DynArray_ReserveAligned (count EBX, array ESI)
    boot_call(0x000280a0, 0, 0x20, 0, 0, 0x00987a78);  // g_entityRemoveQueue
    boot_call(0x000280a0, 0, 0x20, 0, 0, 0x0097a640);
    std::memset(&title_global<uint8_t>(0x00940400), 0, 0x600);  // com_pushedEvents
    title_global<uint32_t>(0x0095f588) = 0;
    title_global<uint32_t>(0x0095f58c) = 0;
    title_global<uint8_t*>(0x00940358) = Cvar_Get(tstr(0x008340d0), tstr(0x0082e098), 0x48);  // sv_cheats "0"
    title_function<void XB_CDECL()>(0x002a23b0)();  // Cvar_Init
    boot_call(0x002938a0, u32(launch));            // Com_ParseCommandLine (EAX)
    title_function<void XB_CDECL()>(0x002b0140)();  // Swap_Init
    title_global<uint32_t>(0x00935328) = 0x0092bf90;  // cmd_text {data, 0x2000, 0}
    title_global<uint32_t>(0x0093532c) = 0x2000;
    title_global<uint32_t>(0x00935330) = 0;
    title_global<uint32_t>(0x00929c10) = 0x00929ce0;
    title_global<uint32_t>(0x00929c14) = 0x2000;
    title_global<uint32_t>(0x00929c18) = 0;
    title_function<void XB_CDECL()>(0x002d1460)();  // Cmd_Init
    Com_StartupVariable(nullptr);
    Com_StartupVariable(tstr(0x00837ce4));  // "developer"
    const auto add_command = title_function<void XB_CDECL(const char*, uint32_t)>(0x002ba5c0);  // Cmd_AddCommand
    add_command(tstr(0x0083453c), 0x002c1c20);  // bind
    add_command(tstr(0x00834534), 0x002c1db0);  // unbind
    add_command(tstr(0x00834528), 0x002c1d60);  // unbindall
    add_command(tstr(0x0083451c), 0x002b3370);  // bindlist
    FS_InitFilesystem();
    const uint32_t keep = 0;
    boot_call(0x002c5ac0, 0, 0x0095fe70, 0, 0, 0, 0, 1, &keep);  // Loc_ResetStringFileState (state EBX)
    Loc_Init();
    title_function<uint32_t XB_CDECL()>(0x00055e40)();  // IN_PadCreate
    boot_construct(0x0095f480, 4, 0x11f0, 0x00330640, 'D');  // ProfileSave_Construct
    boot_construct(0x0095f638, 4, 0x57c, 0x00290020);        // Pak_Init
    boot_construct(0x0095f62c, 4, 0x2c, 0x002909d0);         // Stream_InitBanks
    boot_construct(0x0095f630, 4, 0x1e8, 0x0028ec30);        // ResourceManager_Construct
    title_function<void XB_CDECL()>(0x000239c0)();           // ResourceManager_CreateLgrid
    title_function<void XB_CDECL()>(0x00023900)();           // ResourceManager_CreateModels
    title_function<void XB_CDECL()>(0x00023960)();           // ResourceManager_CreateXmpb
    title_function<void XB_CDECL()>(0x00023f90)();           // ResourceManager_CreateAitb
    boot_construct(0x0095f4c4, 4, 0x601c, 0x002bda60);       // SoundManager_Ctor
    title_function<void XB_CDECL()>(0x00023ed0)();           // ResourceManager_CreateSoundBanks
    title_global<uint8_t*>(0x0095f4c8) = boot_alloc(4, 4);
    uint8_t* const fade = boot_alloc(4, 0x20);
    if (fade) {
        const float one = title_global<float>(0x0083eb20);
        pk<uint32_t>(fade, 0) = 0;
        pk<uint32_t>(fade, 4) = 0;
        pk<float>(fade, 8) = one;
        pk<float>(fade, 0x10) = one;
        pk<float>(fade, 0xc) = one;
        pk<float>(fade, 0x14) = one;
        pk<float>(fade, 0x18) = 0.0f;
        pk<uint32_t>(fade, 0x1c) = 0;
    }
    title_global<uint8_t*>(0x0095f4cc) = fade;
    boot_construct(0x0095f640, 0x10, 0x1e0, 0x0028f620);  // ZoneGraph_Init
    title_function<void XB_CDECL()>(0x00023ff0)();        // ResourceManager_CreateDb
    boot_construct(0x0095f5b4, 0x10, 0x8e30, 0x0029fbd0); // FXCmdQueue_Ctor
    title_function<void XB_CDECL()>(0x00023f30)();        // ResourceManager_CreateDlg
    title_function<void XB_CDECL()>(0x00023a20)();        // ResourceManager_CreateLevel
    boot_construct(0x0095f4f8, 4, 0x1a8, 0x002c03e0);     // SceneFX_Init -> scn_zoneScenes
    title_function<void XB_CDECL()>(0x00024050)();        // ResourceManager_CreateCfgstr
    boot_construct(0x0095f4f4, 4, 0x198, 0x002b1190, 'd'); // g_pathNodeBlocks (EDX)
    title_function<void XB_CDECL()>(0x00023e70)();        // ResourceManager_CreateLocStrings
    title_global<uint8_t*>(0x0095f5e0) = boot_alloc(1, 0x800);
    boot_construct(0x0095f4e0, 4, 0x598, 0x002cdca0);     // Checkpoint_Init
    boot_construct(0x0095f564, 4, 0x184, 0x002b6fc0, 'a'); // SpgManager_Construct (EAX) -> rm_spg
    title_function<void XB_CDECL()>(0x00234450)();        // Phys_Init
    boot_construct(0x0095f4fc, 4, 0x328, 0x002d8a10);     // CM_WorldTrees_Construct
    boot_construct(0x0095f500, 0x10, 0x1e0, 0x002d0d60, 'b');  // CM_WorldTreeManager_Construct (EBX)
    title_function<void XB_CDECL()>(0x00023d80)();        // ResourceManager_CreateAnimTrees
    title_function<void XB_CDECL()>(0x00024260)();        // ShakeRumble_CreateManager
    title_global<uint8_t>(0x00940178) = 0;
    title_global<uint8_t*>(0x0095f674) = Cvar_Get(tstr(0x0083be30), tstr(0x0082e098), 0x100);  // statusbar
    boot_call(0x00337050, 0, 0, 0, 0, 0x0095f8d0);  // FE_CreateMenus (g_uiRoot in ESI)
    uint8_t* const screens = Z_Malloc(0x1c);
    if (screens) {
        const uint32_t arg = u32(screens);
        title_global<uint32_t>(0x0095f8fc) = boot_call(0x00331680, 0, 0, 0, 0, 0, 0, 1, &arg);  // InGameScreens_Construct
    } else {
        title_global<uint32_t>(0x0095f8fc) = 0;
    }
    uint8_t* const hud = Z_Malloc(0x64);
    title_global<uint32_t>(0x0095f8f0) = hud ? boot_call(0x0031a020, 0, 0, 0, 0, u32(hud)) : 0;  // HudManager_Create
    boot_call(0x002ddea0, 0, 0, 0, 0, 0, title_global<uint32_t>(0x0095f534));
    // The global zone loaded at once, with 0x00299780 as the pak's callback (+0x35c) meanwhile.
    pk<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x35c) = 0x00299780;
    const uint32_t load_args[6] = {0, 0x00837cd4, 0, 0, 0, 0};  // "sp\global.cod", a zero float block
    boot_call(0x00292d10, title_global<uint32_t>(0x0095f638), 0, 0, 0, 0, 0, 6, load_args);  // Stream_LoadSync
    const uint32_t pad = title_function<uint32_t XB_CDECL()>(0x00055e40)();  // IN_PadCreate
    boot_call(0x00055e70, pad, 0, pad);                                      // IN_PadUpdate (ECX)
    // The "Animation" zone of the pak's zone map (+0x18: map at +8, count +0x10, zones +0x14).
    uint8_t* const zones = pk_ptr(title_global<uint8_t*>(0x0095f638), 0x18);
    uint8_t* zone = nullptr;
    if (zones) {
        uint32_t key = 0x00837cc8;  // "Animation"
        const uint32_t key_arg = u32(&key);
        const auto found = reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(boot_call(0x000249a0, 0, u32(zones + 8), 0, 0, 0, 0, 1, &key_arg)));  // StringMap_Find
        if (found) {
            uint32_t index = *found;
            if (index >= pk<uint32_t>(zones, 0x10)) index = 0;
            zone = pk_ptr(pk_ptr(zones, 0x14), index * 4);
        }
    }
    if (!zone) title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
    if (zone) {
        const uint32_t frame = title_global<uint32_t>(0x008d9d38) + 1;
        pk<float>(zone, 0x34) = 0.0f;
        title_global<uint32_t>(0x008d9d38) = frame;
    }
    const uint32_t zone_arg = u32(zone);
    boot_call(0x00292d70, 0, 0, title_global<uint32_t>(0x0095f638), 0, 0, 0, 1, &zone_arg);  // Stream_StartRequest
    boot_call(0x0023b360, 0, 0, 0, 0, 0x00988770);  // R_InitDebugMeshes (ESI)
    pk<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x35c) = 0;
    boot_call(0x002ce9c0, 0, 0, 0, 0, 0, title_global<uint32_t>(0x0095f4c0));  // S_WaitForWaveBanks (rm_soundBanks)
    title_function<void XB_CDECL()>(0x0029fe70)();  // Com_InitJournaling
    boot_cbuf_append(0x00837cb4, 0x12);  // "exec language.cfg\n"
    boot_cbuf_append(0x00837ca4, 0xd);   // "exec bro.cfg\n"
    boot_cbuf_append(0x00837c90, 0x12);  // "exec autoexec.cfg\n"
    if (title_function<int32_t XB_CDECL()>(0x002999e0)())  // Com_SafeMode
        boot_call(0x002aabb0, 0x00837c7c);                // Cbuf_AddText("exec safemode.cfg\n")
    title_function<void XB_CDECL()>(0x002c62b0)();         // Cbuf_Execute
    boot_call(0x00332e20, 0, 0, 0, 0, 0, 0x008dce30);      // CL_ApplyProfileSettings (profile in EDI)
    Com_StartupVariable(nullptr);
    if (title_global<uint32_t>(0x0095f580) != title_global<uint32_t>(0x0095f578)) {
        boot_call(0x00297990, title_global<uint32_t>(0x0095f580));  // FS_FreeSearchPaths (EAX)
        title_global<uint32_t>(0x0095f580) = title_global<uint32_t>(0x0095f578);
    }
    if (title_global<uint32_t>(0x0095f584) != title_global<uint32_t>(0x0095f57c)) {
        boot_call(0x00297950, title_global<uint32_t>(0x0095f584));  // FS_FreePackList (EAX)
        title_global<uint32_t>(0x0095f584) = title_global<uint32_t>(0x0095f57c);
    }
    title_global<uint8_t>(0x0095f918) = title_global<uint8_t>(0x0095ee01) == 0 ? 1 : 0;
    title_global<uint32_t>(0x009403ec) &= 0xfffffffeu;  // cvar_modifiedFlags
    struct BootCvar {
        uint32_t global, name, value, flags;
    };
    static const BootCvar kCvars[] = {
        {0x0094770c, 0x00837c70, 0x0082e098, 1},      {0x0093fc18, 0x00837ce4, 0x0082e098, 0x100},
        {0x009472f0, 0x00837c5c, 0x0082e098, 0x100},  {0x0093fccc, 0x00837c54, 0x0082e098, 0},
        {0x0093fc20, 0x00837c48, 0x0082e098, 0},      {0x0093fce4, 0x00837c3c, 0x0082e058, 0x208},
        {0x0093fcc0, 0x00837c30, 0x0082e098, 0x200},  {0x0093fcb4, 0x00837c28, 0x0082e098, 0x200},
        {0x009403e8, 0x00837c1c, 0x0082e098, 0},      {0x0093fcd8, 0x0082ca00, 0x0082e098, 0},
        {0x0093fcbc, 0x00837c0c, 0x0082e098, 0},      {0x0093fcf0, 0x00832cb8, 0x0082e098, 0x40},
        {0x00947704, 0x00834184, 0x0082e098, 0x40},   {0x009472e8, 0x00837bfc, 0x0082e098, 0},
    };
    for (const BootCvar& c : kCvars) title_global<uint8_t*>(c.global) = Cvar_Get(tstr(c.name), tstr(c.value), c.flags);
    uint8_t* const developer = title_global<uint8_t*>(0x0093fc18);
    if (developer && pk<uint32_t>(developer, 0x20)) {
        add_command(tstr(0x00837bf4), 0x00297090);  // error
        add_command(tstr(0x00837bec), 0x00293850);  // crash
        add_command(tstr(0x00837be4), 0x0029e8d0);  // freeze (Com_Freeze_f)
    }
    add_command(tstr(0x0082ac38), 0x002a6dc0);  // quit (Com_Quit_f)
    add_command(tstr(0x00837bd8), 0x00293820);  // writeconfig
    add_command(tstr(0x00837bc8), 0x00293810);  // writedefaults
    const uint32_t set = u32(Cvar_Set2(tstr(0x0083caa8), const_cast<char*>(tstr(0x0083cab0)), 1));  // arch "ps2"
    const uint32_t value_args[2] = {0x0083ca9c, 0x3f800000};  // sys_cpuid 1.0
    boot_call(0x0029fdd0, set, 0, 0, 0, 0, 0, 2, value_args);   // Cvar_SetValue
    title_function<void XB_CDECL()>(0x002be180)();  // IN_InitCvars
    title_function<void XB_CDECL()>(0x002c0ce0)();  // Netchan_Init
    title_function<void XB_CDECL()>(0x002a8410)();  // Com_InitNodeRing
    Com_Memset(&title_global<uint8_t>(0x00927c38), 0, 0x1a4);  // vmTable
    title_function<void XB_CDECL()>(0x002f48e0)();  // SV_Init
    title_function<void XB_CDECL()>(0x002f20a0)();  // CL_Init
    boot_call(0x0032cf50, 0, 0, 0, 0, 0x008dce30);  // Profile_SetDefaults (ESI)
    boot_call(0x00332e20, 0, 0, 0, 0, 0x008dce30, 0x008dce30);  // CL_ApplyProfileSettings (EDI)
    title_global<int32_t>(0x00947710) = title_function<int32_t XB_CDECL()>(0x0029c6b0)();  // com_frameTime
    title_function<void XB_CDECL()>(0x002970c0)();  // Com_AddStartupCommands
    Cvar_Set2(tstr(0x00835238), const_cast<char*>(tstr(0x0082e058)), 1);  // r_uiFullScreen "1"
    title_function<void XB_CDECL()>(0x002f0a50)();  // CL_StartHunkUsers
    Cvar_Set2(tstr(0x00837c48), const_cast<char*>(tstr(0x0082e098)), 1);  // com_statmon "0"
    title_global<uint32_t>(0x00947b04) = 1;
}

}  // namespace game
