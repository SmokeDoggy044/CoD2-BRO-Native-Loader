#include "qcommon_core.h"

#include <cstring>

#include "lanes.h"
#include "qcommon.h"
#include "qcommon_fs.h"
#include "r_init.h"
#include "snd.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t& word_at(const uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(base) + offset);
}

template <typename T>
T* pointer_at(const uint8_t* base, uint32_t offset) {
    return reinterpret_cast<T*>(uintptr_t{word_at(base, offset)});
}

}  // namespace

uint8_t XB_REGS2 XB_STDCALL KeyList_Contains(uint32_t, const uint8_t* list, const uint32_t* key) {
    const uint8_t* node = pointer_at<const uint8_t>(list, 0x10);
    const uint8_t* next = node ? pointer_at<const uint8_t>(node, 0) : nullptr;
    if (node == list + 0x14 || !next) return 0;
    const uint32_t wanted = *key;
    for (;;) {
        if (word_at(node, 8) == wanted) return 1;
        node = next;
        next = pointer_at<const uint8_t>(next, 0);
        if (!next) return 0;
    }
}

uint8_t XB_REGS3 XB_CDECL Vec3_IsNonZero(uint32_t, uint32_t, const float* v) {
    const float zero = title_global<float>(0x00922eb8);
    return v[0] != zero || v[1] != zero || v[2] != zero;
}

uint8_t* XB_REGS1 XB_CDECL HandleTable256_Init(uint8_t* table) {
    uint32_t* const words = reinterpret_cast<uint32_t*>(table);
    for (int32_t i = 7; i >= 0; --i) words[i] = 0;
    for (int32_t i = 0; i < 0x100; ++i) {
        words[8 + i * 2] = 0;
        words[9 + i * 2] = 1;
    }
    word_at(table, 0x820) = 0;
    for (int32_t i = 0; i < 0x100; ++i) words[i >> 5] |= 1u << (i & 0x1f);
    return table;
}

PLATFORM_ASM_ENTRY(HandleTable256_Free_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(HandleTable256_Free) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL HandleTable256_Free(uint8_t* table, uint32_t handle) {
    if (!handle) return;
    const uint32_t slot = handle & 0xff;
    uint32_t& generation = word_at(table, 0x24 + slot * 8);
    if (generation != handle >> 8) return;
    word_at(table, (slot >> 5) * 4) |= 1u << (slot & 0x1f);
    ++generation;
    word_at(table, 0x20 + slot * 8) = 0;
}

PLATFORM_ASM_ENTRY(IntHashMap_Find_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(IntHashMap_Find) "addl $12, %esp\n"
                   "ret")

namespace {

// The title's hash_map find (IntHashMap_Find / UintHashMap_Find; Key = the signedness of the
// ordered compare): bucket = (key ^ 0xdeadbeef) & mask, folded down when not below the limit; the
// first node in [buckets[b], buckets[b + 1]) whose key is not less, if equal, else the end.
template <typename Key>
void hash_map_find(uint32_t* out, Key wanted, const uint8_t* map) {
    const uint32_t mask = word_at(map, 0x20);
    uint32_t bucket = (static_cast<uint32_t>(wanted) ^ 0xdeadbeef) & mask;
    if (!(word_at(map, 0x24) > bucket)) bucket += 0xffffffffu - (mask >> 1);
    const uint32_t* const buckets = pointer_at<const uint32_t>(map, 0x14);
    const uint32_t last = buckets[bucket + 1];
    for (uint32_t node = buckets[bucket]; node != last;
         node = word_at(reinterpret_cast<const uint8_t*>(uintptr_t{node}), 0)) {
        const Key node_key = static_cast<Key>(word_at(reinterpret_cast<const uint8_t*>(uintptr_t{node}), 8));
        if (node_key >= wanted) {
            *out = wanted >= node_key ? node : word_at(map, 8);
            return;
        }
    }
    *out = word_at(map, 8);
}

}  // namespace

void XB_CDECL IntHashMap_Find(uint32_t* out, const int32_t* key, const uint8_t* map) {
    hash_map_find<int32_t>(out, *key, map);
}

PLATFORM_ASM_ENTRY(UintHashMap_Find_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(UintHashMap_Find) "addl $12, %esp\n"
                   "ret")

void XB_CDECL UintHashMap_Find(uint32_t* out, const uint32_t* key, const uint8_t* map) {
    hash_map_find<uint32_t>(out, *key, map);
}

uint32_t* XB_REGS3 XB_CDECL BitSet256_Complement(uint32_t* dst, uint32_t, const uint32_t* src) {
    uint32_t inverted[8];
    for (int32_t i = 7; i >= 0; --i) inverted[i] = ~src[i];
    for (int32_t i = 0; i < 8; ++i) dst[i] = inverted[i];
    return dst;
}

PLATFORM_ASM_ENTRY(std_vector_Erase4_2_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(std_vector_Erase4_2) "addl $16, %esp\n"
                   "ret $8")

void XB_CDECL std_vector_Erase4_2(uint32_t* out, uint8_t* vector, uint32_t* first, uint32_t* last) {
    if (first != last) {
        uint32_t* const end = pointer_at<uint32_t>(vector, 8);
        uint32_t* dest = first;
        for (const uint32_t* from = last; from != end; ++from) *dest++ = *from;
        word_at(vector, 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dest));
    }
    *out = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(first));
}

uint32_t* XB_REGS3 XB_CDECL std_CopyBackward4_2(uint32_t* dest, const uint32_t* first, const uint32_t* last) {
    while (first != last) {
        const uint32_t value = *--last;
        *--dest = value;
    }
    return dest;
}

