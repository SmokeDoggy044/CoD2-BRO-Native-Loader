#include "qcommon_core.h"

#include <cstring>

#include "lanes.h"
#include "qcommon.h"
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

}  // namespace game
