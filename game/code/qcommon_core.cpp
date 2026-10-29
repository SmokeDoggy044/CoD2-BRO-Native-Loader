#include "qcommon_core.h"

#include <cstring>

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

}  // namespace game