uint8_t XB_THISCALL HashStr_Equals(const uint32_t* a, const uint32_t* b) {
    for (int32_t i = 0; i < 8; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

void XB_CDECL Matrix_Identity(float* out) {
    // Rows 0-2: the constant rows times lane 0 of 0x0082e140 (1.0); row 3 copied as words.
    const float scale = title_global<float>(0x0082e140);
    static const uint32_t kRows[3] = {0x0082e160, 0x0082e170, 0x0082e180};
    for (int32_t row = 0; row < 3; ++row)
        for (int32_t lane = 0; lane < 4; ++lane)
            out[row * 4 + lane] = (&title_global<float>(kRows[row]))[lane] * scale;
    uint32_t* const last = reinterpret_cast<uint32_t*>(out + 12);
    for (int32_t lane = 0; lane < 4; ++lane) last[lane] = title_global<uint32_t>(0x0082e290 + lane * 4);
}

void XB_REGS1 XB_CDECL SoundBankTable_Init(uint8_t* table) {
    word_at(table, 0) = 0;
    for (int32_t slot = 0; slot < 16; ++slot) {
        uint8_t* const base = table + 4 + slot * 0x84;
        for (int32_t i = 0; i < 8; ++i) word_at(base, i * 4) = 0;
        word_at(base, 0x20) = 0xffffffffu;
        for (int32_t i = 0; i < 6; ++i) {
            word_at(base, 0x24 + i * 4) = 0;
            word_at(base, 0x3c + i * 4) = 0;
            word_at(base, 0x54 + i * 4) = 0xffffffffu;
            word_at(base, 0x6c + i * 4) = 0xffffffffu;
        }
    }
}

uint8_t* XB_REGS3 XB_CDECL Array48_GetOrFirst(uint32_t index, uint32_t, const uint32_t* array) {
    if (index >= array[0]) index = 0;
    return reinterpret_cast<uint8_t*>(uintptr_t{array[1]}) + index * 48;
}

void XB_REGS1 XB_STDCALL DynArray_Erase12(uint32_t* pos, uint32_t* array, const uint32_t* last) {
    const uint32_t* const end = reinterpret_cast<const uint32_t*>(uintptr_t{array[0]}) + array[2] * 3;
    if (pos == end) return;
    for (const uint32_t* from = last; from != end; from += 3, pos += 3) {
        pos[0] = from[0];
        pos[1] = from[1];
        pos[2] = from[2];
    }
    if (pos == end) return;
    uint32_t count = array[2];
    do {
        pos += 3;
        --count;
    } while (pos != end);
    array[2] = count;
}

int32_t XB_REGS3 XB_CDECL std_vector_string_Size(uint32_t, uint32_t, const uint32_t* vector) {
    if (!vector[1]) return 0;
    return static_cast<int32_t>(vector[2] - vector[1]) / 28;
}

void XB_REGS3 XB_CDECL SoundBankSlot_Copy(uint32_t* dst, uint32_t, const uint32_t* src) {
    for (int32_t i = 0; i < 0x21; ++i) dst[i] = src[i];
}

void XB_REGS2 XB_CDECL HandleBits512_Invert(uint32_t* dst, const uint32_t* src) {
    uint32_t inverted[16];
    for (int32_t i = 15; i >= 0; --i) inverted[i] = ~src[i];
    for (int32_t i = 0; i < 16; ++i) dst[i] = inverted[i];
}

namespace {

// BitSet256_NextSet / BitIter_Next: iterator {words, current word, word index, result}.
void bit_iterator_next(uint32_t* iterator, int32_t last_word) {
    if (iterator[2] == 0xffffffffu) return;
    if (!iterator[1]) {
        const uint32_t* const words = reinterpret_cast<const uint32_t*>(uintptr_t{iterator[0]});
        while (static_cast<int32_t>(iterator[2]) < last_word) {
            ++iterator[2];
            iterator[1] = words[iterator[2]];
            if (iterator[1]) break;
        }
    }
    if (!iterator[1]) {
        iterator[3] = 0xffffffffu;
        iterator[2] = 0xffffffffu;
        return;
    }
    uint32_t bit = 0;
    while (!(iterator[1] & (1u << bit))) ++bit;  // BSF
    iterator[1] &= ~(1u << bit);
    iterator[3] = (iterator[2] << 5) + bit;
}

uint8_t* node_link(const uint8_t* node, uint32_t offset) { return pointer_at<uint8_t>(node, offset); }

// The string compare of the trees (the title's inline basic_string::compare): the common prefix
// as unsigned bytes (rep cmpsb), then the sizes.
const uint8_t* string_data(const uint8_t* text) {
    return word_at(text, 0x14) >= 0x10 ? pointer_at<const uint8_t>(text, 0) : text;
}

int32_t string_compare(const uint8_t* a_data, uint32_t a_size, const uint8_t* b_data, uint32_t b_size) {
    if (a_size) {
        const uint32_t n = a_size >= b_size ? b_size : a_size;
        for (uint32_t i = 0; i < n; ++i)
            if (a_data[i] != b_data[i]) return a_data[i] < b_data[i] ? -1 : 1;
    }
    if (a_size < b_size) return -1;
    return a_size != b_size;
}

// The key string at node+0xc: its data (+0x10 / buffer) and size (+0x20).
template <uint32_t Nil>
uint8_t* tree_lower_bound(const uint8_t* tree, const uint8_t* key) {
    uint8_t* const head = node_link(tree, 4);
    uint8_t* result = head;
    uint8_t* node = node_link(head, 4);
    while (!node[Nil]) {
        // key: data at +4 (pointer when the capacity +0x18 >= 16), size +0x14
        const uint8_t* const key_data = word_at(key, 0x18) >= 0x10 ? pointer_at<const uint8_t>(key, 4) : key + 4;
        const uint8_t* const node_data = string_data(node + 0x10);
        if (string_compare(node_data, word_at(node, 0x20), key_data, word_at(key, 0x14)) < 0) {
            node = node_link(node, 8);
        } else {
            result = node;
            node = node_link(node, 0);
        }
    }
    return result;
}

template <uint32_t Nil>
void tree_lrotate(uint8_t* node, uint8_t* tree) {
    uint8_t* const pivot = node_link(node, 8);
    word_at(node, 8) = word_at(pivot, 0);
    uint8_t* const inner = node_link(pivot, 0);
    if (!inner[Nil]) word_at(inner, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    word_at(pivot, 4) = word_at(node, 4);
    const uint32_t pivot_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pivot));
    uint8_t* const head = node_link(tree, 4);
    if (node == node_link(head, 4)) {
        word_at(head, 4) = pivot_word;
    } else {
        uint8_t* const parent = node_link(node, 4);
        if (node == node_link(parent, 0))
            word_at(parent, 0) = pivot_word;
        else
            word_at(parent, 8) = pivot_word;
    }
    word_at(pivot, 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    word_at(node, 4) = pivot_word;
}

template <uint32_t Nil>
void tree_rrotate(uint8_t* node, uint8_t* tree) {
    uint8_t* const pivot = node_link(node, 0);
    word_at(node, 0) = word_at(pivot, 8);
    uint8_t* const inner = node_link(pivot, 8);
    if (!inner[Nil]) word_at(inner, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    word_at(pivot, 4) = word_at(node, 4);
    const uint32_t pivot_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pivot));
    uint8_t* const head = node_link(tree, 4);
    if (node == node_link(head, 4)) {
        word_at(head, 4) = pivot_word;
    } else {
        uint8_t* const parent = node_link(node, 4);
        if (node == node_link(parent, 8))
            word_at(parent, 8) = pivot_word;
        else
            word_at(parent, 0) = pivot_word;
    }
    word_at(pivot, 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    word_at(node, 4) = pivot_word;
}

template <uint32_t Nil>
void tree_dec(uint8_t** iterator) {
    uint8_t* const node = *iterator;
    if (node[Nil]) {  // the head: its right link is the rightmost node
        *iterator = node_link(node, 8);
        return;
    }
    uint8_t* left = node_link(node, 0);
    if (!left[Nil]) {
        for (uint8_t* right = node_link(left, 8); !right[Nil]; right = node_link(left, 8)) left = right;
        *iterator = left;
        return;
    }
    uint8_t* parent = node_link(node, 4);
    if (parent[Nil]) return;
    while (*iterator == node_link(parent, 0)) {
        *iterator = parent;
        parent = node_link(parent, 4);
        if (parent[Nil]) break;
    }
    if (!parent[Nil]) *iterator = parent;
}

template <uint32_t Nil>
void tree_inc(uint8_t** iterator) {
    uint8_t* const node = *iterator;
    if (node[Nil]) return;
    uint8_t* right = node_link(node, 8);
    if (!right[Nil]) {
        for (uint8_t* left = node_link(right, 0); !left[Nil]; left = node_link(right, 0)) right = left;
        *iterator = right;
        return;
    }
    uint8_t* parent = node_link(node, 4);
    if (!parent[Nil]) {
        while (*iterator == node_link(parent, 8)) {
            *iterator = parent;
            parent = node_link(parent, 4);
            if (parent[Nil]) break;
        }
    }
    *iterator = parent;
}

template <uint32_t Nil, uint32_t Link>
uint8_t* tree_extreme(uint8_t* node) {
    for (uint8_t* next = node_link(node, Link); !next[Nil]; next = node_link(node, Link)) node = next;
    return node;
}

}  // namespace

void XB_REGS2 XB_CDECL BitSet256_NextSet(uint32_t, uint32_t* iterator) { bit_iterator_next(iterator, 7); }
void XB_REGS2 XB_CDECL BitIter_Next(uint32_t, uint32_t* iterator) { bit_iterator_next(iterator, 15); }

uint8_t* XB_REGS3 XB_CDECL std_map_string_LowerBound(const uint8_t* tree, uint32_t, const uint8_t* key) {
    return tree_lower_bound<0x65>(tree, key);
}
uint8_t* XB_REGS3 XB_CDECL std_map_string_LowerBound_2(const uint8_t* tree, uint32_t, const uint8_t* key) {
    return tree_lower_bound<0x2d>(tree, key);
}
uint8_t* XB_REGS3 XB_CDECL std_tree_LowerBound29(const uint8_t* tree, uint32_t, const uint8_t* key) {
    return tree_lower_bound<0x29>(tree, key);
}

uint8_t* XB_REGS1 XB_STDCALL std_tree_UpperBound29(const uint8_t* tree, const uint8_t* key) {
    uint8_t* const head = node_link(tree, 4);
    uint8_t* result = head;
    uint8_t* node = node_link(head, 4);
    while (!node[0x29]) {
        const uint8_t* const node_data = string_data(node + 0x10);
        const uint8_t* const key_data = word_at(key, 0x18) >= 0x10 ? pointer_at<const uint8_t>(key, 4) : key + 4;
        if (string_compare(key_data, word_at(key, 0x14), node_data, word_at(node, 0x20)) < 0) {
            result = node;
            node = node_link(node, 0);
        } else {
            node = node_link(node, 8);
        }
    }
    return result;
}

void XB_THISCALL std_tree_Lrotate29(uint8_t* node, uint8_t* tree) { tree_lrotate<0x29>(node, tree); }
void XB_THISCALL std_tree_Rrotate29(uint8_t* node, uint8_t* tree) { tree_rrotate<0x29>(node, tree); }
void XB_THISCALL std_map_string_Lrotate(uint8_t* node, uint8_t* tree) { tree_lrotate<0x65>(node, tree); }
void XB_THISCALL std_map_string_Rrotate(uint8_t* node, uint8_t* tree) { tree_rrotate<0x65>(node, tree); }
void XB_THISCALL std_set_string_Lrotate(uint8_t* node, uint8_t* tree) { tree_lrotate<0x2d>(node, tree); }
void XB_THISCALL std_set_string_Rrotate(uint8_t* node, uint8_t* tree) { tree_rrotate<0x2d>(node, tree); }

void XB_REGS2 XB_CDECL std_tree_Dec29(uint32_t, uint8_t** iterator) { tree_dec<0x29>(iterator); }
void XB_REGS2 XB_CDECL std_set_string_Dec(uint32_t, uint8_t** iterator) { tree_dec<0x2d>(iterator); }
void XB_REGS2 XB_CDECL std_map_string_Dec(uint32_t, uint8_t** iterator) { tree_dec<0x65>(iterator); }
void XB_REGS2 XB_CDECL std_tree_Inc29(uint32_t, uint8_t** iterator) { tree_inc<0x29>(iterator); }
void XB_REGS2 XB_CDECL std_set_string_Inc(uint32_t, uint8_t** iterator) { tree_inc<0x2d>(iterator); }
void XB_REGS2 XB_CDECL std_map_string_Inc(uint32_t, uint8_t** iterator) { tree_inc<0x65>(iterator); }

uint8_t* XB_REGS1 XB_CDECL std_tree_Max29(uint8_t* node) { return tree_extreme<0x29, 8>(node); }
uint8_t* XB_REGS1 XB_CDECL std_tree_Min29(uint8_t* node) { return tree_extreme<0x29, 0>(node); }
uint8_t* XB_REGS1 XB_CDECL std_map_string_Max(uint8_t* node) { return tree_extreme<0x65, 8>(node); }
uint8_t* XB_REGS1 XB_CDECL std_map_string_Min(uint8_t* node) { return tree_extreme<0x65, 0>(node); }
uint8_t* XB_REGS1 XB_CDECL std_set_string_Max(uint8_t* node) { return tree_extreme<0x2d, 8>(node); }
uint8_t* XB_REGS1 XB_CDECL std_set_string_Min(uint8_t* node) { return tree_extreme<0x2d, 0>(node); }

PLATFORM_ASM_ENTRY(LoadBits_Progress_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(LoadBits_Progress) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL LoadBits_Progress(const uint32_t* bits) {
    const float half = title_global<float>(0x0083eb24);
    float sum = 0.0f;
    for (int32_t bit = 0; bit < 0x40; ++bit) {
        const uint32_t mask = 1u << (bit & 0x1f);
        if (bits[bit >> 5] & mask) sum = sum + half;
        if (bits[(bit >> 5) + 2] & mask) sum = sum + half;
    }
    return sum;
}

uint8_t* XB_REGS1 XB_STDCALL NameTable12_FindFrom1(const uint32_t* table, const char* name) {
    const uint32_t count = table[0];
    for (uint32_t i = 1; i < count; ++i) {
        const uint32_t index = i < count ? i : 0;
        const char* entry = reinterpret_cast<const char*>(uintptr_t{pointer_at<const uint32_t>(
            reinterpret_cast<const uint8_t*>(table), 4)[index * 3]});
        if (!std::strcmp(entry, name)) return pointer_at<uint8_t>(reinterpret_cast<const uint8_t*>(table), 4) + index * 12;
    }
    return nullptr;
}

void XB_REGS1 XB_CDECL BlockTail80_Clear(uint8_t* record) {
    for (uint32_t offset = 0x80; offset < 0x100; offset += 4) word_at(record, offset) = 0;
}

void XB_THISCALL Vec3A_CopyXYZ(uint32_t* dst, const uint32_t* src) {
    const uint32_t w = dst[3];
    const uint32_t x = src[0], y = src[1], z = src[2], src_w = src[3];
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = src_w;
    dst[3] = w;
}

namespace {

uint8_t* current_heap() { return reinterpret_cast<uint8_t*>(uintptr_t{title_global<uint32_t>(0x00901ae8)}); }

}  // namespace

void XB_CDECL Heap_Init(uint8_t* heap, uint32_t base, uint32_t end, uint32_t tag) {
    if (base == end) return;
    for (uint32_t i = 0; i < 0x125; ++i) word_at(heap, i * 4) = 0;
    word_at(heap, 0x480) = end;
    word_at(heap, 0x478) = end;
    word_at(heap, 0x488) = 0;
    word_at(heap, 0x48c) = 0;
    word_at(heap, 0x47c) = base;
    word_at(heap, 0x474) = base;
    word_at(heap, 0x484) = end - base;
    word_at(heap, 0x490) = tag;
}

int32_t XB_CDECL Heap_DetachRegion(uint8_t* heap) {
    for (uint32_t offset = 0x474; offset <= 0x484; offset += 4) word_at(heap, offset) = 0;
    return word_at(heap, 0x488) ? -4 : 1;
}

uint32_t XB_CDECL Heap_Sbrk(uint32_t size) {
    uint8_t* const heap = current_heap();
    const uint32_t top = word_at(heap, 0x47c);
    const uint32_t next = size + top;
    if (next >= word_at(heap, 0x478)) return 0xffffffffu;
    word_at(heap, 0x47c) = next;
    return top;
}

uint32_t XB_CDECL Heap_GetCurrent() { return title_global<uint32_t>(0x00901ae8); }

uint32_t XB_CDECL Heap_SelectByIndex(int32_t index) {
    const uint32_t previous = title_global<uint32_t>(0x00901ae8);
    title_global<uint32_t>(0x00901ae8) = static_cast<uint32_t>(index) * 0x494 + 0x008ff638;
    return previous;
}

uint32_t XB_CDECL Heap_SetCurrent(uint32_t heap) {
    const uint32_t previous = title_global<uint32_t>(0x00901ae8);
    title_global<uint32_t>(0x00901ae8) = heap;
    return previous;
}

void XB_CDECL Str_AppendBounded(char* dst, int32_t* length, int32_t max, const char* src) {
    if (!src) return;
    const char* from = src;  // the argument slots are compared: local copies
    char* out = dst + *length;
    if (*from) {
        for (;;) {
            const int32_t used = *length;
            if (used >= max) break;
            *length = used + 1;
            *out = *from;
            const char next = from[1];
            ++out;
            ++from;
            if (!next) break;
        }
    }
    *out = 0;
}

void XB_CDECL FixedStr_Copy(char* dst, int32_t* length, int32_t count, const char* src, int32_t max) {
    const int32_t n = count >= max ? max : count;
    *length = n;
    char* out = dst;  // the argument slots are compared: local copies
    const char* from = src;
    for (uint32_t words = static_cast<uint32_t>(n / 4); words; --words) {
        uint32_t word;
        std::memcpy(&word, from, 4);
        std::memcpy(out, &word, 4);
        out += 4;
        from += 4;
    }
    for (uint32_t bytes = static_cast<uint32_t>(*length % 4); bytes; --bytes) *out++ = *from++;
    *out = 0;
}

uint8_t XB_THISCALL SmallHeap_Owns(const uint32_t* heaps, uint32_t ptr) {
    const uint32_t* const end = heaps + 1 + heaps[0];
    for (const uint32_t* it = heaps + 1; it != end; ++it) {
        const uint8_t* const heap = reinterpret_cast<const uint8_t*>(uintptr_t{*it});
        if (ptr >= word_at(heap, 0x18) && ptr <= word_at(heap, 0x1c)) return 1;
    }
    return 0;
}

void XB_CDECL Mem_SetAllocatorHooks(const uint32_t* hooks) {
    for (uint32_t i = 0; i < 6; ++i) title_global<uint32_t>(0x009098d8 + i * 4) = hooks[i];
}

namespace {

// The title's packed normalize: lanes * (Newton step of the rsqrt estimate of |x^2 + y^2 + z^2|),
// or * the zero vector's lane 0 when the sum is 0 (a NaN sum takes the Newton path).
float* normalize_lanes(float* out, const float* v, uint32_t three, uint32_t half, uint32_t zero) {
    const Lanes in = lanes(v);
    const Lanes squares = mul(in, in);
    platform_keep(squares);
    const float sum = (squares.v[0] + squares.v[1]) + squares.v[2];
    float scale;
    if (sum == 0.0f) {
        scale = title_global<float>(zero);
    } else {
        const float a = abs_bits(sum);
        const float r = platform_rsqrt_estimate(a);
        scale = ((title_global<float>(three) - (r * r) * a) * r) * title_global<float>(half);
    }
    const Lanes result = mul(in, scale);
    for (int i = 0; i < 4; ++i) out[i] = result.v[i];
    return out;
}

void store4(float* out, const Lanes& v) {
    for (int i = 0; i < 4; ++i) out[i] = v.v[i];
}

Lanes splat(float f) { return {{f, f, f, f}}; }

}  // namespace

float* XB_CDECL Vec3A_Normalize(float* out, const float* v) {
    return normalize_lanes(out, v, 0x0082e660, 0x0082e670, 0x0082e6a0);
}

float* XB_CDECL Vec3A_NormalizeDup(float* out, const float* v) {
    return normalize_lanes(out, v, 0x007cd630, 0x007cd640, 0x007cd670);
}

float* XB_CDECL Vec3_Scale(float* out, float x, float y, float z, float s) {
    const float rx = x * s;
    const float ry = y * s;
    const float rz = z * s;
    out[0] = rx;
    out[1] = ry;
    out[2] = rz;
    return out;
}

void XB_REGS3 XB_CDECL Sphere_Merge(const float* center_b, float* radius_a, float* center_a, float radius_b) {
    const Lanes d = sub(lanes(center_b), lanes(center_a));
    const Lanes squares = mul(d, d);
    platform_keep(squares);
    const float distance2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    const float diff = radius_b - *radius_a;
    if (diff * diff >= distance2) {
        if (radius_b > *radius_a) {  // B holds A
            const uint32_t* const from = reinterpret_cast<const uint32_t*>(center_b);
            uint32_t* const to = reinterpret_cast<uint32_t*>(center_a);
            for (int i = 0; i < 4; ++i) to[i] = from[i];
            *radius_a = radius_b;
        }
        return;
    }
    // On the x87: the new radius (distance + ra + rb) / 2 and the offset fraction diff / distance.
    const long double distance = platform_x87_sqrt_ext(static_cast<long double>(distance2));
    *radius_a = static_cast<float>(((distance + *radius_a) + radius_b) * title_global<float>(0x0083eb24));
    const float t = static_cast<float>(static_cast<long double>(diff) / distance);
    const Lanes a = lanes(center_a);  // re-read after the radius store
    const Lanes b = lanes(center_b);
    const Lanes sum = add(add(b, a), mul(splat(t), d));
    store4(center_a, mul(splat(title_global<float>(0x0083eb24)), sum));
}

float* XB_CDECL PhysBody_RotateVector(float* out, const uint8_t* body, const float* v) {
    if (body[0x144] & 0x10) {
        const uint32_t* const from = reinterpret_cast<const uint32_t*>(v);
        uint32_t* const to = reinterpret_cast<uint32_t*>(out);
        for (int i = 0; i < 4; ++i) to[i] = from[i];
        return out;
    }
    const Lanes in = lanes(v);
    const float* const m = reinterpret_cast<const float*>(body);
    const Lanes r = add(add(mul(lanes(m), splat(in.v[0])), mul(lanes(m + 4), splat(in.v[1]))),
                        mul(lanes(m + 8), splat(in.v[2])));
    store4(out, r);
    return out;
}

long double XB_CDECL FastAcos(float x) {
    const float a = abs_bits(x);
    float r;
    if (platform_x87_greater(title_global<float>(0x0083eb24), a)) {  // 0.5 > |x|: asin series
        const float a2 = a * a;
        const float a3 = a2 * a;
        const float t1 = a3 * title_global<float>(0x0083ee9c);
        const float a5 = a3 * a2;
        const float t2 = a5 * title_global<float>(0x0083ee98);
        const float t3 = (a5 * a2) * title_global<float>(0x0083ee94);
        r = ((t3 + t2) + t1) + a;
    } else {
        const float s = platform_x87_sqrt_abs_sub_mul(title_global<float>(0x0083eb20), a, title_global<float>(0x0083eb24));
        const float s2 = s * s;
        const float s3 = s2 * s;
        const float t0 = s * title_global<float>(0x0083eba8);
        const float t1 = s3 * title_global<float>(0x0083ee90);
        const float s5 = s3 * s2;
        const float t2 = s5 * title_global<float>(0x0083ed2c);
        const float t3 = (s5 * s2) * title_global<float>(0x0083ee8c);
        r = (((t3 - t2) - t1) - t0) + title_global<float>(0x0083ee88);
    }
    const float half_pi = title_global<float>(0x0083ed0c);
    const float v = 0.0f > x ? 0.0f - r : r;
    return static_cast<long double>(half_pi) - static_cast<long double>(v);
}

float XB_CDECL RayPlaneIntersectFrac(const float* a, const float* n, const float* b, const float* d) {
    const Lanes normal = lanes(n);
    const Lanes nd = mul(normal, lanes(d));
    platform_keep(nd);
    const float denominator = (nd.v[0] + nd.v[1]) + nd.v[2];
    if (platform_x87_greater(title_global<float>(0x0083ed50), abs_bits(denominator)))
        return title_global<float>(0x007cc950);  // 1e20
    const Lanes w = mul(normal, sub(lanes(a), lanes(b)));
    platform_keep(w);
    return ((w.v[0] + w.v[1]) + w.v[2]) / denominator;
}

void XB_CDECL BitMask64_Unpack(uint32_t* dst, uint32_t mask_lo, uint32_t mask_hi, const uint32_t* src) {
    uint64_t mask = (uint64_t{mask_hi} << 32) | mask_lo;
    for (uint64_t bit = 1; mask; bit <<= 1, ++dst) {
        if (mask & bit) {
            mask &= ~bit;
            *dst = *src++;
        }
    }
}

int32_t XB_REGS1 XB_CDECL BitMask64_IndexOfBit(const uint32_t* mask, uint32_t bit_lo, uint32_t bit_hi) {
    const uint32_t lo = mask[0], hi = mask[1];
    if ((lo & bit_lo) != bit_lo || (hi & bit_hi) != bit_hi) return -1;
    const uint64_t below = ((uint64_t{bit_hi} << 32 | bit_lo) * 2) - 1;
    return static_cast<int32_t>(Snd_PopCount64_3(static_cast<uint32_t>(below) & lo,
                                                 static_cast<uint32_t>(below >> 32) & hi)) - 1;
}

uint32_t XB_CDECL apsColor_PackScaled(const float* scale, const float* color) {
    const Lanes factors = {{scale[0], scale[1], scale[2], title_global<float>(0x0083eb20)}};
    const Lanes c = mul(mul(lanes(color), splat(title_global<float>(0x0083eb8c))), factors);
    const float zero = title_global<float>(0x0083eafc);
    const float top = title_global<float>(0x0083eb8c);
    float clamped[4];
    for (int i = 0; i < 4; ++i) {
        const float low = zero > c.v[i] ? zero : c.v[i];  // MAXPS 0, c
        clamped[i] = top < low ? top : low;              // MINPS 255, m
    }
    uint32_t packed = static_cast<uint32_t>(platform_ftol(clamped[3])) << 8;
    packed = (packed | static_cast<uint32_t>(platform_ftol(clamped[0]))) << 8;
    packed = (packed | static_cast<uint32_t>(platform_ftol(clamped[1]))) << 8;
    return static_cast<uint32_t>(platform_ftol(clamped[2])) | packed;
}

float* XB_CDECL Mat34_FromAxisSinCos(float* out, const float* axis, float sine, float cosine) {
    const Lanes a = lanes(axis);
    const Lanes k = splat(title_global<float>(0x0083eb20) - cosine);
    const Lanes p = mul({{a.v[0] * a.v[0], a.v[1], a.v[2], a.v[3]}}, k);  // MULSS, then MULPS
    const Lanes q = mul(mul(a, splat(a.v[1])), k);
    const Lanes r = mul(mul(a, splat(a.v[2])), k);
    const Lanes s = mul(mul(a, a), k);
    const Lanes t = mul(a, splat(sine));
    platform_keep(p);
    platform_keep(q);
    platform_keep(r);
    platform_keep(s);
    const float d0 = p.v[0] + cosine;
    store4(out, {{d0, t.v[2] + q.v[0], r.v[0] - t.v[1], d0}});
    const float e0 = q.v[0] - t.v[2];
    store4(out + 4, {{e0, s.v[1] + cosine, t.v[0] + r.v[1], e0}});
    const float f0 = t.v[1] + r.v[0];
    store4(out + 8, {{f0, r.v[1] - t.v[0], s.v[2] + cosine, f0}});
    return out + 8;
}

void XB_CDECL Mat34_FromRotationVector(float* out, const float* v, float scale) {
    const Lanes in = lanes(v);
    const Lanes squares = mul(in, in);
    platform_keep(squares);
    const float length2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    float length = length2;  // 0 or NaN
    if (!(length2 == 0.0f || length2 != length2)) {
        const float r = platform_rsqrt_estimate(length2);
        const float lr = length2 * r;
        length = ((title_global<float>(0x007cd630) - lr * r) * lr) * title_global<float>(0x007cd640);
    }
    if (title_global<float>(0x0083effc) > length) {
        Matrix_Identity(out);
        return;
    }
    const long double angle = static_cast<long double>(length) * scale;
    const float inverse = title_global<float>(0x0083eb20) / length;
    alignas(16) float axis[4];
    store4(axis, mul(splat(inverse), in));
    const float cosine = static_cast<float>(platform_x87_cos_ext(angle));
    const float sine = static_cast<float>(platform_x87_sin_ext(angle));
    Mat34_FromAxisSinCos(out, axis, sine, cosine);
}

namespace {

Lanes yzxw(const Lanes& v) { return {{v.v[1], v.v[2], v.v[0], v.v[3]}}; }

// a x b as the title's packed code: (a * b.yzx - a.yzx * b).yzx.
Lanes cross_packed(const Lanes& a, const Lanes& b) { return yzxw(sub(mul(a, yzxw(b)), mul(yzxw(a), b))); }

}  // namespace

void XB_CDECL Mat34_RotationBetweenVectors(float* out, const float* a, const float* b) {
    const Lanes va = lanes(a), vb = lanes(b);
    const Lanes c = cross_packed(va, vb);
    const Lanes squares = mul(c, c);
    platform_keep(squares);
    const float length2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    if (title_global<float>(0x0083effc) > length2) {
        Matrix_Identity(out);
        return;
    }
    const Lanes products = mul(va, vb);
    const float sine = static_cast<float>(platform_x87_sqrt_ext(static_cast<long double>(length2)));
    const float inverse = static_cast<float>(static_cast<long double>(title_global<float>(0x0083eb20)) / sine);
    alignas(16) float axis[4];
    store4(axis, mul(c, splat(inverse)));
    platform_keep(products);
    const float dot = (products.v[0] + products.v[1]) + products.v[2];
    const long double dd = static_cast<long double>(dot) * dot;
    const long double ss = static_cast<long double>(sine) * sine;
    const float h = static_cast<float>(platform_x87_sqrt_ext(dd + ss));
    const float r = title_global<float>(0x0083eb20) / h;
    const float cosine = r * dot;
    const float s = r * sine;
    Mat34_FromAxisSinCos(out, axis, s, cosine);
}

void XB_REGS3 XB_CDECL Quat_RotationBetween(const float* a, uint32_t, const float* b, float* out) {
    const Lanes va = lanes(a), vb = lanes(b);
    alignas(16) float c[4];
    store4(c, cross_packed(va, vb));
    const Lanes products = mul(va, vb);
    platform_keep(products);
    const float dot = (products.v[0] + products.v[1]) + products.v[2];
    const Lanes squares = mul(lanes(c), lanes(c));
    platform_keep(squares);
    const float length2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    float sine = length2;  // 0 or NaN
    if (!(length2 == 0.0f || length2 != length2)) {
        const float r = platform_rsqrt_estimate(length2);
        const float lr = length2 * r;
        sine = ((title_global<float>(0x0082e660) - lr * r) * lr) * title_global<float>(0x0082e670);
    }
    alignas(16) float unit[4];
    const Lanes n = lanes(Vec3A_Normalize(unit, c));
    const Lanes m = sub(n, mul(n, splat(dot)));
    const Lanes ns = mul(n, splat(sine));
    Lanes q;  // -ns: the sign bits of 0x0082e0b0
    for (int i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &ns.v[i], 4);
        q.v[i] = from_bits(bits ^ title_global<uint32_t>(0x0082e0b0 + i * 4));
    }
    const Lanes row0 = add(Lanes{{dot, ns.v[2], q.v[1], q.v[1]}}, mul(m, splat(n.v[0])));
    const Lanes row1 = add(Lanes{{q.v[2], dot, ns.v[0], ns.v[0]}}, mul(m, splat(n.v[1])));
    const Lanes row2 = add(Lanes{{ns.v[1], q.v[0], dot, dot}}, mul(m, splat(n.v[2])));
    store4(out, row0);
    store4(out + 4, row1);
    store4(out + 8, row2);
    uint32_t* const last = reinterpret_cast<uint32_t*>(out + 12);
    for (int i = 0; i < 4; ++i) last[i] = title_global<uint32_t>(0x0082e290 + i * 4);
}

void XB_CDECL Mat44_InverseRigid_2(float* out, const float* m) {
    if (out == m) {
        alignas(16) float copy[16];
        Mat44_InverseRigid_2(copy, m);
        platform_rep_movs(out, copy, 0x40);
        return;
    }
    alignas(16) float rows[12];
    platform_rep_movs(rows, m, 0x30);
    const float* const r0 = rows;
    const float* const r1 = rows + 4;
    const float* const r2 = rows + 8;
    const float t0[4] = {r0[0], r1[0], r2[0], r2[0]};
    const float t1[4] = {r0[1], r1[1], r2[1], r2[1]};
    const float t2[4] = {r0[2], r1[2], r2[2], r2[2]};
    std::memcpy(rows, t0, 16);
    std::memcpy(rows + 4, t1, 16);
    std::memcpy(rows + 8, t2, 16);
    platform_rep_movs(out, rows, 0x30);
    const Lanes t = lanes(m + 12);  // read after the rows are stored
    const Lanes sum = add(add(mul(lanes(out), splat(t.v[0])), mul(lanes(out + 4), splat(t.v[1]))),
                          mul(lanes(out + 8), splat(t.v[2])));
    uint32_t* const last = reinterpret_cast<uint32_t*>(out + 12);
    for (int i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &sum.v[i], 4);
        last[i] = bits ^ title_global<uint32_t>(0x0082e0b0 + i * 4);
    }
}

namespace {

// The title's inline strcpy (byte loop, front to back).
void copy_string(char* dst, const char* src) {
    char c;
    do {
        c = *src++;
        *dst++ = c;
    } while (c);
}

}  // namespace

uint32_t XB_CDECL Save_GetLastResult() { return title_global<uint32_t>(0x00920aa4); }

void XB_CDECL Save_SetLastResult(uint32_t result) { title_global<uint32_t>(0x00920aa4) = result; }

void XB_CDECL Save_SetPathPrefix(const char* prefix) { copy_string(&title_global<char>(0x00920aa8), prefix); }

uint32_t XB_CDECL Save_GetData() {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&title_global<char>(0x00920aa8)));
}

void XB_CDECL Save_SetPort(int32_t port) { title_global<int32_t>(0x008c02d0) = port; }

int32_t XB_CDECL Save_GetPort() { return title_global<int32_t>(0x008c02d0); }

void XB_CDECL Save_SetListener(uint32_t listener) { title_global<uint32_t>(0x00920af0) = listener; }

void XB_CDECL Save_SetDeviceRemovedListener(uint32_t listener) { title_global<uint32_t>(0x00920af8) = listener; }

uint32_t XB_CDECL Save_GetBuffer() { return title_global<uint32_t>(0x00920ae8); }

uint32_t XB_CDECL Save_GetBufferSize() { return title_global<uint32_t>(0x00920aec); }

uint32_t XB_CDECL Save_GetOpState() { return title_global<uint32_t>(0x00920af4); }

int32_t XB_CDECL Save_LastPort() { return 8; }

void XB_THISCALL SaveRequest_SetFileName(uint8_t* request, const char* name) {
    copy_string(reinterpret_cast<char*>(request + 0x248), name);
    word_at(request, 0x240) = 0;
    word_at(request, 0x244) = 0;
}

uint8_t XB_THISCALL SaveRequest_AddFile(uint8_t* request, const char* name, uint32_t a, uint32_t b) {
    const uint32_t count = word_at(request, 0x244);
    if (count >= 8) return 0;
    copy_string(reinterpret_cast<char*>(request + count * 0x40), name);
    word_at(request, 0x200 + word_at(request, 0x244) * 4) = a;
    word_at(request, 0x220 + word_at(request, 0x244) * 4) = b;
    ++word_at(request, 0x244);
    return 1;
}

uint8_t XB_THISCALL SaveRequest_NextFile(uint8_t* request, char* name, uint32_t* a, uint32_t* b) {
    const uint32_t cursor = word_at(request, 0x240);
    if (!(cursor < word_at(request, 0x244))) return 0;
    copy_string(name, reinterpret_cast<const char*>(request + cursor * 0x40));
    *a = word_at(request, 0x200 + word_at(request, 0x240) * 4);
    *b = word_at(request, 0x220 + word_at(request, 0x240) * 4);
    ++word_at(request, 0x240);
    return 1;
}

char* XB_THISCALL SaveRequest_GetFileName(uint8_t* request) { return reinterpret_cast<char*>(request + 0x248); }

uint8_t* XB_THISCALL SaveRequest_Init(uint8_t* request, const char* name) {
    copy_string(reinterpret_cast<char*>(request + 0x248), name);
    word_at(request, 0x240) = 0;
    word_at(request, 0x244) = 0;
    return request;
}

