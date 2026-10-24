#include "fx_cmd.h"

#include <cstring>

#include "dobj.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kAlloc = 0x002a5a40;   // FXCmd_Alloc
constexpr uint32_t kCommit = 0x00298e90;  // FXCmd_Commit
constexpr uint32_t kFlushSpawnQueue = 0x002a5940;
constexpr uint32_t kDefaultScriptString = 0x00971908;

uint32_t& word(uint8_t* p, uint32_t offset) { return *reinterpret_cast<uint32_t*>(p + offset); }
uint32_t word(const uint8_t* p, uint32_t offset) { return *reinterpret_cast<const uint32_t*>(p + offset); }
uint16_t& half(uint8_t* p, uint32_t offset) { return *reinterpret_cast<uint16_t*>(p + offset); }
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint8_t* pointer(uint32_t address) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); }

uint8_t* queue_slot(uint8_t* list, uint32_t index) { return list + 0x920 + index * 0x210; }

// Stack argument `n` (1-based) of a builder.
uint32_t arg(uint32_t entry, uint32_t n) { return *reinterpret_cast<const uint32_t*>(pointer(entry + 4 * n)); }

void script_assign(uint8_t* cmd, ScriptString* temp) {
    using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
    title_function<Assign>(0x0011d600)(reinterpret_cast<ScriptString*>(cmd + 0x1fc), temp);  // ScriptString_Assign
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(temp);                     // ScriptString_Release
}

// The command's script string (+0x1fc) = the default one, through a temporary at `temp`.
void script_default(uint8_t* cmd, ScriptString* temp) {
    ScriptString_Copy(temp, &title_global<ScriptString>(kDefaultScriptString));
    script_assign(cmd, temp);
}

// The command's script string = `text`.
void script_text(uint8_t* cmd, ScriptString* temp, const char* text) {
    title_function<void XB_THISCALL(ScriptString*, const char*)>(0x0011eb50)(temp, text);  // ScriptString_FromCString
    script_assign(cmd, temp);
}

// A builder's frame: Alloc and Commit run where the original calls them (FXCmd_Alloc may
// flush the queue, whose callees store stack addresses).
struct Builder {
    uint32_t top;   // the original's ESP at its FXCmd_Alloc / FXCmd_Commit calls
    uint8_t* list;  // 0x0095f5b4
    uint8_t* cmd = nullptr;