void XB_CDECL Save_SetDriveNames(const char* names) {
    using Port = int32_t XB_CDECL();
    int32_t port = title_function<Port>(0x000d13c0)();  // Save_FirstPort (0)
    for (; port <= Save_LastPort(); ++port)
        copy_string(&title_global<char>(0x00920860) + port * 0x40, names + port * 0x40);
}

namespace {

void close_save_handles() {
    using Close = uint32_t XB_STDCALL(uint32_t);
    title_global<uint32_t>(0x00920eb0) = 0;
    const uint32_t file = title_global<uint32_t>(0x00920eac);
    if (file) {
        title_function<Close>(0x001e7b0b)(file);  // CloseHandle
        title_global<uint32_t>(0x00920eac) = 0;
    }
}

}  // namespace

void XB_CDECL Save_CloseFileHandles() {
    using Close = uint32_t XB_STDCALL(uint32_t);
    close_save_handles();
    const uint32_t find = title_global<uint32_t>(0x00920da0);
    if (find) {
        title_function<Close>(0x001e7b0b)(find);
        title_global<uint32_t>(0x00920da0) = 0;
    }
}

void XB_CDECL Save_CloseDevice() {
    using Close = uint32_t XB_STDCALL(uint32_t);
    close_save_handles();
    const uint32_t find = title_global<uint32_t>(0x00920da0);
    if (find) title_function<Close>(0x001e7b0b)(find);
    title_global<uint8_t>(0x00920da8) = 0;
    title_global<uint32_t>(0x00920da0) = 0;
    for (uint32_t i = 0; i < 4; ++i) title_global<uint32_t>(0x00920d90 + i * 4) = 0;  // overlapped
    title_global<uint8_t>(0x008c02d8) = 0;
    title_global<uint8_t>(0x008c02d4) = '?';
}

int32_t XB_CDECL Save_TranslateError(int32_t error) {
    switch (error) {
        case 0: return 0;
        case 0x55: return -17;
        case 0x70: return -3;
        case 0x48f: return -10;
        default: return -1;
    }
}

uint32_t XB_CDECL Save_Init(uint32_t value) {
    title_global<uint32_t>(0x00920afc) = value;
    Save_SetDriveNames(&title_global<char>(0x007cfa40));
    Save_SetPort(8);
    Save_SetLastResult(0);
    return Save_GetLastResult();
}

uint32_t XB_CDECL BytesToBlocks(uint32_t bytes_lo, uint32_t bytes_hi) {
    return static_cast<uint32_t>(((uint64_t{bytes_hi} << 32 | bytes_lo) + 0x3fff) >> 14);
}

int32_t XB_CDECL Save_PortToDevice(int32_t port, uint32_t* controller, uint32_t* slot) {
    if (port < 0 || port > 8) return -18;
    static const uint32_t kControllers[5] = {0, 1, 2, 3, 0};  // [4]: the original's return address
    static const uint32_t kSlots[2] = {0, 1};
    *controller = kControllers[port / 2];
    *slot = kSlots[port % 2];
    return 0;
}

void XB_CDECL File_UnlockMutex() {
    if (title_global<int32_t>(0x008bfd80) == 1)
        title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e85f5)(title_global<uint32_t>(0x00920784));  // ReleaseMutex
}

int32_t XB_REGS3 XB_CDECL File_FindDeviceForBit(uint32_t, uint32_t, uint32_t bit) {
    const int32_t count = title_global<int32_t>(0x008bfe7c);
    if (count <= 0) return -1;
    const uint32_t wanted = 1u << (bit & 0x1f);
    const uint8_t* const* const devices = title_global<const uint8_t* const*>(0x008bfe80);
    for (int32_t i = 0; i < count; ++i)
        if (*pointer_at<const uint32_t>(devices[i], 8) & wanted) return i;
    return -1;
}

namespace {

constexpr uint32_t kFilePool = 0x009207e8;
constexpr uint32_t kRequestPool = 0x00920798;

const uint8_t* pool_at(uint32_t address) { return &title_global<uint8_t>(address); }

// The file record's index for a handle, through the alias of an aliased record (+0x20 == 1).
int32_t resolve_file(int32_t handle) {
    const int32_t index = HandlePool_Resolve(pool_at(kFilePool), handle);
    int32_t target = -1;
    if (index != -1) {
        const uint8_t* const record = title_global<const uint8_t*>(0x00920820) + index * 36;
        if (record) target = word_at(record, 0x20) == 1 ? static_cast<int32_t>(word_at(record, 8)) : handle;
    }
    return HandlePool_Resolve(pool_at(kFilePool), target);
}

}  // namespace

PLATFORM_ASM_ENTRY(File_GetRecord_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(File_GetRecord) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL File_GetRecord(int32_t handle) {
    const int32_t index = resolve_file(handle);
    if (index == -1) return nullptr;
    return title_global<uint8_t*>(0x00920820) + index * 36;
}

int32_t XB_CDECL File_GetDeviceBit(int32_t handle) {
    const uint8_t* const record = File_GetRecord(handle);
    return record ? static_cast<int32_t>(word_at(record, 4)) : -1;
}

PLATFORM_ASM_ENTRY(File_GetDriverData_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(File_GetDriverData) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL File_GetDriverData(int32_t handle) {
    const int32_t index = resolve_file(handle);
    if (index == -1) return nullptr;
    return title_global<uint8_t*>(0x00920824) + title_global<uint32_t>(0x00920828) * static_cast<uint32_t>(index);
}

PLATFORM_ASM_ENTRY(File_RequestStateToStatus_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(File_RequestStateToStatus) "addl $4, %esp\n"
                   "popl %edx\n"
                   "popl %ecx\n"
                   "ret")

int32_t XB_CDECL File_RequestStateToStatus(uint32_t state) {
    static const int32_t kStatus[8] = {4, 4, 0, 1, 1, 2, 3, 3};
    return state > 7 ? -1 : kStatus[state];
}

void XB_CDECL File_ShutdownDevices() {
    using Shutdown = void XB_CDECL(uint8_t*);
    for (int32_t i = title_global<int32_t>(0x008bfe7c) - 1; i >= 0; --i) {
        uint8_t* const device = title_global<uint8_t* const*>(0x008bfe80)[i];
        const uint32_t callback = word_at(pointer_at<const uint8_t>(device, 4), 8);
        if (callback) reinterpret_cast<Shutdown*>(uintptr_t{callback})(device);
    }
}

uint32_t* XB_CDECL File_GetDeviceInfoForBit(uint32_t bit, uint32_t* out) {
    const int32_t index = File_FindDeviceForBit(0, 0, bit);
    if (index == -1) return nullptr;
    const uint8_t* const device = title_global<const uint8_t* const*>(0x008bfe80)[index];
    if (!device) return nullptr;
    out[0] = word_at(pointer_at<const uint8_t>(device, 0x14), 8);
    out[1] = word_at(pointer_at<const uint8_t>(device, 0x14), 0);
    out[2] = word_at(pointer_at<const uint8_t>(device, 0x14), 4);
    return out;
}

int32_t XB_CDECL File_GetSize(int32_t handle) {
    const int32_t index = HandlePool_Resolve(pool_at(kFilePool), handle);
    if (index == -1) return -1;
    const uint8_t* const record = title_global<const uint8_t*>(0x00920820) + index * 36;
    return record ? static_cast<int32_t>(word_at(record, 0)) : -1;
}

int32_t XB_CDECL File_GetRequestStatus(int32_t handle) {
    const int32_t index = HandlePool_Resolve(pool_at(kRequestPool), handle);
    if (index == -1) return -1;
    const uint8_t* const request = title_global<const uint8_t*>(0x00920810) + index * 0x44;
    return request ? File_RequestStateToStatus(word_at(request, 0x2c)) : -1;
}

uint32_t* XB_CDECL File_GetRequestProgress(int32_t handle, uint32_t* out) {
    const int32_t index = HandlePool_Resolve(pool_at(kRequestPool), handle);
    if (index == -1) return out;
    const uint8_t* const request = title_global<const uint8_t*>(0x00920810) + index * 0x44;
    if (!request) return out;
    out[0] = word_at(request, 0x34);
    out[1] = title_function<uint32_t XB_CDECL()>(0x000cb200)() - word_at(request, 0x18);  // Snd_UpdateTime
    return out;
}

namespace {

// Links node in before head (at the list's tail).
void list_push_back(uint8_t* head, uint8_t* node) {
    word_at(node, 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(head));
    word_at(node, 4) = word_at(head, 4);
    word_at(pointer_at<uint8_t>(head, 4), 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    word_at(head, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
}

void list_unlink(uint8_t* node) {
    word_at(pointer_at<uint8_t>(node, 4), 0) = word_at(node, 0);
    word_at(pointer_at<uint8_t>(node, 0), 4) = word_at(node, 4);
}

}  // namespace

uint8_t XB_CDECL HandlePool_Init(uint8_t* pool, uint8_t* data, int32_t count, uint32_t stride) {
    if (count <= 0 || static_cast<uint32_t>(count) > 0xfffff || stride < 0xc) return 0;
    uint32_t bits = 0;
    int32_t rest = count;
    do {
        rest >>= 1;
        ++bits;
    } while (rest);
    word_at(pool, 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    word_at(pool, 0x1c) = stride;
    word_at(pool, 0x20) = static_cast<uint32_t>(count);
    word_at(pool, 0x24) = (1u << (bits & 0x1f)) - 1;
    for (uint32_t head = 4; head <= 0x10; head += 0xc) {
        uint8_t* const list = pool + head;
        word_at(list, 8) = 0xffffffffu;
        word_at(list, 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
        word_at(list, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list));
    }
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const node = pointer_at<uint8_t>(pool, 0) + word_at(pool, 0x1c) * static_cast<uint32_t>(i);
        word_at(node, 8) = static_cast<uint32_t>(i) | 0x80000000u;
        list_push_back(pool + 4, node);
    }
    return 1;
}

int32_t XB_CDECL HandlePool_Alloc(uint8_t* pool) {
    uint8_t* const node = pointer_at<uint8_t>(pool, 4);
    if (node == pool + 4) return -1;
    list_unlink(node);
    list_push_back(pool + 0x10, node);
    uint32_t handle = (word_at(node, 8) + word_at(pool, 0x24) + 1) & 0x7fffffffu;
    word_at(node, 8) = handle;
    if (!(handle & ~(word_at(pool, 0x24) | 0x80000000u))) word_at(node, 8) = word_at(pool, 0x24) + handle + 1;
    return static_cast<int32_t>(word_at(node, 8));
}

void XB_CDECL HandlePool_Free(uint8_t* pool, int32_t handle) {
    if (handle < 0) return;
    const uint32_t index = word_at(pool, 0x24) & static_cast<uint32_t>(handle);
    if (index >= word_at(pool, 0x20)) return;
    uint8_t* const node = pointer_at<uint8_t>(pool, 0) + word_at(pool, 0x1c) * index;
    const uint32_t current = word_at(node, 8);
    if (static_cast<uint32_t>(handle) != current) return;
    word_at(node, 8) = current | 0x80000000u;
    list_unlink(node);
    list_push_back(pool + 4, node);
}

namespace {

// dlmalloc state at a heap (0x494 bytes each, 8 from 0x008ff638): max_fast (+0, bit 0 set = no fast
// chunks), fast bins (+4), top (+0x2c), bins from +0x34 (unsorted = +0x34, its fd +0x3c); bytes in use
// +0x488, peak +0x48c, next heap to try +0x490, region [+0x474, +0x478), capacity +0x484.
// Chunks: prev_size +0, size +4 (bit 0 previous in use, bit 1 not ours), fd +8, bk +0xc.
uint8_t* chunk_ptr(const uint8_t* base, uint32_t offset) { return pointer_at<uint8_t>(base, offset); }

void set_ptr(uint8_t* base, uint32_t offset, const uint8_t* value) {
    word_at(base, offset) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
}

void chunk_unlink(uint8_t* chunk) {
    uint8_t* const fd = chunk_ptr(chunk, 8);
    uint8_t* const bk = chunk_ptr(chunk, 0xc);
    set_ptr(fd, 0xc, bk);
    set_ptr(bk, 8, fd);
}

// 0x008ff638 when the system heap flag (0x008ffabc) is set, else 0.
uint32_t system_heap() { return title_global<uint32_t>(0x008ffabc) ? 0x008ff638u : 0u; }

void debug_break() { title_function<void XB_CDECL()>(0x00042460)(); }  // Sys_DebugBreak

uint8_t* heap_ptr(uint32_t heap) { return reinterpret_cast<uint8_t*>(uintptr_t{heap}); }

}  // namespace

uint8_t* XB_REGS1 XB_CDECL Heap_InitBins(uint8_t* heap) {
    uint8_t* const first = heap + 0x34;
    uint8_t* bin = first;
    for (uint32_t i = 0x7f; i; --i) {
        set_ptr(bin, 0xc, bin);
        set_ptr(bin, 8, bin);
        bin += 8;
    }
    word_at(heap, 0x448) = 0;
    word_at(heap, 0x454) = 0;
    word_at(heap, 0x44c) = 0x20000;
    word_at(heap, 0x444) = 0x20000;
    word_at(heap, 0) = (word_at(heap, 0) & 2) | 0x49;
    set_ptr(heap, 0x2c, first);
    word_at(heap, 0x45c) = 0x2000;
    return heap;
}

void XB_CDECL Heap_EnsureInit(uint8_t* heap) {
    uint32_t max_fast = word_at(heap, 0);
    if (!max_fast) {
        Heap_InitBins(heap);
        return;
    }
    max_fast |= 1;
    word_at(heap, 0) = max_fast;
    uint8_t* const last_bin = heap + (max_fast >> 3) * 4 - 4;
    uint8_t* bin = heap + 4;
    uint8_t* current;
    do {
        uint8_t* p = chunk_ptr(bin, 0);
        if (p) {
            word_at(bin, 0) = 0;
            do {
                const uint32_t head = word_at(p, 4);
                uint8_t* const next = chunk_ptr(p, 8);
                uint32_t size = head & ~1u;
                uint8_t* const next_chunk = p + size;
                const uint32_t next_size = word_at(next_chunk, 4) & ~3u;
                if (!(head & 1)) {
                    const uint32_t prev_size = word_at(p, 0);
                    size += prev_size;
                    p -= prev_size;
                    chunk_unlink(p);
                }
                if (next_chunk != chunk_ptr(heap, 0x2c)) {
                    const uint32_t next_in_use = word_at(next_chunk + next_size, 4) & 1;
                    word_at(next_chunk, 4) = next_size;
                    if (!next_in_use) {
                        size += next_size;
                        chunk_unlink(next_chunk);
                    }
                    uint8_t* const unsorted = heap + 0x34;
                    uint8_t* const first = chunk_ptr(heap, 0x3c);
                    set_ptr(unsorted, 8, p);
                    set_ptr(first, 0xc, p);
                    word_at(p, 4) = size | 1;
                    set_ptr(p, 0xc, unsorted);
                    set_ptr(p, 8, first);
                    word_at(p + size, 0) = size;
                } else {
                    word_at(p, 4) = (next_size + size) | 1;
                    set_ptr(heap, 0x2c, p);
                }
                p = next;
            } while (p);
        }
        current = bin;
        bin += 4;
    } while (current != last_bin);
}

void XB_CDECL Heap_FreeBlock(uint8_t* mem) {
    uint8_t* const heap = heap_ptr(Heap_GetCurrent());
    if (!mem) return;
    uint8_t* p = mem - 8;
    const uint32_t head = word_at(p, 4);
    uint32_t size = head & ~3u;
    const uint32_t max_fast = word_at(heap, 0);
    if (size <= max_fast) {
        word_at(heap, 0) = max_fast & ~1u;
        uint8_t* const bin = heap + (size >> 3) * 4 - 4;
        word_at(p, 8) = word_at(bin, 0);
        set_ptr(bin, 0, p);
        return;
    }
    if (head & 2) return;
    uint8_t* const next_chunk = p + size;
    const uint32_t next_size = word_at(next_chunk, 4) & ~3u;
    if (!(head & 1)) {
        const uint32_t prev_size = word_at(p, 0);
        size += prev_size;
        p -= prev_size;
        chunk_unlink(p);
    }
    if (next_chunk != chunk_ptr(heap, 0x2c)) {
        const uint32_t next_in_use = word_at(next_chunk + next_size, 4) & 1;
        word_at(next_chunk, 4) = next_size;
        if (!next_in_use) {
            chunk_unlink(next_chunk);
            size += next_size;
        }
        uint8_t* const first = chunk_ptr(heap, 0x3c);
        uint8_t* const unsorted = heap + 0x34;
        set_ptr(p, 8, first);
        set_ptr(p, 0xc, unsorted);
        set_ptr(unsorted, 8, p);
        set_ptr(first, 0xc, p);
        word_at(p, 4) = size | 1;
        word_at(p + size, 0) = size;
    } else {
        size += next_size;
        word_at(p, 4) = size | 1;
        set_ptr(heap, 0x2c, p);
    }
    if (size >= 0x10000 && !(word_at(heap, 0) & 1)) Heap_EnsureInit(heap);
}

void XB_CDECL Heap_Free(uint8_t* mem) {
    const uint32_t ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem));
    if (title_global<uint32_t>(0x00901ae8) == system_heap()) {
        for (int32_t i = 0; i < title_global<int32_t>(0x008ff630); ++i) {
            const uint32_t start = title_global<uint32_t>(0x008ff5b0 + i * 8);
            if (ptr < start || ptr >= title_global<uint32_t>(0x008ff5b4 + i * 8) + start) continue;
            if (title_global<uint8_t>(0x00901aec)) debug_break();
        }
    }
    uint8_t* const heap = current_heap();
    if (!mem) return;
    word_at(heap, 0x488) -= word_at(mem - 4, 0) & ~3u;
    Heap_FreeBlock(mem);
}

void XB_CDECL Heap_FreeIn(uint32_t heap, uint8_t* mem) {
    const uint32_t saved = title_global<uint32_t>(0x00901ae8);
    title_global<uint32_t>(0x00901ae8) = heap;
    Heap_Free(mem);
    title_global<uint32_t>(0x00901ae8) = saved;
}

uint8_t XB_CDECL Heap_FreeFromOwner(uint8_t* heap, uint8_t* mem) {
    const uint32_t ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem));
    const uint32_t flag = title_global<uint32_t>(0x008ffabc);
    for (;;) {
        if (ptr >= word_at(heap, 0x474) && ptr < word_at(heap, 0x478)) {
            Heap_FreeIn(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(heap)), mem);
            return 1;
        }
        const uint32_t next = word_at(heap, 0x490);
        if (!next || next == (flag ? 0x008ff638u : 0u)) return 0;
        heap = heap_ptr(next);
    }
}

void XB_CDECL Z_Free(uint8_t* mem) {
    if (!mem) return;
    const uint32_t ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem));
    for (int32_t i = 0; i < title_global<int32_t>(0x008ff5a8); ++i) {
        if (Heap_FreeFromOwner(heap_ptr(title_global<uint32_t>(0x008ff4a8 + i * 4)), mem)) return;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        const uint8_t* const heap = heap_ptr(0x008ff638 + i * 0x494);
        if (word_at(heap, 0x484) && ptr >= word_at(heap, 0x474) && ptr < word_at(heap, 0x478)) {
            Heap_FreeIn(0x008ff638 + i * 0x494, mem);
            return;
        }
    }
    const uint8_t* const heap = current_heap();
    if (ptr >= word_at(heap, 0x474) && ptr < word_at(heap, 0x478)) Heap_Free(mem);
}

void XB_THISCALL Heap_FreeIn2(uint32_t heap, uint8_t* mem) { Heap_FreeIn(heap, mem); }

namespace {

// Heap_AllocAligned / Z_Malloc after the allocation: count the chunk in the current heap (+0x488,
// peak +0x48c), else retry in the next heap (+0x490) and, from the system heap, in each owner heap.
template <typename Retry>
uint8_t* account_or_retry(uint8_t* result, Retry retry) {
    uint8_t* heap = current_heap();
    if (result) {
        word_at(heap, 0x488) += word_at(result - 4, 0) & ~3u;
        heap = current_heap();
        const uint32_t used = word_at(heap, 0x488);
        if (word_at(heap, 0x48c) < used) word_at(heap, 0x48c) = used;
        return result;
    }
    const uint32_t saved = title_global<uint32_t>(0x00901ae8);
    const uint32_t next = word_at(heap, 0x490);
    if (next) {
        title_global<uint32_t>(0x00901ae8) = next;
        result = retry();
        title_global<uint32_t>(0x00901ae8) = saved;
    }
    if (result || saved != system_heap()) return result;
    for (int32_t i = 0; i < title_global<int32_t>(0x008ff5a8); ++i) {
        title_global<uint32_t>(0x00901ae8) = title_global<uint32_t>(0x008ff4a8 + i * 4);
        result = retry();
        title_global<uint32_t>(0x00901ae8) = saved;
        if (result) return result;
    }
    if (title_global<uint8_t>(0x00901aec)) debug_break();
    return result;
}

}  // namespace

uint8_t* XB_CDECL Heap_AllocAligned(uint32_t align, uint32_t size) {
    if (!title_global<uint32_t>(0x00901ad8)) Heap_InitDefaultHeaps();
    uint8_t* const result = title_function<uint8_t* XB_CDECL(uint32_t, uint32_t)>(0x00043b70)(align, size);  // Heap_MallocAligned
    return account_or_retry(result, [=] { return Heap_AllocAligned(align, size); });
}

uint8_t* XB_CDECL Heap_Alloc16(uint32_t size) { return Heap_AllocAligned(0x10, size); }

uint8_t* XB_CDECL Heap_Alloc16_2(uint32_t size) { return Heap_AllocAligned(0x10, size); }

uint8_t* XB_CDECL Z_Malloc(uint32_t size) {
    if (!size) return nullptr;
    if (!title_global<uint32_t>(0x00901ad8)) Heap_InitDefaultHeaps();
    uint8_t* const result = title_function<uint8_t* XB_CDECL(uint32_t)>(0x00043500)(size);  // Heap_Malloc
    return account_or_retry(result, [=] { return Z_Malloc(size); });
}

uint8_t* XB_THISCALL Heap_CreateFromZone(uint8_t* heap, uint32_t size) {
    uint8_t* const memory = Z_Malloc(size);
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory));
    Heap_Init(heap, base, base + size, 0);
    return heap;
}

uint8_t* XB_THISCALL Heap_AllocFromHeap(uint8_t* heap, uint32_t size, uint32_t align) {
    if (word_at(heap, 0x488) + size > word_at(heap, 0x484)) return nullptr;
    const uint32_t saved = Heap_SetCurrent(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(heap)));
    uint8_t* const result = Heap_AllocAligned(align, size);
    Heap_SetCurrent(saved);
    return result;
}

namespace {

// The pool getters: Mem_AllocPhysicalPools (0x00043fb0, XPhysicalAlloc) when the value is still 0.
uint32_t pool_value(uint32_t address) {
    if (!title_global<uint32_t>(address)) title_function<void XB_CDECL()>(0x00043fb0)();
    return title_global<uint32_t>(address);
}

}  // namespace

uint32_t XB_CDECL Mem_GetPhysicalPoolEnd() { return pool_value(0x00901af4); }
uint32_t XB_CDECL Mem_GetPhysicalPoolStart() { return pool_value(0x00901af0); }
uint32_t XB_CDECL Mem_GetDebugPoolEnd() { return pool_value(0x00901b00); }
uint32_t XB_CDECL Mem_GetDebugPoolStart() { return pool_value(0x00901afc); }
uint32_t XB_CDECL Mem_GetWCPoolEnd() { return pool_value(0x00901b0c); }
uint32_t XB_CDECL Mem_GetWCPoolStart() { return pool_value(0x00901b08); }

uint32_t XB_CDECL Heap_InitDefaultHeaps() {
    if (title_global<uint32_t>(0x00901ad8)) return 1;
    for (uint32_t i = 0; i < 0x928; ++i) title_global<uint32_t>(0x008ff638 + i * 4) = 0;
    title_global<uint32_t>(0x00901ad8) = (Mem_GetPhysicalPoolStart() + 0x1f) & ~0x1fu;
    const uint32_t end = Mem_GetPhysicalPoolEnd() & ~0x1fu;
    title_global<uint32_t>(0x00901adc) = end;
    const uint32_t start = title_global<uint32_t>(0x00901ad8);
    if (start < end) Heap_Init(heap_ptr(0x008ff638), start, end, 0);
    const uint32_t wc_start = Mem_GetWCPoolStart();
    if (wc_start < Mem_GetWCPoolEnd()) {
        const uint32_t wc_end = Mem_GetWCPoolEnd();
        Heap_Init(heap_ptr(0x008fff60), Mem_GetWCPoolStart(), wc_end, 0);
    }
    if (Mem_GetDebugPoolStart() != 0xffffffffu) {
        title_global<uint32_t>(0x00901ae0) = (Mem_GetDebugPoolStart() + 0x1f) & ~0x1fu;
        const uint32_t debug_end = Mem_GetDebugPoolEnd() & ~0x1fu;
        title_global<uint32_t>(0x00901ae4) = debug_end;
        const uint32_t debug_start = title_global<uint32_t>(0x00901ae0);
        if (debug_start < debug_end) {
            Heap_Init(heap_ptr(0x008ffacc), debug_start, debug_end, 0);
            title_function<void XB_CDECL(uint32_t, uint32_t, ...)>(0x00044160)(  // Mem_DebugPrintf
                0, 0x007b324c, title_global<uint32_t>(0x008fff50));             // "create debug heap size=%i\n"
        }
        title_global<uint32_t>(0x008ffac8) = 0x008ffacc;
    }
    title_global<uint32_t>(0x00901ae8) = 0x008ff638;
    return 1;
}

namespace {

uint8_t* bin_at(uint8_t* heap, uint32_t index) { return heap + 0x2c + index * 8; }

uint32_t& bin_map(uint8_t* heap, uint32_t block) { return word_at(heap, 0x434 + block * 4); }

uint32_t large_bin_index(uint32_t size) {
    if ((size >> 6) <= 0x20) return (size >> 6) + 0x38;
    if ((size >> 9) <= 0x14) return (size >> 9) + 0x5b;
    if ((size >> 12) <= 0xa) return (size >> 12) + 0x6e;
    if ((size >> 15) <= 4) return (size >> 15) + 0x77;
    if ((size >> 18) <= 2) return (size >> 18) + 0x7c;
    return 0x7e;
}

void set_errno_nomem() { *title_function<int32_t* XB_CDECL()>(0x001f0ac5)() = 12; }  // _errno, ENOMEM

// Splits nb bytes off victim (size bytes); the rest becomes the only unsorted chunk.
uint8_t* split_to_unsorted(uint8_t* heap, uint8_t* victim, uint32_t size, uint32_t nb, bool last_remainder) {
    uint8_t* const unsorted = heap + 0x34;
    uint8_t* const remainder = victim + nb;
    const uint32_t remainder_size = size - nb;
    set_ptr(heap, 0x3c, remainder);
    set_ptr(heap, 0x40, remainder);
    if (last_remainder) set_ptr(heap, 0x30, remainder);
    set_ptr(remainder, 8, unsorted);
    set_ptr(remainder, 0xc, unsorted);
    word_at(victim, 4) = nb | 1;
    word_at(remainder, 4) = remainder_size | 1;
    word_at(remainder + remainder_size, 0) = remainder_size;
    return victim + 8;
}

uint8_t* use_whole(uint8_t* victim, uint32_t size) {
    word_at(victim + size, 4) |= 1;
    return victim + 8;
}

}  // namespace

uint8_t* XB_REGS1 XB_CDECL Heap_SysAlloc(uint8_t* heap, uint32_t nb) {
    uint8_t* const old_top = chunk_ptr(heap, 0x2c);
    const uint32_t old_head = word_at(old_top, 4);
    const uint32_t page_mask = word_at(heap, 0x45c) - 1;
    const uint32_t old_size = old_head & ~3u;
    uint32_t size = word_at(heap, 0x448) + nb + 0x10;
    if (!(heap[0] & 2)) size -= old_size;
    size = (size + page_mask) & ~page_mask;
    if (static_cast<int32_t>(size) <= 0) {
        set_errno_nomem();
        return nullptr;
    }
    const uint32_t brk = Heap_Sbrk(size);
    if (brk == 0xffffffffu) {
        set_errno_nomem();
        return nullptr;
    }
    word_at(heap, 0x464) += size;
    if (brk == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(old_top)) + old_size) {
        word_at(old_top, 4) = (size + old_size) | 1;
    } else {
        uint32_t correction = 0;
        uint32_t aligned_brk = brk;
        uint32_t snd_brk;
        bool have_second = true;
        if (!(heap[0] & 2)) {
            const uint32_t front = brk & 7;
            if (front) {
                correction = 8 - front;
                aligned_brk = brk + correction;
            }
            correction += old_size;
            const uint32_t end = correction + size + brk;
            correction += ((end + page_mask) & ~page_mask) - end;
            snd_brk = Heap_Sbrk(correction);
            if (snd_brk == 0xffffffffu) {
                correction = 0;
                snd_brk = Heap_Sbrk(0);
                have_second = snd_brk != 0xffffffffu;
            }
        } else {
            snd_brk = Heap_Sbrk(0);
            have_second = snd_brk != 0xffffffffu;
        }
        if (have_second) {
            uint8_t* const top = heap_ptr(aligned_brk);
            set_ptr(heap, 0x2c, top);
            word_at(top, 4) = (correction - aligned_brk + snd_brk) | 1;
            word_at(heap, 0x464) += correction;
            if (old_size) {
                const uint32_t fence = (old_size - 0xc) & ~7u;
                word_at(old_top, 4) = fence | 1;
                word_at(old_top + fence, 4) = 5;
                word_at(old_top + fence, 8) = 5;
                if (fence >= 0x10) Heap_FreeBlock(old_top + 8);
            }
        }
    }
    uint32_t sum = word_at(heap, 0x464);
    if (sum > word_at(heap, 0x468)) word_at(heap, 0x468) = sum;
    sum += word_at(heap, 0x460);
    if (sum > word_at(heap, 0x470)) word_at(heap, 0x470) = sum;
    uint8_t* const top = chunk_ptr(heap, 0x2c);
    const uint32_t top_size = word_at(top, 4) & ~3u;
    if (top_size < nb + 0x10) {
        set_errno_nomem();
        return nullptr;
    }
    uint8_t* const remainder = top + nb;
    set_ptr(heap, 0x2c, remainder);
    word_at(top, 4) = nb | 1;
    word_at(remainder, 4) = (top_size - nb) | 1;
    return top + 8;
}

uint8_t* XB_CDECL Heap_Malloc(uint32_t bytes) {
    uint8_t* const heap = heap_ptr(Heap_GetCurrent());
    if (bytes >= 0xffffffe0u) {
        set_errno_nomem();
        return nullptr;
    }
    const uint32_t padded = bytes + 0xb;
    const uint32_t nb = padded < 0x10 ? 0x10 : padded & ~7u;
    const uint32_t max_fast = word_at(heap, 0);
    if (nb <= max_fast) {
        uint8_t* const fast_bin = heap + (nb >> 3) * 4 - 4;
        uint8_t* const victim = chunk_ptr(fast_bin, 0);
        if (victim) {
            word_at(fast_bin, 0) = word_at(victim, 8);
            return victim + 8;
        }
    }
    uint32_t index;
    if (nb < 0x200) {
        index = nb >> 3;
        uint8_t* const bin = bin_at(heap, index);
        uint8_t* const victim = chunk_ptr(bin, 0xc);
        if (victim != bin) {
            if (!victim) {
                Heap_EnsureInit(heap);
            } else {
                uint8_t* const back = chunk_ptr(victim, 0xc);
                word_at(victim + nb, 4) |= 1;
                set_ptr(bin, 0xc, back);
                set_ptr(back, 8, bin);
                return victim + 8;
            }
        }
    } else {
        index = large_bin_index(nb);
        if (!(max_fast & 1)) Heap_EnsureInit(heap);
    }
    uint8_t* const unsorted = heap + 0x34;
    for (;;) {
        uint8_t* victim;
        while ((victim = chunk_ptr(heap, 0x40)) != unsorted) {
            uint32_t size = word_at(victim, 4) & ~3u;
            uint8_t* back = chunk_ptr(victim, 0xc);
            if (nb < 0x200 && back == unsorted && victim == chunk_ptr(heap, 0x30) && size > nb + 0x10)
                return split_to_unsorted(heap, victim, size, nb, true);
            set_ptr(heap, 0x40, back);
            set_ptr(back, 8, unsorted);
            if (size == nb) return use_whole(victim, size);
            uint32_t victim_index;
            uint8_t* forward;
            if (size < 0x200) {
                victim_index = size >> 3;
                back = bin_at(heap, victim_index);
                forward = chunk_ptr(back, 8);
            } else {
                victim_index = large_bin_index(size);
                back = bin_at(heap, victim_index);
                forward = chunk_ptr(back, 8);
                if (forward != back) {
                    size |= 1;
                    uint8_t* const last = chunk_ptr(back, 0xc);
                    if (size <= word_at(last, 4)) {
                        forward = back;
                        back = last;
                    } else {
                        while (size < word_at(forward, 4)) forward = chunk_ptr(forward, 8);
                        back = chunk_ptr(forward, 0xc);
                    }
                }
            }
            bin_map(heap, victim_index >> 5) |= 1u << (victim_index & 0x1f);
            set_ptr(victim, 0xc, back);
            set_ptr(victim, 8, forward);
            set_ptr(forward, 0xc, victim);
            set_ptr(back, 8, victim);
        }
        if (nb >= 0x200) {
            uint8_t* const bin = bin_at(heap, index);
            victim = chunk_ptr(bin, 0xc);
            if (victim != bin && word_at(chunk_ptr(bin, 8), 4) >= nb) {
                uint32_t size = word_at(victim, 4) & ~3u;
                while (size < nb) {
                    victim = chunk_ptr(victim, 0xc);
                    size = word_at(victim, 4) & ~3u;
                }
                chunk_unlink(victim);
                if (size - nb < 0x10) return use_whole(victim, size);
                return split_to_unsorted(heap, victim, size, nb, false);
            }
        }
        ++index;
        uint8_t* bin = bin_at(heap, index);
        uint32_t block = index >> 5;
        uint32_t map = bin_map(heap, block);
        uint32_t bit = 1u << (index & 0x1f);
        bool use_top = false;
        for (;;) {
            if (bit > map || !bit) {
                do {
                    if (++block >= 4) break;
                    map = bin_map(heap, block);
                } while (!map);
                if (block >= 4) {
                    use_top = true;
                    break;
                }
                bin = bin_at(heap, block << 5);
                bit = 1;
            }
            while (!(map & bit)) {
                bit <<= 1;
                bin += 8;
            }
            victim = chunk_ptr(bin, 0xc);
            if (victim == bin) {
                map &= ~bit;
                bin_map(heap, block) = map;
                bin += 8;
                bit <<= 1;
                continue;
            }
            const uint32_t size = word_at(victim, 4) & ~3u;
            uint8_t* const back = chunk_ptr(victim, 0xc);
            set_ptr(bin, 0xc, back);
            set_ptr(back, 8, bin);
            if (size - nb < 0x10) return use_whole(victim, size);
            return split_to_unsorted(heap, victim, size, nb, nb < 0x200);
        }
        if (use_top) {
            victim = chunk_ptr(heap, 0x2c);
            const uint32_t size = word_at(victim, 4) & ~3u;
            if (size >= nb + 0x10) {
                uint8_t* const remainder = victim + nb;
                set_ptr(heap, 0x2c, remainder);
                word_at(victim, 4) = nb | 1;
                word_at(remainder, 4) = (size - nb) | 1;
                return victim + 8;
            }
            if (word_at(heap, 0) & 1) return Heap_SysAlloc(heap, nb);
            Heap_EnsureInit(heap);
            index = nb >> 3;
        }
    }
}

uint8_t* XB_CDECL Heap_MallocAligned(uint32_t align, uint32_t bytes) {
    if (align <= 8) return Heap_Malloc(bytes);
    if (align < 0x10) align = 0x10;
    if (align & (align - 1)) {
        uint32_t power = 0x10;
        if (align > power) {
            do power <<= 1;
            while (power < align);
        }
        align = power;
    }
    if (bytes >= 0xffffffe0u) {
        set_errno_nomem();
        return nullptr;
    }
    const uint32_t padded = bytes + 0xb;
    const uint32_t nb = padded < 0x10 ? 0x10 : padded & ~7u;
    uint8_t* const mem = Heap_Malloc(align + nb + 0x10);
    if (!mem) return nullptr;
    uint8_t* p = mem - 8;
    const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem));
    if (address % align) {
        uint8_t* aligned = heap_ptr(((address + align - 1) & (0u - align)) - 8);
        if (static_cast<uint32_t>(aligned - p) < 0x10) aligned += align;
        const uint32_t lead = static_cast<uint32_t>(aligned - p);
        const uint32_t head = word_at(p, 4);
        const uint32_t new_size = (head & ~3u) - lead;
        if (head & 2) {
            const uint32_t prev = word_at(p, 0);
            word_at(aligned, 4) = new_size | 2;
            word_at(aligned, 0) = prev + lead;
            return aligned + 8;
        }
        word_at(aligned, 4) = new_size | 1;
        word_at(aligned + new_size, 4) |= 1;
        word_at(p, 4) = (word_at(p, 4) & 1) | lead;
        Heap_FreeBlock(p + 8);
        p = aligned;
    }
    const uint32_t head = word_at(p, 4);
    if (!(head & 2)) {
        const uint32_t size = head & ~3u;
        if (size > nb + 0x10) {
            word_at(p + nb, 4) = (size - nb) | 1;
            word_at(p, 4) = (word_at(p, 4) & 1) | nb;
            Heap_FreeBlock(p + nb + 8);
        }
    }
    return p + 8;
}

uint32_t* XB_CDECL Heap_GetMallInfo(uint32_t* out) {
    uint8_t* const heap = heap_ptr(Heap_GetCurrent());
    if (!chunk_ptr(heap, 0x2c)) Heap_EnsureInit(heap);
    const uint32_t top_size = word_at(chunk_ptr(heap, 0x2c), 4) & ~3u;
    uint32_t avail = top_size;
    uint32_t fast_blocks = 0;
    uint32_t fast_avail = 0;
    uint32_t blocks = 1;
    for (uint32_t i = 0; i < 10; ++i) {
        for (const uint8_t* p = chunk_ptr(heap, 4 + i * 4); p; p = chunk_ptr(p, 8)) {
            ++fast_blocks;
            fast_avail += word_at(p, 4) & ~3u;
        }
    }
    avail += fast_avail;
    for (uint32_t i = 0; i < 0x7f; ++i) {
        uint8_t* const bin = heap + 0x34 + i * 8;
        for (const uint8_t* p = chunk_ptr(bin, 0xc); p != bin; p = chunk_ptr(p, 0xc)) {
            ++blocks;
            avail += word_at(p, 4) & ~3u;
        }
    }
    const uint32_t arena = word_at(heap, 0x464);
    const uint32_t info[10] = {arena,
                               blocks,
                               fast_blocks,
                               word_at(heap, 0x450),
                               word_at(heap, 0x460),
                               word_at(heap, 0x470),
                               fast_avail,
                               arena - avail,
                               avail,
                               top_size};
    for (uint32_t i = 0; i < 10; ++i) out[i] = info[i];
    return out;
}

void XB_CDECL Heap_MallInfo() {
    uint32_t info[10];
    Heap_GetMallInfo(info);
}

uint8_t* XB_THISCALL SmallHeap_InitPool(uint8_t* pool, uint32_t block_size, uint32_t count, uint32_t tag,
                                        uint32_t align) {
    const uint32_t total = block_size * count;
    word_at(pool, 4) = count;
    word_at(pool, 8) = count;
    word_at(pool, 0x10) = align;
    word_at(pool, 0) = block_size;
    word_at(pool, 0xc) = tag;
    word_at(pool, 0x14) = 0;
    word_at(pool, 0x18) = 0;
    word_at(pool, 0x20) = 0;
    word_at(pool, 0x24) = 0;
    uint8_t* block = Heap_AllocAligned(word_at(pool, 0x10), total);
    set_ptr(pool, 0x1c, block + total);
    const uint32_t last = word_at(pool, 4) - 1;
    set_ptr(pool, 0x18, block);
    if (static_cast<int32_t>(last) < 0) return pool;
    for (uint32_t left = last + 1; left; --left) {
        uint8_t* node = nullptr;
        if (block) {
            const uint16_t tag16 = static_cast<uint16_t>(word_at(pool, 0xc));
            std::memcpy(block, &tag16, 2);
            block[2] = 0;
            block[3] = 0;
            word_at(block, 4) = 0;
            node = block;
        }
        node += 4;
        word_at(node, 0) = word_at(pool, 0x24);
        set_ptr(pool, 0x24, node);
        block += block_size;
    }
    return pool;
}

uint8_t* XB_THISCALL SmallHeap_Init(uint8_t* heap, const uint32_t* config, uint32_t no_pools, uint32_t no_fallback) {
    word_at(heap, 0) = 0;
    word_at(heap, 0x24) = 0;
    heap[0x38] = static_cast<uint8_t>(no_pools);
    heap[0x39] = 0;
    heap[0x3a] = static_cast<uint8_t>(no_fallback);
    uint32_t index = 0;
    for (const uint32_t* entry = config + 1; entry != config + 1 + config[0] * 3; entry += 3, ++index) {
        uint8_t* pool = Z_Malloc(0x28);
        if (pool) pool = SmallHeap_InitPool(pool, entry[0], entry[2], index & 0xffff, entry[1]);
        const int32_t pools = static_cast<int32_t>(word_at(heap, 0));
        if (pools < 8) {
            set_ptr(heap, 4 + pools * 4, pool);
            ++word_at(heap, 0);
        }
        const int32_t sizes = static_cast<int32_t>(word_at(heap, 0x24));
        if (sizes < 8) {
            std::memcpy(heap + 0x28 + sizes * 2, entry, 2);
            ++word_at(heap, 0x24);
        }
    }
    return heap;
}

void XB_THISCALL SmallHeap_Destroy(uint8_t* heap) {
    for (uint8_t* slot = heap + 4; slot != heap + 4 + word_at(heap, 0) * 4; slot += 4) {
        uint8_t* const pool = chunk_ptr(slot, 0);
        if (!pool) continue;
        Z_Free(chunk_ptr(pool, 0x18));
        Z_Free(pool);
    }
}