    Builder(uint32_t top_, const void* entity, int32_t handle)
        : top(top_), list(title_global<uint8_t*>(0x0095f5b4)) {
        const TitleRegs regs{address_of(entity), address_of(list), 0, 0, address_of(list), 0};
        const uint32_t args[2] = {address_of(entity), static_cast<uint32_t>(handle)};
        title_call_at_regs(top, kAlloc, &regs, 2, args);
        cmd = title_global<uint8_t*>(address_of(list) + 0x8d20);
    }
    void kind(uint32_t value) const {
        word(cmd, 0) |= 1;
        word(cmd, 0xc) &= ~1u;
        word(cmd, 0x1b4) = value;
    }
    // Sets a field bit: +0 set, +0xc (override) clear.
    void set(uint32_t bit) const {
        word(cmd, 0) |= bit;
        word(cmd, 0xc) &= ~bit;
    }
    // Sets a field bit in both words.
    void set_both(uint32_t bit) const {
        word(cmd, 0) |= bit;
        word(cmd, 0xc) |= bit;
    }
    // 127 characters of `name` (strncpy, zero padded) at +offset, 128 bytes.
    void name(uint32_t offset, const char* text) const {
        char buffer[0x80];
        title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(buffer, text, 0x7f);  // _strncpy
        buffer[0x7f] = 0;
        std::memcpy(cmd + offset, buffer, sizeof buffer);
    }
    // Flag 4 and the 12-word transform at +0x1c0; with `scale`, flag 8 and its word 8 at +0x1b8.
    void transform(const uint8_t* t, bool scale) const {
        cmd[0x20c] |= 4;
        std::memcpy(cmd + 0x1c0, t, 0x30);
        if (scale) {
            set_both(8);
            word(cmd, 0x1b8) = word(t, 0x20);
        }
    }
    uint32_t commit() const {
        const TitleRegs regs{0, address_of(list), 0, 0, address_of(list), 0};
        return title_call_at_regs(top, kCommit, &regs, 0, nullptr);
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------
// Queue helpers

PLATFORM_ASM_ENTRY(FxSpawnCmd_Copy_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(FxSpawnCmd_Copy) "addl $8, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL FxSpawnCmd_Copy(uint8_t* dst, const uint8_t* src) {
    std::memcpy(dst, src, 0x1bc);
    std::memcpy(dst + 0x1c0, src + 0x1c0, 0x30);
    word(dst, 0x1f0) = word(src, 0x1f0);
    word(dst, 0x1f4) = word(src, 0x1f4);
    word(dst, 0x1f8) = word(src, 0x1f8);
    using Assign = void XB_THISCALL(uint8_t*, const uint8_t*);
    title_function<Assign>(0x0011d600)(dst + 0x1fc, src + 0x1fc);  // ScriptString_Assign
    word(dst, 0x200) = word(src, 0x200);
    word(dst, 0x204) = word(src, 0x204);
    word(dst, 0x208) = word(src, 0x208);
    std::memcpy(dst + 0x20c, src + 0x20c, 2);
    std::memcpy(dst + 0x20e, src + 0x20e, 2);
    return dst;
}

uint8_t* XB_REGS3 XB_CDECL FxInstance_Init(uint8_t* effect, uint32_t, uint32_t handle) {
    word(effect, 0x44) = handle;
    uint32_t& next_id = title_global<uint32_t>(0x0095f5b8);
    next_id += 1;
    word(effect, 0x4c) = next_id;
    word(effect, 0) = 0;
    word(effect, 0x48) = 0;
    word(effect, 0x50) = 2;
    if (word(effect, 0x4c) == 0) word(effect, 0x4c) = 1;
    return effect;
}

// Two pushes before its FX_FlushSpawnQueue call.
PLATFORM_ASM_ENTRY(FXCmd_Alloc_entry,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "leal 4(%ebp), %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FXCmd_Alloc_body) "movl %ebp, %esp\n"
                   "popl %ebp\n"
                   "ret $8")

uint8_t* XB_CDECL FXCmd_Alloc_body(uint32_t entry, uint8_t* list, const uint8_t* entity, int32_t handle) {
    int32_t& count = *reinterpret_cast<int32_t*>(list + 0x910);
    uint8_t*& current = *reinterpret_cast<uint8_t**>(list + 0x8d20);
    current = nullptr;
    if (count >= 0x40 && count > 0) {
        int32_t i = 0;
        while (word(queue_slot(list, static_cast<uint32_t>(i)), 0x1b4) != 0) {
            if (++i >= count) break;
        }
        if (i < count) {
            const int32_t n = count;
            if (n > 1 && i < n) {
                const int32_t last = n - 1 <= 0 ? 0 : n - 1;
                FxSpawnCmd_Copy(queue_slot(list, static_cast<uint32_t>(i)),
                                queue_slot(list, static_cast<uint32_t>(last)));
            }
            if (count != 0) count -= 1;
        }
    }
    if (count >= 0x40) {
        const TitleRegs regs{address_of(list), address_of(list), 0, 0, address_of(list), 0};
        title_call_at_regs(entry - 8, kFlushSpawnQueue, &regs, 0, nullptr);
    }
    uint8_t* const slot = queue_slot(list, static_cast<uint32_t>(count));
    word(slot, 0x8) = 0;
    word(slot, 0x4) = 0;
    word(slot, 0x0) = 0;
    word(slot, 0x14) = 0;
    word(slot, 0x10) = 0;
    word(slot, 0xc) = 0;
    word(slot, 0x1f0) = 0;
    word(slot, 0x1f8) = 0;
    word(slot, 0x204) = 0xffffffffu;
    half(slot, 0x20e) = 0xffff;
    word(slot, 0x208) = 0xffffffffu;
    half(slot, 0x20c) = 0;
    word(slot, 0x200) = 0;
    const int32_t index = count;
    count = index + 1;
    uint8_t* const cmd = queue_slot(list, static_cast<uint32_t>(index));
    current = cmd;
    word(cmd, 0x1f0) = word(entity, 0x224);
    uint32_t owner = static_cast<uint32_t>(handle);
    if (owner == 0xffffffffu) {
        owner = word(entity, 0x220);
        if (owner == 0xffffffffu) owner = word(title_global<const uint8_t*>(0x0095f638), 0x34);
    }
    word(current, 0x1f4) = owner;
    current[0x20c] &= 0xfb;
    word(current, 0x1f8) = 0;
    return current;
}

PLATFORM_ASM_ENTRY(FXCmd_Commit_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(FXCmd_Commit_body) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL FXCmd_Commit_body(uint8_t* list) {
    uint8_t*& current = *reinterpret_cast<uint8_t**>(list + 0x8d20);
    if (!current) return 0;
    const uint32_t handle = word(current, 0x1f0);  // the entity handle
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8) ||
        title_global<uint32_t>(0x0097a968 + index * 8) == 0) {
        int32_t& count = *reinterpret_cast<int32_t*>(list + 0x910);
        if (count != 0) count -= 1;
        current = nullptr;
        return 0;
    }
    uint32_t& instances = word(list, 0);
    if (instances == 0x200) return 0;
    using HeapAlloc = uint8_t* XB_THISCALL(uint32_t, uint32_t, uint32_t);
    uint8_t* effect = title_function<HeapAlloc>(0x00044850)(title_global<uint32_t>(0x0095f5bc), 0x54, 0);  // SmallHeap_Alloc
    if (effect) effect = FxInstance_Init(effect, 0, word(current, 0x1f4));
    // A failed allocation reads address 0x4c, as the original does.
    const uint32_t id_address = address_of(effect) + 0x4c;
    word(current, 0x1f8) = title_global<uint32_t>(id_address);
    if (static_cast<int32_t>(instances) < 0x200) {
        word(list, 4 + instances * 4) = address_of(effect);
        instances += 1;
    }
    current = nullptr;
    return title_global<uint32_t>(id_address);
}

void XB_STDCALL FXParam_Broadcast(const uint8_t* list, uint32_t id, uint32_t a, uint32_t b) {
    using Slot = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
    for (uint32_t i = 0; i < word(list, 0); ++i) {
        const uint8_t* effect = pointer(word(list, 4 + i * 4));
        if (word(effect, 0x4c) != id) continue;
        for (uint32_t c = 0; c < word(effect, 0); ++c) {
            uint8_t* component = pointer(word(effect, 4 + c * 4));
            const uint32_t vtable = word(component, 0);
            title_function<Slot>(word(pointer(vtable), 8))(component, a, b);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Builders: entry = the entry ESP; the body gets (entry, EAX, ECX).

#define FXCMD_ENTRY(name)                                                                 \
    PLATFORM_ASM_ENTRY(name##_entry, "pushl %ebp\n"                                         \
                                     "movl %esp, %ebp\n"                                    \
                                     "leal 4(%ebp), %edx\n"                                 \
                                     "subl $0x20000, %esp\n"                                \
                                     "pushl %ecx\n"                                         \
                                     "pushl %eax\n"                                         \
                                     "pushl %edx\n" PLATFORM_ASM_CALL(name##_body) "movl %ebp, %esp\n" \
                                     "popl %ebp\n"                                          \
                                     "ret")

FXCMD_ENTRY(FXCmd_Type9)
uint32_t XB_CDECL FXCmd_Type9_body(uint32_t entry, uint8_t* entity, uint32_t) {
    const Builder b(entry - 4, entity, -1);
    b.kind(9);
    b.set(0x200);
    word(b.cmd, 0x128) = arg(entry, 1);
    script_default(b.cmd, reinterpret_cast<ScriptString*>(pointer(entry + 4)));  // in the argument slot
    word(b.cmd, 0x208) = 0xffffffffu;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_Type8AtTag)
uint32_t XB_CDECL FXCmd_Type8AtTag_body(uint32_t entry, uint8_t* entity, uint32_t tag) {
    const DObj* model = reinterpret_cast<const DObj*>(pointer(word(entity, 0x22c)));
    if (!model) return 0;
    const int32_t bone = DObj_GetTagIndex(model, tag);
    if (bone == -1) return 0;
    const Builder b(entry - 0x94, entity, -1);
    b.kind(8);
    b.name(0xa8, reinterpret_cast<const char*>(pointer(arg(entry, 1))));
    b.set(0x100);
    b.set(0x200);
    word(b.cmd, 0x128) = arg(entry, 2);
    b.transform(pointer(arg(entry, 3)), true);
    word(b.cmd, 0x204) = static_cast<uint32_t>(bone);
    ScriptString temp;
    script_default(b.cmd, &temp);
    word(b.cmd, 0x208) = 0xffffffffu;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_Type8)
uint32_t XB_CDECL FXCmd_Type8_body(uint32_t entry, uint8_t* entity, uint32_t) {
    const Builder b(entry - 0x90, entity, -1);
    b.kind(8);
    b.name(0xa8, reinterpret_cast<const char*>(pointer(arg(entry, 1))));
    b.set(0x100);
    b.set(0x200);
    word(b.cmd, 0x128) = arg(entry, 2);
    ScriptString temp;
    script_default(b.cmd, &temp);
    word(b.cmd, 0x208) = 0xffffffffu;
    return b.commit();
}

namespace {

uint32_t named_at_transform(uint32_t entry, uint8_t* entity, uint32_t kind) {
    const Builder b(entry - 0x94, entity, -1);
    b.kind(kind);
    b.name(0x18, reinterpret_cast<const char*>(pointer(arg(entry, 1))));
    b.set(0x80);
    ScriptString temp;
    script_default(b.cmd, &temp);
    b.transform(pointer(arg(entry, 2)), true);
    word(b.cmd, 0x208) = 0xffffffffu;
    return b.commit();
}

}  // namespace

FXCMD_ENTRY(FXCmd_Type7AtTransform)
uint32_t XB_CDECL FXCmd_Type7AtTransform_body(uint32_t entry, uint8_t* entity, uint32_t) {
    return named_at_transform(entry, entity, 7);
}

FXCMD_ENTRY(FXCmd_Type6AtTransform)
uint32_t XB_CDECL FXCmd_Type6AtTransform_body(uint32_t entry, uint8_t* entity, uint32_t) {
    return named_at_transform(entry, entity, 6);
}

FXCMD_ENTRY(FXCmd_Type5AtTransform)
uint32_t XB_CDECL FXCmd_Type5AtTransform_body(uint32_t entry, uint8_t* entity, uint32_t) {
    const uint8_t* const transform = pointer(arg(entry, 2));
    const Builder b(entry - 0x10, entity, -1);
    b.kind(5);
    b.set(0x400);
    word(b.cmd, 0x12c) = arg(entry, 1);
    script_default(b.cmd, reinterpret_cast<ScriptString*>(pointer(entry + 8)));  // in the argument slot
    word(b.cmd, 0x208) = 0;
    b.transform(transform, true);
    return b.commit();
}

namespace {

// Kind 4 with the name at +0x18 and the value at +0x128.
Builder weapon_sound(uint32_t top, const void* entity, const char* name, uint32_t value) {
    Builder b(top, entity, -1);
    b.kind(4);
    b.name(0x18, name);
    b.set(0x80);
    b.set(0x200);
    word(b.cmd, 0x128) = value;
    ScriptString temp;
    script_default(b.cmd, &temp);
    return b;
}

}  // namespace

FXCMD_ENTRY(FXCmd_PlayWeaponSound)
uint32_t XB_CDECL FXCmd_PlayWeaponSound_body(uint32_t entry, uint8_t* entity, uint32_t) {
    const Builder b =
        weapon_sound(entry - 0x90, entity, reinterpret_cast<const char*>(pointer(arg(entry, 1))), arg(entry, 2));
    half(b.cmd, 0x20e) = 0xffff;
    word(b.cmd, 0x208) = 0xffffffffu;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_PlayWeaponSoundLoop)
uint32_t XB_CDECL FXCmd_PlayWeaponSoundLoop_body(uint32_t entry, uint8_t* entity, uint32_t) {
    const Builder b =
        weapon_sound(entry - 0x90, entity, reinterpret_cast<const char*>(pointer(arg(entry, 1))), arg(entry, 2));
    if (static_cast<uint8_t>(arg(entry, 3))) b.cmd[0x20c] |= 2;
    half(b.cmd, 0x20e) = 0xffff;
    word(b.cmd, 0x208) = 6;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_PlayWeaponSoundAtBone)
uint32_t XB_CDECL FXCmd_PlayWeaponSoundAtBone_body(uint32_t entry, uint8_t*, uint32_t) {
    const uint8_t* const entity = pointer(arg(entry, 1));
    const Builder b =
        weapon_sound(entry - 0x94, entity, reinterpret_cast<const char*>(pointer(arg(entry, 2))), arg(entry, 3));
    half(b.cmd, 0x20e) = word(entity, 0x248) ? 0xffff : static_cast<uint16_t>(arg(entry, 4));
    word(b.cmd, 0x208) = 5;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_PlayWeaponSoundAtBoneEx)
uint32_t XB_CDECL FXCmd_PlayWeaponSoundAtBoneEx_body(uint32_t entry, uint8_t*, uint32_t) {
    const uint8_t* const entity = pointer(arg(entry, 1));
    const Builder b =
        weapon_sound(entry - 0x94, entity, reinterpret_cast<const char*>(pointer(arg(entry, 2))), arg(entry, 3));
    b.set_both(0x800);
    word(b.cmd, 0xa0) = arg(entry, 5);
    half(b.cmd, 0x20e) = word(entity, 0x248) ? 0xffff : static_cast<uint16_t>(arg(entry, 4));
    word(b.cmd, 0x208) = 4;
    return b.commit();
}

namespace {

// Kind 3: the alias name at +0x134 and as the script string.
Builder sound_alias(uint32_t top, const void* entity, int32_t handle, const char* alias) {
    Builder b(top, entity, handle);
    b.kind(3);
    b.name(0x134, alias);
    b.set(0x40);
    ScriptString temp;
    script_text(b.cmd, &temp, alias);
    return b;
}

}  // namespace

FXCMD_ENTRY(FXCmd_PlaySoundAliasAtPoint)
uint32_t XB_CDECL FXCmd_PlaySoundAliasAtPoint_body(uint32_t entry, uint8_t*, uint32_t) {
    const char* const alias = reinterpret_cast<const char*>(pointer(arg(entry, 2)));
    if (!alias) return 0;
    const Builder b = sound_alias(entry - 0x94, pointer(arg(entry, 1)), -1, alias);
    if (static_cast<uint8_t>(arg(entry, 4))) b.cmd[0x20c] |= 2;
    b.cmd[0x20c] |= 1;
    std::memcpy(b.cmd + 0x1d0, pointer(arg(entry, 3)), 12);
    word(b.cmd, 0x208) = 0xffffffffu;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_PlaySoundAliasMode7)
uint32_t XB_CDECL FXCmd_PlaySoundAliasMode7_body(uint32_t entry, uint8_t*, uint32_t) {
    const char* const alias = reinterpret_cast<const char*>(pointer(arg(entry, 2)));
    if (!alias) return 0;
    const Builder b = sound_alias(entry - 0x94, pointer(arg(entry, 1)), -1, alias);
    b.cmd[0x20c] |= 2;
    word(b.cmd, 0x200) = arg(entry, 3);
    word(b.cmd, 0x208) = 7;
    b.cmd[0x20c] |= 8;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_PlaySoundAlias)
uint32_t XB_CDECL FXCmd_PlaySoundAlias_body(uint32_t entry, uint8_t*, uint32_t entity) {
    const char* const alias = reinterpret_cast<const char*>(pointer(arg(entry, 1)));
    if (!alias) return 0;
    const Builder b = sound_alias(entry - 0x94, pointer(entity), static_cast<int32_t>(arg(entry, 3)), alias);
    if (static_cast<uint8_t>(arg(entry, 2))) b.cmd[0x20c] |= 2;
    word(b.cmd, 0x208) = 0xffffffffu;
    if (static_cast<uint8_t>(arg(entry, 4)))
        b.cmd[0x20c] |= 8;
    else
        b.cmd[0x20c] &= 0xf7;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_Type2AtTransform)
uint32_t XB_CDECL FXCmd_Type2AtTransform_body(uint32_t entry, uint8_t* entity, uint32_t) {
    const uint8_t* const transform = pointer(arg(entry, 1));
    const Builder b(entry - 0x10, entity, -1);
    b.kind(2);
    script_default(b.cmd, reinterpret_cast<ScriptString*>(pointer(entry + 4)));  // in the argument slot
    b.transform(transform, true);
    word(b.cmd, 0x208) = 3;
    return b.commit();
}

namespace {

// The entity's +0x23c record with +0x58c set skips kinds 0 and 1.
bool skips_kind01(const uint8_t* entity) {
    const uint8_t* const record = pointer(word(entity, 0x23c));
    return record && word(record, 0x58c);
}

}  // namespace

FXCMD_ENTRY(FXCmd_Type1AtTransform)
uint32_t XB_CDECL FXCmd_Type1AtTransform_body(uint32_t entry, uint8_t* entity, uint32_t) {
    if (skips_kind01(entity)) return 0;
    const Builder b(entry - 0x10, entity, -1);
    b.kind(1);
    b.set(4);
    word(b.cmd, 0xa4) = arg(entry, 1);
    ScriptString temp;
    script_default(b.cmd, &temp);
    b.transform(pointer(arg(entry, 2)), false);
    word(b.cmd, 0x208) = 2;
    return b.commit();
}

FXCMD_ENTRY(FXCmd_Type0AtTransform)
uint32_t XB_CDECL FXCmd_Type0AtTransform_body(uint32_t entry, uint8_t* entity, uint32_t) {
    if (skips_kind01(entity)) return 0;
    const Builder b(entry - 0x14, entity, -1);
    b.kind(0);
    b.set(4);
    word(b.cmd, 0xa4) = arg(entry, 1);
    ScriptString temp;
    script_default(b.cmd, &temp);
    b.transform(pointer(arg(entry, 2)), true);
    word(b.cmd, 0x208) = 1;
    return b.commit();
}

#undef FXCMD_ENTRY

}  // namespace game