uint8_t* XB_THISCALL SmallHeap_Alloc(uint8_t* heap, uint32_t size, uint32_t no_pool) {
    if (!static_cast<uint8_t>(no_pool) && !heap[0x39]) {
        for (int32_t i = 0; i < static_cast<int32_t>(word_at(heap, 0x24)); ++i) {
            int16_t limit;
            std::memcpy(&limit, heap + 0x28 + i * 2, 2);
            if (static_cast<int32_t>(size & 0xffff) > limit) continue;
            uint8_t* const pool = chunk_ptr(heap, 4 + i * 4);
            if (!word_at(pool, 8)) continue;
            uint8_t* const node = chunk_ptr(pool, 0x24);
            if (!node) continue;
            word_at(pool, 0x24) = word_at(node, 0);
            uint8_t* const block = node - 4;
            const uint32_t free_count = word_at(pool, 8) - 1;
            word_at(pool, 8) = free_count;
            uint32_t used = word_at(pool, 4) - free_count;
            const uint32_t peak = word_at(pool, 0x14);
            if (peak > used) used = peak;
            word_at(pool, 0x14) = used;
            if (block) return block;
        }
    }
    if (heap[0x3a]) return nullptr;
    return Heap_AllocAligned(0x10, size);
}

void XB_THISCALL SmallHeap_Free(uint8_t* heap, uint8_t* mem) {
    if (!mem) return;
    const uint32_t ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem));
    uint8_t* const end = heap + 4 + word_at(heap, 0) * 4;
    for (uint8_t* slot = heap + 4; slot != end; slot += 4) {
        uint8_t* const pool = chunk_ptr(slot, 0);
        if (ptr < word_at(pool, 0x18) || ptr > word_at(pool, 0x1c)) continue;
        ++word_at(pool, 8);
        uint8_t* const node = mem + 4;
        word_at(node, 0) = word_at(pool, 0x24);
        set_ptr(pool, 0x24, node);
        return;
    }
    Z_Free(mem);
}

namespace {

constexpr uint32_t kDevicePool = 0x009207c0;

bool file_threaded() { return title_global<int32_t>(0x008bfd80) == 1; }

void file_lock() {
    title_function<uint32_t XB_STDCALL(uint32_t, uint32_t)>(0x001e86ab)(title_global<uint32_t>(0x00920784),  // WaitForSingleObject
                                                                       0xffffffffu);
}

void file_release() { title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e85f5)(title_global<uint32_t>(0x00920784)); }  // ReleaseMutex

uint8_t* file_record(int32_t handle) {
    const int32_t index = HandlePool_Resolve(pool_at(kFilePool), handle);
    return index == -1 ? nullptr : title_global<uint8_t*>(0x00920820) + index * 36;
}

uint8_t* file_request(int32_t handle) {
    const int32_t index = HandlePool_Resolve(pool_at(kRequestPool), handle);
    return index == -1 ? nullptr : title_global<uint8_t*>(0x00920810) + index * 0x44;
}

uint8_t* file_device(uint32_t handle) {
    const int32_t index = HandlePool_Resolve(pool_at(kDevicePool), static_cast<int32_t>(handle));
    return index == -1 ? nullptr : title_global<uint8_t*>(0x0092081c) + index * 16;
}

void pump_requests() { title_function<void XB_CDECL()>(0x000ca690)(); }  // File_PumpRequests

template <typename F>
F* driver_op(const uint8_t* driver, uint32_t table, uint32_t slot) {
    return reinterpret_cast<F*>(uintptr_t{word_at(pointer_at<const uint8_t>(driver, table), slot)});
}

}  // namespace

void XB_CDECL File_SetDevicePriority(uint32_t device, uint32_t priority) {
    uint8_t* const record = file_device(device ? device : title_global<uint32_t>(0x008bfd6c));
    if (!record) return;
    if (file_threaded()) file_lock();
    word_at(record, 0xc) = priority;
    int32_t next = HandlePool_First(pool_at(kRequestPool));
    while (next != -1) {
        const int32_t handle = next;
        next = HandlePool_Next(pool_at(kRequestPool), handle);
        uint8_t* const request = file_request(handle);
        if (word_at(request, 8) == device)
            word_at(request, 0x20) = (word_at(request, 0x20) & 0xff) | (priority << 8);
    }
    if (file_threaded()) file_release();
}

int32_t XB_CDECL File_Open(uint32_t bit, const char* path, uint32_t mode, uint32_t* size) {
    char name[0x100];
    const int32_t device = File_FindDeviceForBit(0, 0, bit);
    if (device == -1) return -1;
    const uint8_t* const driver = title_global<const uint8_t* const*>(0x008bfe80)[device];
    if (!driver) return -1;
    using Resolve = int32_t XB_CDECL(uint32_t, const char*, char*, uint32_t);
    if (driver_op<Resolve>(driver, 8, 4)(bit, path, name, 0x100)) return -1;
    if (file_threaded()) file_lock();
    const int32_t handle = HandlePool_Alloc(&title_global<uint8_t>(kFilePool));
    if (handle == -1) {
        if (file_threaded()) file_release();
        return -1;
    }
    uint8_t* const record = file_record(handle);
    word_at(record, 0x20) = 0;
    word_at(record, 0x10) = 0;
    word_at(record, 0xc) = mode;
    word_at(record, 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(driver));
    word_at(record, 4) = bit;
    uint8_t* const data = File_GetDriverData(handle);
    const uint32_t hint = size ? *size : 0;
    using OpenOp = int32_t XB_CDECL(uint8_t*, const char*, uint32_t, uint32_t);
    using SizeOp = int32_t XB_CDECL(uint8_t*, uint32_t*);
    uint32_t info[2] = {0, 0};
    if (driver_op<OpenOp>(driver, 0xc, 8)(data, name, mode, hint) || driver_op<SizeOp>(driver, 0xc, 0x10)(data, info)) {
        if (file_threaded()) file_release();
        HandlePool_Free(&title_global<uint8_t>(kFilePool), handle);
        return -1;
    }
    const uint32_t file_size = info[1];
    if (size) *size = file_size;
    word_at(record, 0) = file_size;
    File_UnlockMutex();
    return handle;
}

int32_t XB_CDECL File_OpenRead(uint32_t bit, const char* path) { return File_Open(bit, path, 1, nullptr); }

int32_t XB_CDECL File_OpenReadEx(uint32_t bit, const char* path, uint32_t* size) { return File_Open(bit, path, 1, size); }

void XB_CDECL File_SetRequestPriority(int32_t handle, uint32_t priority) {
    if (file_threaded()) file_lock();
    uint8_t* const request = file_request(handle);
    if (request) word_at(request, 0x20) = (word_at(request, 0x20) & 0xff00) | priority;
    if (file_threaded()) file_release();
}

int32_t XB_CDECL File_QueueRead(const uint32_t* desc) {
    if (!file_record(static_cast<int32_t>(desc[0]))) return -1;
    const uint8_t* const device = file_device(desc[1] ? desc[1] : title_global<uint32_t>(0x008bfd6c));
    if (!device) return -1;
    if (file_threaded()) file_lock();
    const int32_t handle = HandlePool_Alloc(&title_global<uint8_t>(kRequestPool));
    if (handle == -1) {
        if (file_threaded()) file_release();
        return -1;
    }
    uint8_t* const request = file_request(handle);
    word_at(request, 4) = desc[3];
    word_at(request, 0xc) = desc[0];
    word_at(request, 8) = desc[1];
    word_at(request, 0x20) = (word_at(device, 0xc) << 8) | desc[4];
    word_at(request, 0x10) = desc[2];
    word_at(request, 0x14) = desc[9];
    const uint32_t now = Snd_UpdateTime();
    word_at(request, 0x18) = now;
    word_at(request, 0x1c) = desc[8] ? desc[8] + now : 0x7fffffffu;
    word_at(request, 0x30) = now;
    word_at(request, 0x2c) = 0;
    word_at(request, 0x24) = desc[6];
    word_at(request, 0x28) = desc[7];
    word_at(request, 0) = desc[5];
    word_at(request, 0x34) = 0;
    if (file_threaded()) {
        file_release();
        if (file_threaded()) title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e857d)(title_global<uint32_t>(0x00920788));  // SetEvent
    }
    return handle;
}

void XB_CDECL File_AbortRequest(int32_t handle) {
    if (file_threaded()) file_lock();
    uint8_t* const request = file_request(handle);
    if (request) {
        const uint32_t state = word_at(request, 0x2c);
        if (state == 1) word_at(request, 0x2c) = 3;
        else if (state != 3) word_at(request, 0x2c) = 4;
    }
    if (file_threaded()) file_release();
}

uint32_t XB_CDECL File_Exists(uint32_t bit, const char* path) {
    const int32_t handle = File_OpenRead(bit, path);
    File_Close(handle);
    return handle != -1;
}

int32_t XB_CDECL File_QueueRequestEx(uint32_t file, uint32_t buffer, uint32_t offset, uint32_t size, uint32_t arg5) {
    const uint32_t desc[10] = {file, 0, arg5, 0, 2, buffer, offset, size, 0, 0};
    return File_QueueRead(desc);
}

int32_t XB_CDECL File_QueueRequest(uint32_t file, uint32_t buffer, uint32_t offset, uint32_t size) {
    const uint32_t desc[10] = {file, 0, 0, 0, 2, buffer, offset, size, 0, 0};
    return File_QueueRead(desc);
}

int32_t XB_CDECL File_QueueRequestAlt(uint32_t file, uint32_t buffer, uint32_t offset, uint32_t size) {
    const uint32_t desc[10] = {file, 0, 0, 1, 2, buffer, offset, size, 0, 0};
    return File_QueueRead(desc);
}

int32_t XB_CDECL File_ReadAt(uint32_t file, uint32_t buffer, uint32_t offset, uint32_t size) {
    const int32_t handle = File_QueueRequest(file, buffer, offset, size);
    if (handle == -1) return -1;
    pump_requests();
    for (;;) {
        const int32_t status = File_GetRequestStatus(handle);
        if (status == -1) return static_cast<int32_t>(size);
        if (status == 3) {
            File_AbortRequest(handle);
            return 0;
        }
        pump_requests();
    }
}

void XB_CDECL File_AbortRequestsForFile(uint32_t file) {
    int32_t next = HandlePool_First(pool_at(kRequestPool));
    if (next == -1) return;
    do {
        const int32_t handle = next;
        const uint8_t* const request = file_request(handle);
        next = HandlePool_Next(pool_at(kRequestPool), handle);
        if (word_at(request, 0xc) == file) File_AbortRequest(handle);
    } while (next != -1);
}

void XB_CDECL File_Close(int32_t handle) {
    for (;;) {
        uint8_t* const record = file_record(handle);
        if (!record) return;
        File_AbortRequestsForFile(static_cast<uint32_t>(handle));
        const uint32_t kind = word_at(record, 0x20);
        if (kind == 0) {
            if (word_at(record, 0x10)) return;
            const uint8_t* const driver = pointer_at<const uint8_t>(record, 8);
            using CloseOp = void XB_CDECL(uint8_t*);
            CloseOp* const close = driver_op<CloseOp>(driver, 0xc, 0xc);
            close(File_GetDriverData(handle));
            HandlePool_Free(&title_global<uint8_t>(kFilePool), handle);
            return;
        }
        if (kind != 1) return;
        // An alias: drop a reference of the file it shares (+8), then close that one.
        uint8_t* const parent = file_record(static_cast<int32_t>(word_at(record, 8)));
        --word_at(parent, 0x10);
        handle = static_cast<int32_t>(word_at(record, 8));
    }
}

namespace {

uint32_t addr32(const void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}


uint8_t* manager_deleting_dtor(uint8_t* self, uint32_t vtable, uint32_t flags) {
    if (vtable) word_at(self, 0) = vtable;
    ResourceManager_Destruct(self);
    if (flags & 1) Z_Free(self);
    return self;
}

}  // namespace

uint8_t* XB_THISCALL ResourceManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0, flags);
}

uint8_t* XB_THISCALL AnimTreeManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x0083261c, flags);
}

uint8_t* XB_THISCALL SpgManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x00832660, flags);
}

uint8_t* XB_THISCALL DlgManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x00837728, flags);
}

uint8_t* XB_THISCALL LocStringManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x0083765c, flags);
}

uint8_t* XB_THISCALL DbManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x008376a0, flags);
}

uint8_t* XB_THISCALL CfgstrManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x00837618, flags);
}

uint8_t* XB_THISCALL ModelManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x0083be8c, flags);
}

uint8_t* XB_THISCALL XmpbManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x0083bea4, flags);
}

uint8_t* XB_THISCALL AitbManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    return manager_deleting_dtor(self, 0x0083ce3c, flags);
}

uint8_t* XB_THISCALL HeapAllocator_DeletingDtor(uint8_t* self, uint32_t flags) {
    uint8_t* const block = pointer_at<uint8_t>(self, 4);
    word_at(self, 0) = 0x008376c4;  // HeapAllocator vtable
    Z_Free(block);
    word_at(self, 0) = 0x0083760c;  // base allocator vtable
    if (flags & 1) Z_Free(self);
    return self;
}

uint8_t* XB_THISCALL ProfileSave_DeletingDtor(uint8_t* self, uint32_t flags) {
    title_function<void XB_THISCALL(uint8_t*)>(0x003124f0)(self);  // ProfileSave_Dtor
    if (flags & 1) Z_Free(self);
    return self;
}

void* XB_STDCALL AlignedAllocator_Alloc(uint32_t size, uint32_t alignment) {
    if (!size) return nullptr;
    return Mem_AlignedAlloc(size, alignment, 0);
}

namespace {

char* printf_with_length(char* buffer, uint32_t last, const char* format, void* args) {
    buffer[last] = 0;
    buffer[0] = 0;
    title_function<int32_t XB_CDECL(char*, const char*, void*)>(0x001eea95)(buffer, format, args);  // _vsprintf
    buffer[last] = static_cast<char>(std::strlen(buffer));
    return buffer;
}

}  // namespace

PLATFORM_ASM_ENTRY(ShortString_Printf_entry,
                   "leal 8(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(ShortString_Printf) "addl $12, %esp\n"
                   "ret")

char* XB_CDECL ShortString_Printf(char* buffer, const char* format, void* args) {
    return printf_with_length(buffer, 0x7f, format, args);
}

PLATFORM_ASM_ENTRY(FixedStr64_Printf_entry,
                   "leal 8(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(FixedStr64_Printf) "addl $12, %esp\n"
                   "ret")

char* XB_CDECL FixedStr64_Printf(char* buffer, const char* format, void* args) {
    return printf_with_length(buffer, 0x3f, format, args);
}

void XB_THISCALL std_vector_Tidy4(uint8_t* vector) {
    uint8_t* const data = pointer_at<uint8_t>(vector, 4);
    if (data) Z_Free(data);
    word_at(vector, 4) = 0;
    word_at(vector, 8) = 0;
    word_at(vector, 0xc) = 0;
}

namespace {

void list_clear(uint8_t* list) {
    uint8_t* const head = pointer_at<uint8_t>(list, 4);
    uint8_t* node = pointer_at<uint8_t>(head, 0);
    word_at(head, 0) = addr32(head);
    uint8_t* const head2 = pointer_at<uint8_t>(list, 4);
    word_at(head2, 4) = addr32(head2);
    const bool empty = node == pointer_at<uint8_t>(list, 4);
    word_at(list, 8) = 0;
    if (empty) return;
    for (;;) {
        uint8_t* const next = pointer_at<uint8_t>(node, 0);
        Z_Free(node);
        node = next;
        if (next == pointer_at<uint8_t>(list, 4)) return;
    }
}

}  // namespace

PLATFORM_ASM_ENTRY(std_list_Clear_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_list_Clear) "addl $4, %esp\n"
                   "ret")

void XB_CDECL std_list_Clear(uint8_t* list) {
    list_clear(list);
}

void XB_THISCALL std_list_Clear_2(uint8_t* list) {
    list_clear(list);
}

namespace {

// The creators' common part: a 4-aligned heap block, the ResourceManager vtable, registered in the
// manager table (0x009719fc, count 0x009719f8, at most 20), then the class vtable and the global.
uint8_t* create_manager(uint32_t size, uint32_t global) {
    uint8_t* const manager = Heap_AllocAligned(4, size);
    if (!manager) {
        title_global<uint32_t>(global) = 0;
        return nullptr;
    }
    word_at(manager, 0) = 0x00838220;  // ResourceManager vtable
    const int32_t count = title_global<int32_t>(0x009719f8);
    if (count < 0x14) {
        title_global<uint32_t>(0x009719fc + static_cast<uint32_t>(count) * 4) = addr32(manager);
        title_global<int32_t>(0x009719f8) = count + 1;
    }
    return manager;
}

void create_plain_manager(uint32_t vtable, uint32_t global) {
    uint8_t* const manager = create_manager(0x190, global);
    if (!manager) return;
    std::memset(manager + 4, 0, 0x63 * 4);
    word_at(manager, 0) = vtable;
    title_global<uint32_t>(global) = addr32(manager);
}

}  // namespace

void XB_CDECL ResourceManager_CreateModels() {
    create_plain_manager(0x0083be98, 0x0095f6ac);
}

void XB_CDECL ResourceManager_CreateXmpb() {
    create_plain_manager(0x0083beb0, 0x0095f6b0);
}

void XB_CDECL ResourceManager_CreateLgrid() {
    create_plain_manager(0x0083be70, 0x0095f6a0);
}

void XB_CDECL ResourceManager_CreateLevel() {
    uint8_t* const manager = create_manager(0xc, 0x0095f534);
    if (!manager) return;
    word_at(manager, 0) = 0x00832614;
    word_at(manager, 8) = 0;
    word_at(manager, 4) = 0;
    title_global<uint32_t>(0x0095f534) = addr32(manager);
}

void XB_CDECL ResourceManager_CreateAnimTrees() {
    create_plain_manager(0x0083261c, 0x0095f524);
}

void XB_CDECL ResourceManager_CreateLocStrings() {
    create_plain_manager(0x00837668, 0x0095f5d8);
}

void XB_CDECL ResourceManager_CreateSoundBanks() {
    uint8_t* const manager = create_manager(0x84c, 0x0095f4c0);
    if (!manager) return;
    word_at(manager, 0) = 0x008327a8;  // SoundBankManager vtable
    manager[4] = 0;
    manager[5] = 0;
    SoundBankTable_Init(manager + 8);
    title_global<uint32_t>(0x0095f4c0) = addr32(manager);
}

void XB_CDECL ResourceManager_CreateDlg() {
    create_plain_manager(0x00837728, 0x0095f5ac);
}

void XB_CDECL ResourceManager_CreateAitb() {
    create_plain_manager(0x0083ce48, 0x0095f718);
}

void XB_CDECL ResourceManager_CreateDb() {
    create_plain_manager(0x008376ac, 0x0095f5a8);
}

void XB_CDECL ResourceManager_CreateCfgstr() {
    create_plain_manager(0x00837650, 0x0095f5d4);
}

PLATFORM_ASM_ENTRY(NameMap_NodeCtor_entry,
                   "pushl %eax\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(NameMap_NodeCtor) "addl $16, %esp\n"
                   "ret")

uint8_t* XB_CDECL NameMap_NodeCtor(uint8_t* node, uint32_t next, uint32_t prev, const uint8_t* value) {
    word_at(node, 0) = next;
    word_at(node, 4) = prev;
    word_at(node, 8) = word_at(value, 0);
    int32_t length = static_cast<int32_t>(next);
    char* const name = reinterpret_cast<char*>(node + 0xc);
    FixedStr_Copy(name, &length, 0x1f, reinterpret_cast<const char*>(value + 4), value[4 + 0x1f]);
    name[0x1f] = static_cast<char>(length);
    return node;
}

uint32_t* XB_REGS1 XB_STDCALL DynArray_AllocAlignedZeroed(uint32_t capacity, uint32_t count) {
    uint32_t* const data = static_cast<uint32_t*>(Mem_AlignedAlloc(capacity * 4, 4, 0));
    uint32_t* const end = data + count;
    for (uint32_t* p = data; p != end; ++p) {
        if (p) *p = 0;
    }
    return data;
}

PLATFORM_ASM_ENTRY(DynArray_Alloc12_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(DynArray_Alloc12) "addl $8, %esp\n"
                   "ret")

uint32_t* XB_CDECL DynArray_Alloc12(uint32_t capacity, uint32_t count) {
    uint32_t* const data = static_cast<uint32_t*>(Mem_AlignedAlloc(capacity * 12, 4, 0));
    uint32_t* const end = data + count * 3;
    for (uint32_t* p = data; p != end; p += 3) {
        if (p) {
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
        }
    }
    return data;
}

namespace {

// MSVC std::string (0x1c bytes): +4 the buffer (or its pointer from 16 capacity on), +0x14 size, +0x18
// capacity. Tidy: free a heap buffer, back to the 15-char local one, empty.
void string_tidy(uint8_t* s) {
    if (word_at(s, 0x18) >= 0x10) Z_Free(pointer_at<uint8_t>(s, 4));
    word_at(s, 0x18) = 0xf;
    word_at(s, 0x14) = 0;
    s[4] = 0;
}

}  // namespace

PLATFORM_ASM_ENTRY(std_string_DestroyRange_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(std_string_DestroyRange) "addl $8, %esp\n"
                   "ret")

void XB_CDECL std_string_DestroyRange(uint8_t* first, uint8_t* last) {
    for (uint8_t* s = first; s != last; s += 0x1c) string_tidy(s);
}

PLATFORM_ASM_ENTRY(LocEntry_Dtor_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(LocEntry_Dtor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL LocEntry_Dtor(uint8_t* entry) {
    string_tidy(entry + 0x1c);
    string_tidy(entry);
}

PLATFORM_ASM_ENTRY(std_map_string_DestroyValue_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_map_string_DestroyValue) "addl $4, %esp\n"
                   "ret")

void XB_CDECL std_map_string_DestroyValue(uint8_t* value) {
    string_tidy(value + 0x38);
    string_tidy(value + 0x1c);
    string_tidy(value);
}

uint32_t* XB_REGS2 XB_CDECL std_UninitializedFillN4_2(uint32_t, const uint32_t* value, uint32_t* dst, uint32_t count) {
    for (; count; --count, ++dst) {
        if (dst) *dst = *value;
    }
    return dst;
}

uint32_t* XB_REGS2 XB_CDECL std_UninitializedCopy4_2(uint32_t, const uint32_t* last, const uint32_t* first, uint32_t* dst) {
    for (; first != last; ++first, ++dst) {
        if (dst) *dst = *first;
    }
    return dst;
}

namespace {

// A list head node: Z_Malloc'd, next and prev pointing at itself (the allocator's exception frame
// around it has no effect).
uint8_t* buy_list_head(uint32_t size) {
    uint8_t* const head = Z_Malloc(size);
    if (head) word_at(head, 0) = addr32(head);
    uint8_t* const prev = head + 4;
    if (prev) word_at(prev, 0) = addr32(head);
    return head;
}

}  // namespace

uint8_t* XB_CDECL std_list_BuyHeadNameMap() {
    return buy_list_head(0x2c);
}

uint8_t* XB_CDECL std_list_BuyHeadZ() {
    return buy_list_head(0x10);
}

uint8_t* XB_THISCALL std_list_BuyNode8(const uint32_t* value, uint32_t next, uint32_t prev) {
    uint8_t* const node = Z_Malloc(0x10);
    if (node) {
        word_at(node, 0) = next;
        word_at(node, 4) = prev;
        word_at(node, 8) = value[0];
        word_at(node, 0xc) = value[1];
    }
    return node;
}

PLATFORM_ASM_ENTRY(AnglesToForward_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(AnglesToForward) "addl $8, %esp\n"
                   "ret")

void XB_CDECL AnglesToForward(const float* angles, float* forward) {
    // (cos pitch * cos yaw, cos pitch * sin yaw, -sin pitch).
    const float to_radians = title_global<float>(0x0083edd8);
    float sy, cy, sp, cp;
    FastSinCos(&sy, 0, &cy, angles[1] * to_radians);
    FastSinCos(&sp, 0, &cp, angles[0] * to_radians);
    forward[1] = cp * sy;
    forward[0] = cp * cy;
    forward[2] = 0.0f - sp;
}

PLATFORM_ASM_ENTRY(PtrVector_PushBack_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(PtrVector_PushBack) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL PtrVector_PushBack(uint8_t* vector, const uint32_t* item) {
    // {data, capacity, count}: grows by 1 up to 3 elements, then by 4 (Mem_AlignedAlloc, 4-aligned).
    const int32_t count = static_cast<int32_t>(word_at(vector, 8));
    if (!(count < static_cast<int32_t>(word_at(vector, 4)))) {
        const int32_t capacity = count > 3 ? count + 4 : count + 1;
        uint32_t* const data = static_cast<uint32_t*>(Mem_AlignedAlloc(static_cast<uint32_t>(capacity) * 4, 4, 0));
        for (int32_t i = 0; i < static_cast<int32_t>(word_at(vector, 8)); ++i) data[i] = pointer_at<uint32_t>(vector, 0)[i];
        void* const old = pointer_at<void>(vector, 0);
        if (old) {
            Mem_AlignedFree(old);
            word_at(vector, 0) = 0;
        }
        word_at(vector, 0) = addr32(data);
        word_at(vector, 4) = static_cast<uint32_t>(capacity);
    }
    const uint32_t index = word_at(vector, 8);
    pointer_at<uint32_t>(vector, 0)[index] = *item;
    word_at(vector, 8) = word_at(vector, 8) + 1;
}

namespace {

// The 4-byte DynArray allocator: SmallHeap (0x0095f4a4) when the size fits its largest pool
// (+0x28 table, pool count +0x24), else the heap 0x0095f4a0 (4-aligned); every int zeroed.
uint32_t* alloc_ints_zeroed(uint32_t count) {
    uint8_t* const small = title_global<uint8_t*>(0x0095f4a4);
    const uint32_t size = count << 2;
    int32_t last = static_cast<int32_t>(word_at(small, 0x24)) - 1;
    if (last <= 0) last = 0;
    const int32_t limit = *reinterpret_cast<const int16_t*>(small + 0x28 + last * 2);
    uint8_t* data = nullptr;
    if (!(size > static_cast<uint32_t>(limit))) data = SmallHeap_Alloc(small, size, 0);
    if (!data) {
        data = Heap_AllocFromHeap(title_global<uint8_t*>(0x0095f4a0), size, 4);
        if (!data) return nullptr;  // the original runs into an int3 here
    }
    uint32_t* const ints = reinterpret_cast<uint32_t*>(data);
    uint32_t* const end = reinterpret_cast<uint32_t*>(data + size);
    for (uint32_t* p = ints; p != end; ++p) {
        if (p) *p = 0;
    }
    return ints;
}

}  // namespace

uint32_t* XB_STDCALL DynArray_AllocIntsZeroed(uint32_t count, uint32_t) {
    return alloc_ints_zeroed(count);
}

uint32_t* XB_STDCALL DynArray_AllocIntsZeroed_2(uint32_t count) {
    return alloc_ints_zeroed(count);
}

PLATFORM_ASM_ENTRY(PtrVector_Resize_entry,
                   "pushl %ebx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(PtrVector_Resize) "addl $8, %esp\n"
                   "ret")

void XB_CDECL PtrVector_Resize(uint8_t* vector, int32_t size) {
    // {data, capacity, count}: within the capacity only the count changes; else a new exact-size
    // block (Mem_AlignedAlloc) takes the old elements and the old one is freed.
    if (!(size > static_cast<int32_t>(word_at(vector, 4)))) {
        word_at(vector, 8) = static_cast<uint32_t>(size);
        return;
    }
    uint32_t* const data = static_cast<uint32_t*>(Mem_AlignedAlloc(static_cast<uint32_t>(size) * 4, 4, 0));
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(vector, 8)); ++i) data[i] = pointer_at<uint32_t>(vector, 0)[i];
    void* const old = pointer_at<void>(vector, 0);
    if (old) {
        Mem_AlignedFree(old);
        word_at(vector, 0) = 0;
    }
    word_at(vector, 0) = addr32(data);
    word_at(vector, 4) = static_cast<uint32_t>(size);
    word_at(vector, 8) = static_cast<uint32_t>(size);
}

namespace {

// DynArray {data, capacity, count} of ints: a new zeroed block of `capacity` takes the elements; the
// old one goes back to the SmallHeap that owns it or to the heap 0x0095f4a0.
void dynarray_regrow(uint8_t* array, uint32_t capacity) {
    uint32_t* const data = DynArray_AllocIntsZeroed(capacity, word_at(array, 8));
    for (uint32_t i = 0; i < word_at(array, 8); ++i) data[i] = pointer_at<uint32_t>(array, 0)[i];
    uint8_t* const old = pointer_at<uint8_t>(array, 0);
    if (old) {
        uint8_t* const small = title_global<uint8_t*>(0x0095f4a4);
        if (SmallHeap_Owns(reinterpret_cast<const uint32_t*>(small), addr32(old))) {
            SmallHeap_Free(title_global<uint8_t*>(0x0095f4a4), old);
        } else {
            Heap_FreeIn2(title_global<uint32_t>(0x0095f4a0), old);
        }
    }
    word_at(array, 0) = addr32(data);
    word_at(array, 4) = capacity;
}

}  // namespace

void XB_THISCALL DynArray_Reserve(uint8_t* array, uint32_t capacity) {
    if (capacity > word_at(array, 4)) dynarray_regrow(array, capacity);
}

uint32_t XB_THISCALL DynArray_PushBack(uint8_t* array, const uint32_t* item) {
    const uint32_t count = word_at(array, 8);
    if (!(count < word_at(array, 4))) dynarray_regrow(array, count + 4);
    const uint32_t index = word_at(array, 8);
    pointer_at<uint32_t>(array, 0)[index] = *item;
    word_at(array, 8) = word_at(array, 8) + 1;
    return index;
}

uint32_t XB_REGS3 XB_CDECL std_list_IncSize_4(uint32_t eax, uint8_t* list, uint32_t count) {
    const uint32_t size = word_at(list, 8);
    if (!(0x1fffffffu - size >= count)) {
        // "list<T> too long": the title's length_error throw (never returns).
        return title_function<uint32_t XB_REGS3 XB_CDECL(uint32_t, uint8_t*, uint32_t)>(0x00028b40)(eax, list, count);
    }
    word_at(list, 8) = size + count;
    return size + count;
}

uint32_t XB_THISCALL std_list_Insert8(const uint32_t* value, uint8_t* list, uint8_t* where) {
    uint8_t* const node = std_list_BuyNode8(value, addr32(where), word_at(where, 4));
    const uint32_t size = std_list_IncSize_4(0, list, 1);
    word_at(where, 4) = addr32(node);
    word_at(pointer_at<uint8_t>(node, 4), 0) = addr32(node);
    return size;
}

namespace {

// MSVC std::string pieces: the buffer (heap pointer from capacity 16 on), size +0x14, capacity +0x18.
uint8_t* string_buffer(uint8_t* s) {
    return word_at(s, 0x18) >= 0x10 ? pointer_at<uint8_t>(s, 4) : s + 4;
}

void string_init_empty(uint8_t* s) {
    word_at(s, 0x18) = 0xf;
    word_at(s, 0x14) = 0;
    s[4] = 0;
}

// 0x001ee881 / 0x001ee8c1: the CRT's "invalid string position" (out_of_range) / "string too long"
// (length_error) throws.
void string_throw_range() { title_function<void XB_CDECL()>(0x001ee881)(); }
void string_throw_length(uint8_t* s) { title_function<void XB_THISCALL(uint8_t*)>(0x001ee8c1)(s); }

// The trees' erase(iterator) on the end node: an out_of_range ("invalid map/set<T> iterator",
// 0x008327b0; logic_error vtable 0x007f6748, then 0x00832778) thrown with type info 0x00895b68.
// Never returns.
void tree_throw_invalid_iterator() {
    uint8_t message[0x1c];
    string_init_empty(message);
    std_string_AssignN(message, reinterpret_cast<const char*>(uintptr_t{0x008327b0}), 0x1b);
    uint8_t error[0x28];
    std_exception_Ctor(reinterpret_cast<uint32_t*>(error));
    word_at(error, 0) = 0x007f6748;
    string_init_empty(error + 0xc);
    std_string_AssignSubstr(error + 0xc, message, 0, 0xffffffffu);
    word_at(error, 0) = 0x00832778;
    title_function<void XB_STDCALL(uint8_t*, uint32_t)>(0x001f2302)(error, 0x00895b68);  // _CxxThrowException
}

// Red-black tree erase(where) (VC7.1 _Tree::erase): unlinks the node, rebalances, destroys the value
// (T::destroy), frees the node and counts the size down; returns the successor. T::Nil = the isnil
// byte's offset (the colour byte before it, 1 = black).
struct MapStringTree {
    static constexpr uint32_t Nil = 0x65;
    static void lrotate(uint8_t* node, uint8_t* tree) { std_map_string_Lrotate(node, tree); }
    static void rrotate(uint8_t* node, uint8_t* tree) { std_map_string_Rrotate(node, tree); }
    static uint8_t* min(uint8_t* node) { return std_map_string_Min(node); }
    static uint8_t* max(uint8_t* node) { return std_map_string_Max(node); }
    static void inc(uint8_t** it) { std_map_string_Inc(0, it); }
    static void destroy(uint8_t* node) { std_map_string_DestroyValue(node + 0xc); }
    static void erase_subtree(uint8_t* tree, uint8_t* node) { std_map_string_EraseSubtree(tree, node); }
    static void erase(uint8_t* tree, uint8_t* where) {
        uint8_t* next;
        std_map_string_Erase(tree, &next, where);
    }
};

struct SetStringTree {
    static constexpr uint32_t Nil = 0x2d;
    static void lrotate(uint8_t* node, uint8_t* tree) { std_set_string_Lrotate(node, tree); }
    static void rrotate(uint8_t* node, uint8_t* tree) { std_set_string_Rrotate(node, tree); }
    static uint8_t* min(uint8_t* node) { return std_set_string_Min(node); }
    static uint8_t* max(uint8_t* node) { return std_set_string_Max(node); }
    static void inc(uint8_t** it) { std_set_string_Inc(0, it); }
    static void destroy(uint8_t* node) { string_tidy(node + 0xc); }
    static void erase_subtree(uint8_t* tree, uint8_t* node) { std_set_string_EraseSubtree(tree, node); }
    static void erase(uint8_t* tree, uint8_t* where) {
        uint8_t* next;
        std_set_string_Erase(tree, &next, where);
    }
};

struct Tree29 {
    static constexpr uint32_t Nil = 0x29;
    static void lrotate(uint8_t* node, uint8_t* tree) { std_tree_Lrotate29(node, tree); }
    static void rrotate(uint8_t* node, uint8_t* tree) { std_tree_Rrotate29(node, tree); }
    static uint8_t* min(uint8_t* node) { return std_tree_Min29(node); }
    static uint8_t* max(uint8_t* node) { return std_tree_Max29(node); }
    static void inc(uint8_t** it) { std_tree_Inc29(0, it); }
    static void destroy(uint8_t* node) { string_tidy(node + 0xc); }
    static void erase_subtree(uint8_t* tree, uint8_t* node) { std_map_string_EraseSubtree_2(tree, node); }
    static void erase(uint8_t* tree, uint8_t* where) {
        uint8_t* next;
        std_tree_Erase29(tree, &next, where);
    }
};

template <typename T>
uint8_t* tree_erase(uint8_t* tree, uint8_t* where) {
    constexpr uint32_t Nil = T::Nil;
    constexpr uint32_t Color = Nil - 1;
    constexpr uint8_t Black = 1;
    constexpr uint8_t Red = 0;
    if (where[Nil]) tree_throw_invalid_iterator();
    uint8_t* const erased = where;
    uint8_t* next = where;
    T::inc(&next);
    uint8_t* fix;
    uint8_t* fix_parent;
    uint8_t* node = erased;
    if (node_link(erased, 0)[Nil]) {
        fix = node_link(erased, 8);
    } else if (node_link(erased, 8)[Nil]) {
        fix = node_link(erased, 0);
    } else {
        node = next;
        fix = node_link(node, 8);
    }
    uint8_t* const head = node_link(tree, 4);
    if (node == erased) {
        fix_parent = node_link(erased, 4);
        if (!fix[Nil]) word_at(fix, 4) = addr32(fix_parent);
        if (node_link(head, 4) == erased) {
            word_at(head, 4) = addr32(fix);
        } else if (node_link(fix_parent, 0) == erased) {
            word_at(fix_parent, 0) = addr32(fix);
        } else {
            word_at(fix_parent, 8) = addr32(fix);
        }
        uint8_t* const head2 = node_link(tree, 4);
        if (node_link(head2, 0) == erased) word_at(head2, 0) = addr32(fix[Nil] ? fix_parent : T::min(fix));
        uint8_t* const head3 = node_link(tree, 4);
        if (node_link(head3, 8) == erased) word_at(head3, 8) = addr32(fix[Nil] ? fix_parent : T::max(fix));
    } else {
        word_at(node_link(erased, 0), 4) = addr32(node);
        word_at(node, 0) = word_at(erased, 0);
        if (node == node_link(erased, 8)) {
            fix_parent = node;
        } else {
            fix_parent = node_link(node, 4);
            if (!fix[Nil]) word_at(fix, 4) = addr32(fix_parent);
            word_at(fix_parent, 0) = addr32(fix);
            word_at(node, 8) = word_at(erased, 8);
            word_at(node_link(erased, 8), 4) = addr32(node);
        }
        uint8_t* const head2 = node_link(tree, 4);
        if (node_link(head2, 4) == erased) {
            word_at(head2, 4) = addr32(node);
        } else {
            uint8_t* const parent = node_link(erased, 4);
            if (node_link(parent, 0) == erased) {
                word_at(parent, 0) = addr32(node);
            } else {
                word_at(parent, 8) = addr32(node);
            }
        }
        word_at(node, 4) = word_at(erased, 4);
        const uint8_t color = node[Color];
        node[Color] = erased[Color];
        erased[Color] = color;
    }
    if (erased[Color] == Black) {
        for (; fix != node_link(node_link(tree, 4), 4) && fix[Color] == Black;
             fix = fix_parent, fix_parent = node_link(fix, 4)) {
            if (fix == node_link(fix_parent, 0)) {
                uint8_t* sibling = node_link(fix_parent, 8);
                if (sibling[Color] == Red) {
                    sibling[Color] = Black;
                    fix_parent[Color] = Red;
                    T::lrotate(fix_parent, tree);
                    sibling = node_link(fix_parent, 8);
                }
                if (sibling[Nil]) {
                    fix = fix_parent;
                } else if (node_link(sibling, 0)[Color] == Black && node_link(sibling, 8)[Color] == Black) {
                    sibling[Color] = Red;
                    fix = fix_parent;
                } else {
                    if (node_link(sibling, 8)[Color] == Black) {
                        node_link(sibling, 0)[Color] = Black;
                        sibling[Color] = Red;
                        T::rrotate(sibling, tree);
                        sibling = node_link(fix_parent, 8);
                    }
                    sibling[Color] = fix_parent[Color];
                    fix_parent[Color] = Black;
                    node_link(sibling, 8)[Color] = Black;
                    T::lrotate(fix_parent, tree);
                    break;
                }
            } else {
                uint8_t* sibling = node_link(fix_parent, 0);
                if (sibling[Color] == Red) {
                    sibling[Color] = Black;
                    fix_parent[Color] = Red;
                    T::rrotate(fix_parent, tree);
                    sibling = node_link(fix_parent, 0);
                }
                if (sibling[Nil]) {
                    fix = fix_parent;
                } else if (node_link(sibling, 8)[Color] == Black && node_link(sibling, 0)[Color] == Black) {
                    sibling[Color] = Red;
                    fix = fix_parent;
                } else {
                    if (node_link(sibling, 0)[Color] == Black) {
                        node_link(sibling, 8)[Color] = Black;
                        sibling[Color] = Red;
                        T::lrotate(sibling, tree);
                        sibling = node_link(fix_parent, 0);
                    }
                    sibling[Color] = fix_parent[Color];
                    fix_parent[Color] = Black;
                    node_link(sibling, 0)[Color] = Black;
                    T::rrotate(fix_parent, tree);
                    break;
                }
            }
        }
        fix[Color] = Black;
    }
    T::destroy(erased);
    Z_Free(erased);
    if (word_at(tree, 8) > 0) --word_at(tree, 8);
    return next;
}


// erase(first, last): everything (first = begin, last = end) drops the whole tree under the root and
// resets the head; else one erase per node. Returns the end of the erased range.
template <typename T>
uint8_t** tree_erase_range(uint8_t* tree, uint8_t** result, uint8_t* first, uint8_t* last) {
    uint8_t* const head = node_link(tree, 4);
    if (first == node_link(head, 0) && last == head) {
        T::erase_subtree(tree, node_link(head, 4));
        word_at(node_link(tree, 4), 4) = word_at(tree, 4);
        word_at(tree, 8) = 0;
        word_at(node_link(tree, 4), 0) = word_at(tree, 4);
        word_at(node_link(tree, 4), 8) = word_at(tree, 4);
        *result = node_link(node_link(tree, 4), 0);
        return result;
    }
    while (first != last) {
        uint8_t* const where = first;
        T::inc(&first);
        T::erase(tree, where);
    }
    *result = first;
    return result;
}

// The head node of a tree: Z_Malloc(size), links null, black, not nil (the allocator's exception
// frame has no effect).
template <uint32_t Size, uint32_t Color>
uint8_t* tree_buy_head() {
    uint8_t* const node = Z_Malloc(Size);
    if (node) word_at(node, 0) = 0;
    uint8_t* const parent = node + 4;
    if (parent) word_at(parent, 0) = 0;
    uint8_t* const right = node + 8;
    if (right) word_at(right, 0) = 0;
    node[Color] = 1;
    node[Color + 1] = 0;
    return node;
}

}  // namespace

uint8_t* XB_THISCALL std_string_AssignSubstr(uint8_t* self, const uint8_t* source, uint32_t pos, uint32_t count) {
    if (word_at(source, 0x14) < pos) string_throw_range();
    uint32_t length = word_at(source, 0x14) - pos;
    if (count < length) length = count;
    if (self == source) {
        std_string_Erase(self, pos + length, 0xffffffffu);
        std_string_Erase(self, 0, pos);
        return self;
    }
    if (length > 0xfffffffeu) string_throw_length(self);
    if (word_at(self, 0x18) < length) {
        std_string_Copy(self, length, word_at(self, 0x14));
    } else if (length == 0) {
        word_at(self, 0x14) = 0;
        string_buffer(self)[0] = 0;
        return self;
    }
    if (length) {
        const uint8_t* const from = word_at(source, 0x18) >= 0x10 ? pointer_at<const uint8_t>(source, 4) : source + 4;
        platform_rep_movs(string_buffer(self), from + pos, length);
        word_at(self, 0x14) = length;
        string_buffer(self)[length] = 0;
    }
    return self;
}

uint8_t** XB_STDCALL std_map_string_Erase(uint8_t* tree, uint8_t** result, uint8_t* where) {
    *result = tree_erase<MapStringTree>(tree, where);
    return result;
}

uint8_t** XB_STDCALL std_set_string_Erase(uint8_t* tree, uint8_t** result, uint8_t* where) {
    *result = tree_erase<SetStringTree>(tree, where);
    return result;
}

uint8_t** XB_STDCALL std_tree_Erase29(uint8_t* tree, uint8_t** result, uint8_t* where) {
    *result = tree_erase<Tree29>(tree, where);
    return result;
}

void XB_THISCALL std_map_string_EraseSubtree(uint8_t* tree, uint8_t* node) {
    while (!node[0x65]) {
        std_map_string_EraseSubtree(tree, node_link(node, 8));
        uint8_t* const left = node_link(node, 0);
        std_map_string_DestroyValue(node + 0xc);
        Z_Free(node);
        node = left;
    }
}

void XB_THISCALL std_set_string_EraseSubtree(uint8_t* tree, uint8_t* node) {
    while (!node[0x2d]) {
        std_set_string_EraseSubtree(tree, node_link(node, 8));
        uint8_t* const left = node_link(node, 0);
        string_tidy(node + 0xc);
        Z_Free(node);
        node = left;
    }
}

void XB_THISCALL std_map_string_EraseSubtree_2(uint8_t* tree, uint8_t* node) {
    while (!node[0x29]) {
        std_map_string_EraseSubtree_2(tree, node_link(node, 8));
        uint8_t* const left = node_link(node, 0);
        string_tidy(node + 0xc);
        Z_Free(node);
        node = left;
    }
}


PLATFORM_ASM_ENTRY(std_map_string_EraseRange_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_map_string_EraseRange) "addl $16, %esp\n"
                   "ret $12")

uint8_t** XB_CDECL std_map_string_EraseRange(uint8_t* tree, uint8_t** result, uint8_t* first, uint8_t* last) {
    return tree_erase_range<MapStringTree>(tree, result, first, last);
}

PLATFORM_ASM_ENTRY(std_set_string_EraseRange_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_set_string_EraseRange) "addl $16, %esp\n"
                   "ret $12")

uint8_t** XB_CDECL std_set_string_EraseRange(uint8_t* tree, uint8_t** result, uint8_t* first, uint8_t* last) {
    return tree_erase_range<SetStringTree>(tree, result, first, last);
}

PLATFORM_ASM_ENTRY(std_tree_EraseRange29_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_tree_EraseRange29) "addl $16, %esp\n"
                   "ret $12")

uint8_t** XB_CDECL std_tree_EraseRange29(uint8_t* tree, uint8_t** result, uint8_t* first, uint8_t* last) {
    return tree_erase_range<Tree29>(tree, result, first, last);
}

uint8_t* XB_CDECL std_map_string_BuyHead() { return tree_buy_head<0x68, 0x64>(); }
uint8_t* XB_CDECL std_set_string_BuyHead() { return tree_buy_head<0x30, 0x2c>(); }
uint8_t* XB_CDECL std_tree_BuyHead29() { return tree_buy_head<0x2c, 0x28>(); }

PLATFORM_ASM_ENTRY(std_map_string_PairCopy_entry,
                   "pushl %eax\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(std_map_string_PairCopy) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL std_map_string_PairCopy(uint8_t* dst, const uint8_t* src) {
    for (uint32_t offset = 0; offset < 0x54; offset += 0x1c) {
        string_init_empty(dst + offset);
        std_string_AssignSubstr(dst + offset, src + offset, 0, 0xffffffffu);
    }
    word_at(dst, 0x54) = word_at(src, 0x54);
    return dst;
}

namespace {

uint8_t* tree_node_links(uint8_t* node, uint32_t left, uint32_t parent, uint32_t right) {
    word_at(node, 0) = left;
    word_at(node, 4) = parent;
    word_at(node, 8) = right;
    return node;
}

}  // namespace

PLATFORM_ASM_ENTRY(std_tree_NodeCtor29_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_tree_NodeCtor29) "addl $24, %esp\n"
                   "ret $8")

uint8_t* XB_CDECL std_tree_NodeCtor29(uint8_t* node, uint32_t left, uint32_t parent, uint32_t right,
                                      const uint8_t* value, uint32_t color) {
    tree_node_links(node, left, parent, right);
    string_init_empty(node + 0xc);
    std_string_AssignSubstr(node + 0xc, value, 0, 0xffffffffu);
    node[0x28] = static_cast<uint8_t>(color);
    node[0x29] = 0;
    return node;
}

PLATFORM_ASM_ENTRY(std_map_string_NodeCtor_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_map_string_NodeCtor) "addl $24, %esp\n"
                   "ret $8")

uint8_t* XB_CDECL std_map_string_NodeCtor(uint8_t* node, uint32_t left, uint32_t parent, uint32_t right,
                                          const uint8_t* value, uint32_t color) {
    tree_node_links(node, left, parent, right);
    std_map_string_PairCopy(node + 0xc, value);
    node[0x64] = static_cast<uint8_t>(color);
    node[0x65] = 0;
    return node;
}

PLATFORM_ASM_ENTRY(std_set_string_NodeCtor_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_set_string_NodeCtor) "addl $24, %esp\n"
                   "ret $8")

uint8_t* XB_CDECL std_set_string_NodeCtor(uint8_t* node, uint32_t left, uint32_t parent, uint32_t right,
                                          const uint8_t* value, uint32_t color) {
    tree_node_links(node, left, parent, right);
    string_init_empty(node + 0xc);
    std_string_AssignSubstr(node + 0xc, value, 0, 0xffffffffu);
    word_at(node, 0x28) = word_at(value, 0x1c);
    node[0x2c] = static_cast<uint8_t>(color);
    node[0x2d] = 0;
    return node;
}

uint8_t* XB_STDCALL std_map_string_BuyNode(uint32_t left, uint32_t parent, uint32_t right, const uint8_t* value,
                                           uint32_t color) {
    uint8_t* const node = Z_Malloc(0x68);
    if (node) std_map_string_NodeCtor(node, left, parent, right, value, color);
    return node;
}

uint8_t* XB_STDCALL std_set_string_BuyNode(uint32_t left, uint32_t parent, uint32_t right, const uint8_t* value,
                                           uint32_t color) {
    uint8_t* const node = Z_Malloc(0x30);
    if (node) std_set_string_NodeCtor(node, left, parent, right, value, color);
    return node;
}

uint8_t* XB_STDCALL std_tree_BuyNode29(uint32_t left, uint32_t parent, uint32_t right, const uint8_t* value,
                                       uint32_t color) {
    uint8_t* const node = Z_Malloc(0x2c);
    if (node) std_tree_NodeCtor29(node, left, parent, right, value, color);
    return node;
}

namespace {

// vector<T*>(count, value) (VC7.1): {allocator, first, last, end}; Z_Malloc(count * 4), filled with *value.
// "vector<T> too long" (std_vector_Xlen_2) above 0x3fffffff elements.
void vector_construct_n4(uint32_t count, uint8_t* vector, const uint32_t* value) {
    word_at(vector, 4) = 0;
    word_at(vector, 8) = 0;
    word_at(vector, 0xc) = 0;
    if (!count) return;
    if (count > 0x3fffffffu) title_function<void XB_CDECL()>(0x000156f0)();  // std_vector_Xlen_2 (throws)
    const uint32_t bytes = count * 4;
    uint8_t* const data = Z_Malloc(bytes);
    word_at(vector, 4) = addr32(data);
    word_at(vector, 8) = addr32(data);
    word_at(vector, 0xc) = addr32(data + bytes);
    word_at(vector, 8) = addr32(std_vector_Ufill4(pointer_at<uint32_t>(vector, 4), count, value, addr32(vector)));
}

}  // namespace

void XB_THISCALL std_vector_ConstructN4Z(uint32_t count, uint8_t* vector, const uint32_t* value) {
    vector_construct_n4(count, vector, value);
}

void XB_THISCALL std_vector_ConstructN4Z_2(uint32_t count, uint8_t* vector, const uint32_t* value) {
    vector_construct_n4(count, vector, value);
}

PLATFORM_ASM_ENTRY(std_vector_Ufill4_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edx\n"
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(std_vector_Ufill4) "addl $16, %esp\n"
                   "ret $4")

uint32_t* XB_CDECL std_vector_Ufill4(uint32_t* dst, uint32_t count, const uint32_t* value, uint32_t) {
    std_UninitializedFillN4_2(0, value, dst, count);
    return dst + count;
}

PLATFORM_ASM_ENTRY(File_LayoutMemory_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(File_LayoutMemory) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL File_LayoutMemory(uint8_t* base) {
    // Cursor 0x00920790 (bytes laid out so far), room left 0x00920794, base 0x0092078c. Each block is
    // padded by its alignment - 1 and placed at the aligned address base + cursor; without a base (or
    // when a block no longer fits) its address is 0. The room is the previous call's total.
    uint32_t& cursor = title_global<uint32_t>(0x00920790);
    uint32_t& room = title_global<uint32_t>(0x00920794);
    const uint32_t start = addr32(base);
    uint32_t left = base ? cursor : 0;
    title_global<uint32_t>(0x0092078c) = start;
    room = left;
    cursor = 0;
    uint32_t failed = 0;
    uint32_t align = 1;
    uint32_t data_size = 1;
    uint32_t buffers[29];
    int32_t index = 0;
    if (title_global<int32_t>(0x008bfe7c) > 0) {
        uint8_t* const* const devices = title_global<uint8_t* const*>(0x008bfe80);
        do {
            uint8_t* const device = devices[index];
            const uint32_t* const config = pointer_at<const uint32_t>(device, 0xc);
            if (data_size < config[0]) data_size = config[0];
            if (align < config[1]) align = config[1];
            const uint32_t kind = *pointer_at<const uint32_t>(device, 4);
            if (kind == 2 || kind == 3 || kind == 4) {
                const uint32_t* const buffer = pointer_at<const uint32_t>(device, 0x14);
                if (!buffer[4]) {
                    uint32_t buffer_align = buffer[0];
                    if (buffer_align < buffer[1]) buffer_align = buffer[1];
                    const uint32_t size = buffer[3] + buffer_align - 1;
                    uint32_t address;
                    if (base && size > left) {
                        address = 0;
                    } else {
                        if (base) left -= size;
                        const uint32_t at = cursor;
                        const uint32_t aligned = (buffer_align + at + start - 1) / buffer_align * buffer_align;
                        cursor = at + size;
                        address = base ? aligned : 0;
                    }
                    buffers[index] = address;
                    failed |= static_cast<uint32_t>(address == 0) << (index & 0x1f);
                }
            }
            ++index;
        } while (index < title_global<int32_t>(0x008bfe7c));
        room = left;
    }
    data_size = (data_size + align - 1) / align * align;
    const uint32_t requests = title_global<uint32_t>(0x008bfd78);
    uint32_t size = requests * 0x44 + 0x3f;
    uint32_t request_records;
    if (base && size > left) {
        request_records = 0;
    } else {
        if (base) {
            left -= size;
            room = left;
        }
        const uint32_t at = cursor;
        cursor = at + size;
        request_records = base ? (at + start + 0x3f) >> 6 << 6 : 0;
    }
    size = requests * 20 + 3;
    uint32_t request_nodes;
    if (base && size > left) {
        request_nodes = 0;
    } else {
        if (base) {
            left -= size;
            room = left;
        }
        const uint32_t at = cursor;
        cursor = at + size;
        request_nodes = base ? (at + start + 3) >> 2 << 2 : 0;
    }
    size = (title_global<uint32_t>(0x008bfd74) << 4) + 0x3f;
    uint32_t device_records;
    if (base && size > left) {
        device_records = 0;
    } else {
        if (base) {
            left -= size;
            room = left;
        }
        const uint32_t at = cursor;
        cursor = at + size;
        device_records = base ? (at + start + 0x3f) >> 6 << 6 : 0;
    }
    const uint32_t files = title_global<uint32_t>(0x008bfd70);
    size = files * 36 + 0x3f;
    uint32_t file_records;
    if (base && size > left) {
        file_records = 0;
    } else {
        if (base) {
            left -= size;
            room = left;
        }
        const uint32_t at = cursor;
        cursor = at + size;
        file_records = base ? (at + start + 0x3f) >> 6 << 6 : 0;
    }
    size = files * data_size + align - 1;
    uint32_t driver_data;
    uint32_t total;
    if (base && size > left) {
        total = cursor;
        driver_data = 0;
    } else {
        if (base) {
            left -= size;
            room = left;
        }
        const uint32_t at = cursor;
        const uint32_t aligned = (at + align + start - 1) / align * align;
        total = at + size;
        cursor = total;
        driver_data = base ? aligned : 0;
    }
    if (base && request_records && device_records && file_records && driver_data && !failed) {
        title_global<uint32_t>(0x00920824) = driver_data;
        title_global<uint32_t>(0x00920810) = request_records;
        title_global<uint32_t>(0x00920818) = request_nodes;
        title_global<uint32_t>(0x00920828) = data_size;
        title_global<uint32_t>(0x0092081c) = device_records;
        title_global<uint32_t>(0x00920820) = file_records;
        for (int32_t i = 0; i < title_global<int32_t>(0x008bfe7c); ++i) {
            uint8_t* const device = title_global<uint8_t* const*>(0x008bfe80)[i];
            const uint32_t kind = *pointer_at<const uint32_t>(device, 4);
            if (kind != 2 && kind != 3 && kind != 4) continue;
            uint32_t* const buffer = pointer_at<uint32_t>(device, 0x14);
            if (!buffer[4]) buffer[4] = buffers[i];
        }
    }
    return total;
}

uint32_t XB_CDECL File_Configure(const uint32_t* config) {
    if (config) {
        title_global<uint32_t>(0x008bfd70) = config[0];
        title_global<uint32_t>(0x008bfd74) = config[1];
        title_global<uint32_t>(0x008bfd78) = config[2];
        title_global<uint32_t>(0x008bfd7c) = config[3];
        title_global<uint32_t>(0x008bfd80) = config[4];
    }
    using Setup = void XB_CDECL(uint8_t*);
    for (int32_t i = 0; i < title_global<int32_t>(0x008bfe7c); ++i) {
        const uint32_t kind = title_global<uint32_t>(0x008bfd7c);
        if (kind != 0xffffffffu) *pointer_at<uint32_t>(title_global<uint8_t* const*>(0x008bfe80)[i], 4) = kind;
        uint8_t* const device = title_global<uint8_t* const*>(0x008bfe80)[i];
        const uint32_t setup = pointer_at<const uint32_t>(device, 4)[1];
        if (setup) title_function<Setup>(setup)(title_global<uint8_t* const*>(0x008bfe80)[i]);
    }
    return File_LayoutMemory(nullptr);
}

PLATFORM_ASM_ENTRY(FileDevice_Poll_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FileDevice_Poll) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL FileDevice_Poll(uint8_t* device) {
    // Device: ops +0x10 {+4 cancel(arg +0x2c), +8 poll}, transfer done +0x1c, cancel argument +0x2c,
    // request +0x30, phase +0x40 (0 idle, 1 cancelling, 2 reading, 3 cancelled, 4 failed, 5 aborting).
    if (!word_at(device, 0x40)) return 1;
    const uint32_t poll = pointer_at<const uint32_t>(device, 0x10)[2];
    if (poll && title_function<uint32_t XB_CDECL()>(poll)()) return 0;
    uint8_t* const request = file_request(static_cast<int32_t>(word_at(device, 0x30)));
    if (request) {
        switch (word_at(device, 0x40)) {
        case 1:
            if (word_at(request, 0x2c) == 3) {
                const uint32_t cancel = pointer_at<const uint32_t>(device, 0x10)[1];
                if (cancel && title_function<uint32_t XB_CDECL(uint32_t)>(cancel)(word_at(device, 0x2c))) return 0;
                word_at(device, 0x40) = 3;
            }
            return 0;
        case 2:
            if (word_at(request, 0x2c) == 3) {
                word_at(request, 0x2c) = 4;
            } else if (word_at(device, 0x1c)) {
                word_at(request, 0x34) += word_at(device, 0x1c);
                word_at(request, 0x2c) = word_at(request, 0x34) < word_at(request, 0x28) ? 0 : 2;
            } else {
                word_at(request, 0x2c) = 2;
            }
            break;
        case 4:
            word_at(request, 0x2c) = 4;
            break;
        case 5:
            if (word_at(request, 0x2c) != 7) word_at(request, 0x2c) = 6;
            return 0;
        default:
            return 0;
        }
    }
    word_at(device, 0x40) = 0;
    word_at(device, 0x34) = 0;
    word_at(device, 0x38) = 0;
    word_at(device, 0x2c) = 0;
    word_at(device, 0x28) = 0;
    word_at(device, 0x30) = 0xffffffffu;
    word_at(device, 0x3c) = 0xffffffffu;
    return 1;
}

int32_t XB_CDECL File_PickNextRequest(const uint8_t* device) {
    // The queued request (state 0) with the highest priority (+0x20), the oldest (+0x30) among equals;
    // with a device, only requests whose file (through an alias) uses that driver (+8). -1 when none.
    int32_t best = -1;
    uint32_t best_priority = 0;
    uint32_t best_sequence = 0xffffffffu;
    const uint8_t* const pool = pool_at(kRequestPool);
    int32_t handle = HandlePool_First(pool);
    if (handle == -1) return -1;
    do {
        const int32_t current = handle;
        const uint8_t* const request = file_request(handle);
        handle = HandlePool_Next(pool, handle);
        if (word_at(request, 0x2c)) continue;
        if (device) {
            const int32_t file = static_cast<int32_t>(word_at(request, 0xc));
            const uint8_t* record = file_record(file);
            int32_t target = -1;
            if (record) target = word_at(record, 0x20) == 1 ? static_cast<int32_t>(word_at(record, 8)) : file;
            record = file_record(target);
            const uint32_t driver = record ? word_at(record, 8) : 0;
            if (driver != addr32(device)) continue;
        }
        const uint32_t priority = word_at(request, 0x20);
        if (priority > best_priority) {
            best_priority = priority;
            best_sequence = word_at(request, 0x30);
        } else if (priority != best_priority || !(word_at(request, 0x30) < best_sequence)) {
            continue;
        } else {
            best_sequence = word_at(request, 0x30);
        }
        best = current;
    } while (handle != -1);
    return best;
}

PLATFORM_ASM_ENTRY(File_StartDeviceRequest_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(File_StartDeviceRequest) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL File_StartDeviceRequest(int32_t handle, uint8_t* device) {
    // Builds the I/O block {file size, position (+0), driver data, request, +0x24, bytes left (+0x28), +4},
    // each past the bytes done (+0x34), for the rest of the request (an alias maps the offset into its file: base +4, chunk +0xc, stride +0x10)
    // and hands it to FileDevice_IssueIo; the request is busy (1), or failed to start (6).
    if (word_at(device, 0x40)) return 5;
    uint8_t* const request = file_request(handle);
    const uint8_t* const record = file_record(static_cast<int32_t>(word_at(request, 0xc)));
    const uint32_t done = word_at(request, 0x34);
    uint32_t io[7];
    io[6] = word_at(request, 4);
    io[4] = word_at(request, 0x24) + done;
    io[5] = word_at(request, 0x28) - done;
    io[1] = word_at(request, 0) + done;
    io[3] = static_cast<uint32_t>(handle);
    io[2] = addr32(File_GetDriverData(static_cast<int32_t>(word_at(request, 0xc))));
    io[0] = word_at(record, 0);
    if (word_at(record, 0x20) == 1) {
        HandlePool_Resolve(pool_at(kFilePool), static_cast<int32_t>(word_at(record, 8)));
        uint32_t chunk = word_at(record, 0xc);
        uint32_t stride = word_at(record, 0x10);
        if (!chunk && !stride) {
            stride = 0x40000000;
            chunk = stride;
        }
        const uint32_t chunk_index = done / chunk;
        const uint32_t within = done % chunk;
        if (io[5] > chunk - within) io[5] = chunk - within;
        io[1] = chunk_index * stride + word_at(record, 4) + within;
        io[2] = addr32(File_GetDriverData(static_cast<int32_t>(word_at(record, 8))));
    }
    word_at(request, 0x2c) = 1;
    const uint32_t result = title_function<uint32_t XB_CDECL(uint8_t*, uint32_t*)>(0x000cb8d0)(device, io);  // FileDevice_IssueIo
    if (result) word_at(request, 0x2c) = 6;
    return result;
}

uint32_t XB_CDECL File_PumpDevices() {
    // Polls every device, picks the next request for each idle one (any device's request when there is
    // only one), starts them; 1 while any device is still busy.
    const int32_t count = title_global<int32_t>(0x008bfe7c);
    uint8_t* const* const devices = title_global<uint8_t* const*>(0x008bfe80);
    uint32_t busy = 0;
    uint32_t idle[32];
    int32_t next[32];
    for (int32_t i = 0; i < count; ++i) idle[i] = FileDevice_Poll(devices[i]);
    for (int32_t i = 0; i < count; ++i) {
        if (idle[i]) {
            next[i] = File_PickNextRequest(count == 1 ? nullptr : devices[i]);
        } else {
            next[i] = -1;
        }
    }
    for (int32_t i = 0; i < count; ++i) {
        if (next[i] != -1) File_StartDeviceRequest(next[i], devices[i]);
    }
    for (int32_t i = 0; i < count; ++i) {
        if (word_at(devices[i], 0x40)) busy = 1;
    }
    return busy;
}

uint32_t XB_CDECL File_PumpRequests() {
    // Under the file mutex (threaded): requests done (2) or failed (4) get a completion record
    // {status, callback +0x10, handle +0x40, argument +0x14, free = 1} in the list 0x00920818 (count
    // 0x00920814); aborting ones (6) become aborted (7) and report status 3 if they have a callback, but
    // stay allocated. The callbacks run with the mutex released, then the finished requests are freed.
    // Unthreaded, the devices are pumped here (tail call).
    title_global<int32_t>(0x00920814) = 0;
    if (file_threaded()) file_lock();
    uint8_t* const pool = &title_global<uint8_t>(kRequestPool);
    int32_t handle = HandlePool_First(pool);
    while (handle != -1) {
        uint8_t* const request = file_request(handle);
        handle = HandlePool_Next(pool, handle);
        const uint32_t state = word_at(request, 0x2c);
        if (state == 0) {
            Snd_UpdateTime();
        } else if (state == 2 || state == 4) {
            const int32_t slot = title_global<int32_t>(0x00920814);
            uint32_t* const entry = title_global<uint32_t*>(0x00920818) + slot * 5;
            title_global<int32_t>(0x00920814) = slot + 1;
            entry[0] = static_cast<uint32_t>(File_RequestStateToStatus(state));
            entry[2] = word_at(request, 0x40);
            entry[1] = word_at(request, 0x10);
            entry[3] = word_at(request, 0x14);
            entry[4] = 1;
        } else if (state == 6) {
            word_at(request, 0x2c) = 7;
            if (word_at(request, 0x10)) {
                const int32_t slot = title_global<int32_t>(0x00920814);
                uint32_t* const entry = title_global<uint32_t*>(0x00920818) + slot * 5;
                title_global<int32_t>(0x00920814) = slot + 1;
                entry[0] = static_cast<uint32_t>(File_RequestStateToStatus(7));
                entry[2] = word_at(request, 0x40);
                entry[1] = word_at(request, 0x10);
                entry[3] = word_at(request, 0x14);
                entry[4] = 0;
            }
        }
    }
    using Callback = void XB_CDECL(uint32_t, uint32_t, uint32_t);
    if (title_global<int32_t>(0x00920814)) {
        if (file_threaded()) file_release();
        for (int32_t i = 0; i < title_global<int32_t>(0x00920814); ++i) {
            const uint32_t* const entry = title_global<uint32_t*>(0x00920818) + i * 5;
            if (entry[1]) title_function<Callback>(entry[1])(entry[0], entry[2], entry[3]);
        }
        if (file_threaded()) file_lock();
    }
    for (int32_t i = 0; i < title_global<int32_t>(0x00920814); ++i) {
        const uint32_t* const entry = title_global<uint32_t*>(0x00920818) + i * 5;
        if (entry[4]) HandlePool_Free(pool, static_cast<int32_t>(entry[2]));
    }
    title_global<int32_t>(0x00920814) = 0;
    if (file_threaded()) file_release();
    if (title_global<int32_t>(0x008bfd80) == 0) return File_PumpDevices();
    return 0;
}

uint32_t XB_CDECL File_GetNativeHandle(int32_t handle, uint32_t* out_info, uint32_t* out_offset, uint32_t* out_size,
                                       uint32_t* out_stride) {
    // The driver's native handle (file ops +0x14: (data, &handle, &info) == 0) and the file's offset
    // into it (an alias: base +4, stride +0x10), its size; 0 when anything is missing.
    const uint8_t* record = file_record(handle);
    if (!record) return 0;
    const uint32_t size = word_at(record, 0);
    uint32_t offset = 0;
    uint32_t stride = 0;
    if (word_at(record, 0x20) == 1) {
        handle = static_cast<int32_t>(word_at(record, 8));
        offset = word_at(record, 4);
        stride = word_at(record, 0x10);
        record = file_record(handle);
        if (!record) return 0;
    }
    uint8_t* const data = File_GetDriverData(handle);
    if (!data) return 0;
    const uint8_t* const driver = pointer_at<const uint8_t>(record, 8);
    if (!driver) return 0;
    const uint32_t* const ops = pointer_at<const uint32_t>(driver, 0xc);
    if (!ops || !ops[5]) return 0;
    uint32_t native = 0;
    uint32_t info = 0;
    if (title_function<uint32_t XB_CDECL(uint8_t*, uint32_t*, uint32_t*)>(ops[5])(data, &native, &info)) return 0;
    if (out_info) *out_info = info;
    if (out_offset) *out_offset = offset;
    if (out_size) *out_size = size;
    if (out_stride) *out_stride = stride;
    return native;
}

void XB_CDECL File_InitSystem(uint8_t* memory) {
    // Lays out the file system in `memory` (File_LayoutMemory), sets up the request, device and file pools,
    // the default device (priority 2) and, threaded, the mutex, event and request thread (0x000ca620).
    if (!memory) return;
    File_LayoutMemory(memory);
    HandlePool_Init(&title_global<uint8_t>(kRequestPool), title_global<uint8_t*>(0x00920810) + 0x38,
                    title_global<int32_t>(0x008bfd78), 0x44);
    HandlePool_Init(&title_global<uint8_t>(kDevicePool), title_global<uint8_t*>(0x0092081c),
                    title_global<int32_t>(0x008bfd74), 0x10);
    HandlePool_Init(&title_global<uint8_t>(kFilePool), title_global<uint8_t*>(0x00920820) + 0x14,
                    title_global<int32_t>(0x008bfd70), 0x24);
    const int32_t device = HandlePool_Alloc(&title_global<uint8_t>(kDevicePool));
    File_SetDevicePriority(static_cast<uint32_t>(device), 2);
    title_global<int32_t>(0x008bfd6c) = device;
    if (!file_threaded()) return;
    title_global<uint32_t>(0x00920784) =
        title_function<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x001e859d)(0, 0, 0);  // CreateMutexA
    title_global<uint32_t>(0x00920788) =
        title_function<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x001e851c)(0, 0, 0, 0);  // CreateEventA
    title_global<uint32_t>(0x00920780) = title_function<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                                          uint32_t)>(0x001e7896)(0, 0x2000, 0x000ca620, 0, 0,
                                                                                                 0);  // CreateThread
}

int32_t XB_CDECL Save_MountDevice() {
    // The selected memory unit (Save_GetPort; 8 = the hard disk, drive 'U') mounted with XMountMU; its
    // drive letter in 0x008c02d4 ('?' until then). -10 when the port has no memory unit (0x008c02e0 masks).
    title_global<char>(0x008c02d4) = '?';
    if (Save_GetPort() == 8) {
        title_global<char>(0x008c02d4) = 'U';
        return 0;
    }
    uint32_t slot;
    uint32_t controller;
    const int32_t result = Save_PortToDevice(Save_GetPort(), &controller, &slot);
    if (result) return result;
    if (Save_GetPort() < 0) return -10;
    const uint32_t devices = title_function<uint32_t XB_STDCALL(uint32_t)>(0x007ac250)(0x007aa028);  // XGetDevices
    if (!(title_global<uint32_t[8]>(0x008c02e0)[Save_GetPort()] & devices)) return -10;
    return Save_TranslateError(title_function<int32_t XB_STDCALL(uint32_t, uint32_t, char*)>(0x007ac7b1)(
        controller, slot, &title_global<char>(0x008c02d4)));  // XMountMUA
}

int32_t XB_CDECL Save_UnmountDevice() {
    // XUnmountMU of the selected memory unit: 0x55 -> -17, 0x70 -> -3, 0x48f -> -10, other errors -1.
    title_global<char>(0x008c02d4) = '?';
    if (Save_GetPort() == 8) return 0;
    uint32_t slot;
    uint32_t controller;
    const int32_t result = Save_PortToDevice(Save_GetPort(), &controller, &slot);
    if (result) return result;
    const int32_t error = title_function<int32_t XB_STDCALL(uint32_t, uint32_t)>(0x007ac8e3)(controller, slot);  // XUnmountMU
    if (error == 0) return 0;
    if (error == 0x55) return -17;
    if (error == 0x70) return -3;
    if (error == 0x48f) return -10;
    return -1;
}

uint32_t XB_CDECL Save_EnumSaveGames(uint8_t* entries) {
    // Lists the save games of the mounted device (root 0x008c02d8, its drive letter from 0x008c02d4) into
    // 0x4c-byte entries {name (ANSI, 0x40), blocks +0x40, last write in local time: second +0x44, minute,
    // hour, day, month +0x48, year (16 bits) +0x4a}. Returns the count, 0 when there are none, -1 on an
    // error; -17 (busy) during a save operation.
    if (title_global<uint32_t>(0x00920af4)) {
        Save_SetLastResult(0xffffffefu);
        return Save_GetLastResult();
    }
    uint32_t count = 0;
    Save_SetLastResult(static_cast<uint32_t>(Save_MountDevice()));
    if (Save_GetLastResult()) {
        static_cast<uint32_t>(Save_UnmountDevice());
        return 0xffffffffu;
    }
    char* const root = &title_global<char>(0x008c02d8);
    root[0] = title_global<char>(0x008c02d4);
    uint8_t find[0x344];  // XGAME_FIND_DATA: last write +0x14, directory +0x140, name (wide) +0x244
    const uint32_t search =
        title_function<uint32_t XB_STDCALL(const char*, uint8_t*)>(0x001e92ef)(root, find);  // XFindFirstSaveGame
    if (search == 0xffffffffu) {
        static_cast<uint32_t>(Save_UnmountDevice());
        return 0;
    }
    uint8_t* entry = entries;
    do {
        const uint16_t* const name = reinterpret_cast<const uint16_t*>(find + 0x244);
        const uint32_t length = title_function<uint32_t XB_CDECL(const uint16_t*)>(0x001f1343)(name);  // _wcslen
        title_function<int32_t XB_STDCALL(uint32_t, uint32_t, const uint16_t*, uint32_t, uint8_t*, uint32_t, uint32_t,
                                          uint32_t)>(0x001e975f)(0, 0, name, length + 1, entry, 0x40, 0,
                                                                 0);  // WideCharToMultiByte
        word_at(entry, 0x40) =
            title_function<uint32_t XB_STDCALL(const uint8_t*)>(0x001ea047)(find + 0x140);  // XGetDisplayBlocks
        uint32_t local_time[2];
        uint16_t date[8];  // SYSTEMTIME
        title_function<uint32_t XB_STDCALL(const uint8_t*, uint32_t*)>(0x001e75b4)(find + 0x14, local_time);  // FileTimeToLocalFileTime
        title_function<uint32_t XB_STDCALL(const uint32_t*, uint16_t*)>(0x001e7062)(local_time, date);  // FileTimeToSystemTime
        entry[0x44] = static_cast<uint8_t>(date[6]);
        entry[0x47] = static_cast<uint8_t>(date[3]);
        entry[0x45] = static_cast<uint8_t>(date[5]);
        entry[0x46] = static_cast<uint8_t>(date[4]);
        entry[0x48] = static_cast<uint8_t>(date[1]);
        *reinterpret_cast<uint16_t*>(entry + 0x4a) = date[0];
        ++count;
        entry += 0x4c;
    } while (title_function<uint32_t XB_STDCALL(uint32_t, uint8_t*)>(0x001e93f9)(search, find));  // XFindNextSaveGame
    if (!title_function<uint32_t XB_STDCALL(uint32_t)>(0x001e9440)(search)) {  // XFindClose
        Save_SetLastResult(0xffffffffu);
        static_cast<uint32_t>(Save_UnmountDevice());
        return 0xffffffffu;
    }
    Save_SetLastResult(static_cast<uint32_t>(Save_UnmountDevice()));
    if (Save_GetLastResult()) return 0xffffffffu;
    return count;
}

namespace {

uint32_t c_string_length(const char* text) {
    const char* end = text;
    while (*end) ++end;
    return static_cast<uint32_t>(end - text);
}

// The tail of the append family: `count` more bytes (count > 0), the buffer grown (std_string_Copy)
// when the capacity is short; returns the buffer the new bytes go to (null for an empty result, which
// the title cannot reach once count > 0).
uint8_t* string_grow_for_append(uint8_t* self, uint32_t count, uint32_t& new_size) {
    new_size = word_at(self, 0x14) + count;
    if (new_size > 0xfffffffeu) string_throw_length(self);
    if (word_at(self, 0x18) < new_size) {
        std_string_Copy(self, new_size, word_at(self, 0x14));
    } else if (!new_size) {
        word_at(self, 0x14) = 0;
        string_buffer(self)[0] = 0;
        return nullptr;
    }
    return new_size ? string_buffer(self) : nullptr;
}

void string_finish_append(uint8_t* self, uint32_t new_size) {
    word_at(self, 0x14) = new_size;
    string_buffer(self)[new_size] = 0;
}

}  // namespace

void XB_THISCALL std_string_Copy(uint8_t* self, uint32_t size, uint32_t keep) {
    // _Copy: a new buffer of size | 15 (or 1.5 times the capacity when that is larger and fits), the
    // first `keep` bytes moved over, the old heap buffer freed; size = keep. (Allocation failure retries
    // with the exact size in the title's catch block; Z_Malloc never throws.)
    uint32_t capacity = size | 0xf;
    if (capacity > 0xfffffffeu) {
        capacity = size;
    } else {
        const uint32_t old = word_at(self, 0x18);
        const uint32_t half = old >> 1;
        if (capacity / 3 < half && !(old > 0xfffffffeu - half)) capacity = half + old;
    }
    uint8_t* const data = Z_Malloc(capacity + 1);
    if (keep) platform_rep_movs(data, string_buffer(self), keep);
    if (word_at(self, 0x18) >= 0x10) Z_Free(pointer_at<uint8_t>(self, 4));
    self[4] = 0;
    word_at(self, 4) = addr32(data);
    word_at(self, 0x18) = capacity;
    word_at(self, 0x14) = keep;
    (capacity < 0x10 ? self + 4 : data)[keep] = 0;
}

uint8_t* XB_THISCALL std_string_Erase(uint8_t* self, uint32_t pos, uint32_t count) {
    if (word_at(self, 0x14) < pos) string_throw_range();
    const uint32_t tail = word_at(self, 0x14) - pos;
    if (tail < count) count = tail;
    if (count) {
        uint8_t* const data = string_buffer(self);
        title_function<void* XB_CDECL(void*, const void*, uint32_t)>(0x001ef0c0)(data + pos, data + pos + count,
                                                                                tail - count);  // _memmove
        word_at(self, 0x14) = word_at(self, 0x14) - count;
        string_buffer(self)[word_at(self, 0x14)] = 0;
    }
    return self;
}

uint8_t* XB_THISCALL std_string_AssignN(uint8_t* self, const char* text, uint32_t count) {
    const uint8_t* const source = reinterpret_cast<const uint8_t*>(text);
    if (!(source < string_buffer(self)) && string_buffer(self) + word_at(self, 0x14) > source) {
        return std_string_AssignSubstr(self, self, static_cast<uint32_t>(source - string_buffer(self)), count);
    }
    if (count > 0xfffffffeu) string_throw_length(self);
    if (word_at(self, 0x18) < count) {
        std_string_Copy(self, count, word_at(self, 0x14));
    } else if (!count) {
        word_at(self, 0x14) = 0;
        string_buffer(self)[0] = 0;
        return self;
    }
    if (count) {
        platform_rep_movs(string_buffer(self), source, count);
        word_at(self, 0x14) = count;
        string_buffer(self)[count] = 0;
    }
    return self;
}

uint8_t* XB_THISCALL std_string_CtorCStr(uint8_t* self, const char* text) {
    string_init_empty(self);
    std_string_AssignN(self, text, c_string_length(text));
    return self;
}

uint8_t* XB_REGS2 XB_STDCALL std_string_AssignCStr(uint32_t, const char* text, uint8_t* self) {
    return std_string_AssignN(self, text, c_string_length(text));
}

uint8_t* XB_REGS3 XB_STDCALL std_string_AppendCStr(uint32_t, uint32_t, const char* text, uint8_t* self) {
    return std_string_AppendSubstr(c_string_length(text), self, text);
}

PLATFORM_ASM_ENTRY(std_string_AppendNChar_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(std_string_AppendNChar) "addl $12, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL std_string_AppendNChar(uint8_t* self, uint32_t count, uint32_t ch) {
    if (!(0xffffffffu - word_at(self, 0x14) > count)) string_throw_length(self);
    if (!count) return self;
    uint32_t new_size;
    uint8_t* const data = string_grow_for_append(self, count, new_size);
    if (!data) return self;
    uint8_t* const fill = data + word_at(self, 0x14);
    for (uint32_t i = 0; i < count; ++i) fill[i] = static_cast<uint8_t>(ch);
    string_finish_append(self, new_size);
    return self;
}

uint32_t XB_REGS1 XB_STDCALL std_string_Find(uint32_t pos, const uint8_t* self, const char* text, uint32_t count) {
    // find(text, pos, count): memchr for the first character, then a compare; the index or -1.
    if (!count && !(pos > word_at(self, 0x14))) return pos;
    const uint32_t size = word_at(self, 0x14);
    if (!(pos < size)) return 0xffffffffu;
    uint32_t left = size - pos;
    if (count > left) return 0xffffffffu;
    left += 1 - count;
    const uint8_t* const data = word_at(self, 0x18) >= 0x10 ? pointer_at<const uint8_t>(self, 4) : self + 4;
    const uint8_t* at = data + pos;
    using Memchr = const uint8_t* XB_CDECL(const uint8_t*, int32_t, uint32_t);
    const uint8_t* found;
    while ((found = title_function<Memchr>(0x001efba0)(at, static_cast<signed char>(text[0]), left)) != nullptr) {  // _memchr
        uint32_t same = 0;
        while (same < count && found[same] == static_cast<uint8_t>(text[same])) ++same;
        if (same == count) {
            const uint8_t* const base = word_at(self, 0x18) >= 0x10 ? pointer_at<const uint8_t>(self, 4) : self + 4;
            return static_cast<uint32_t>(found - base);
        }
        left = left + static_cast<uint32_t>(at - found) - 1;
        at = found + 1;
    }
    return 0xffffffffu;
}

uint8_t* XB_REGS3 XB_CDECL std_string_AppendSubstr(uint32_t count, uint8_t* self, const char* text) {
    // append(text, count); text inside the string goes through std_string_AppendRange.
    const uint8_t* const source = reinterpret_cast<const uint8_t*>(text);
    if (!(source < string_buffer(self)) && string_buffer(self) + word_at(self, 0x14) > source) {
        return std_string_AppendRange(self, self, count, static_cast<uint32_t>(source - string_buffer(self)));
    }
    if (!(0xffffffffu - word_at(self, 0x14) > count)) string_throw_length(self);
    if (!count) return self;
    uint32_t new_size;
    uint8_t* const data = string_grow_for_append(self, count, new_size);
    if (!data) return self;
    platform_rep_movs(data + word_at(self, 0x14), source, count);
    string_finish_append(self, new_size);
    return self;
}

PLATFORM_ASM_ENTRY(std_string_AppendRange_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(std_string_AppendRange) "addl $16, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL std_string_AppendRange(uint8_t* self, const uint8_t* source, uint32_t count, uint32_t pos) {
    // append(source, pos, count).
    if (word_at(source, 0x14) < pos) string_throw_range();
    const uint32_t tail = word_at(source, 0x14) - pos;
    if (tail < count) count = tail;
    if (!(0xffffffffu - word_at(self, 0x14) > count)) string_throw_length(self);
    if (!count) return self;
    uint32_t new_size;
    uint8_t* const data = string_grow_for_append(self, count, new_size);
    if (!data) return self;
    const uint8_t* const from = word_at(source, 0x18) >= 0x10 ? pointer_at<const uint8_t>(source, 4) : source + 4;
    platform_rep_movs(data + word_at(self, 0x14), from + pos, count);
    string_finish_append(self, new_size);
    return self;
}

int32_t XB_REGS3 XB_STDCALL std_string_CompareSubstr(uint32_t count, uint32_t, const uint8_t* self, uint32_t pos,
                                                     const uint8_t* text, uint32_t text_count) {
    // compare(pos, count, text, text_count): bytes unsigned, then the lengths; -1, 0 or 1.
    if (word_at(self, 0x14) < pos) string_throw_range();
    const uint32_t tail = word_at(self, 0x14) - pos;
    if (tail < count) count = tail;
    if (count) {
        const uint32_t n = count >= text_count ? text_count : count;
        const uint8_t* const data = (word_at(self, 0x18) >= 0x10 ? pointer_at<const uint8_t>(self, 4) : self + 4) + pos;
        for (uint32_t i = 0; i < n; ++i) {
            if (data[i] != text[i]) return data[i] < text[i] ? -1 : 1;
        }
    }
    if (count < text_count) return -1;
    return count != text_count;
}

uint8_t* XB_THISCALL std_string_ConstructCopy(uint8_t* self, const uint8_t* source) {
    if (!self) return nullptr;
    string_init_empty(self);
    return std_string_AssignSubstr(self, source, 0, 0xffffffffu);
}

uint8_t* XB_THISCALL std_exception_CtorString(uint8_t* self, const uint8_t* message) {
    // logic_error(message): the exception base, vtable 0x007f6748, the message copied to +0xc.
    std_exception_Ctor(reinterpret_cast<uint32_t*>(self));
    word_at(self, 0) = 0x007f6748;
    string_init_empty(self + 0xc);
    std_string_AssignSubstr(self + 0xc, message, 0, 0xffffffffu);
    return self;
}

}  // namespace game
