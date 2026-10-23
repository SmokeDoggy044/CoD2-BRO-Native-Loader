#include "g_script.h"

#include <cstring>

#include "dobj.h"
#include "g_utils.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// Calls a title function with EAX, ECX, EDX, EBX, ESI, EDI set and `count` stack words
// (args[0] pushed last); ESP restored afterwards (cdecl and stdcall callees alike).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL script_call(uint32_t function, const uint32_t* regs, uint32_t count,
                                                         const uint32_t* args);
PLATFORM_ASM_ENTRY(script_call,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 20(%ebp), %esi\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "pushl (%esi,%ecx,4)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 4(%eax), %ecx\n"
                   "movl 8(%eax), %edx\n"
                   "movl 12(%eax), %ebx\n"
                   "movl 16(%eax), %esi\n"
                   "movl 20(%eax), %edi\n"
                   "movl 0(%eax), %eax\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebx\n"
                   "popl %ebp\n"
                   "ret")

// The script interface: ScrIface_Alloc and Scr_CreateThread through their pointers.
void* scr_alloc(uint32_t size) { return title_function<void* XB_CDECL(uint32_t)>(title_global<uint32_t>(0x00922928))(size); }

// A field of a thread functor: copied from the pointer argument into the object.
struct FunctorField {
    uint16_t offset;
    uint16_t size;
};

// The generated Scr_StartThread_* wrappers: a functor object {vtable, a5, a6, *a7, ...}
// (0 when the allocation fails), then Scr_CreateThread(a2, a3, a4, a6, functor, a1).
uint32_t start_thread(const uint32_t* args, uint32_t size, uint32_t vtable, const FunctorField* fields, int count) {
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(size));
    if (functor) {
        functor[1] = args[4];
        functor[0] = vtable;
        functor[2] = args[5];
        for (int i = 0; i < count; ++i)
            std::memcpy(reinterpret_cast<uint8_t*>(functor) + fields[i].offset, pointer<const void>(args[6 + i]), fields[i].size);
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    return title_function<CreateThread>(title_global<uint32_t>(0x0092216c))(args[1], args[2], args[3], args[5], functor,
                                                                           args[0]);
}

}  // namespace

#define SCRIPT_ARGS_ENTRY(name)                                            \
    PLATFORM_ASM_ENTRY(name##_entry, "leal 4(%esp), %eax\n"               \
                                     "pushl %eax\n" PLATFORM_ASM_CALL(name) \
                                     "addl $4, %esp\n"                     \
                                     "ret")

SCRIPT_ARGS_ENTRY(Scr_StartThreadFunctor1)
uint32_t XB_CDECL Scr_StartThreadFunctor1(const uint32_t* args) { return start_thread(args, 0xc, 0x007f5ba0, nullptr, 0); }

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_f_f_b)
uint32_t XB_CDECL Scr_StartThread_ent_f_f_b(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 1}};
    return start_thread(args, 0x18, 0x00824434, fields, 3);
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_hash_ent_hash)
uint32_t XB_CDECL Scr_StartThread_ent_ent_hash_ent_hash(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}, {0x18, 4}};
    return start_thread(args, 0x1c, 0x00824428, fields, 4);
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_vec_b_3)
uint32_t XB_CDECL Scr_StartThread_ent_vec_b_3(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 12}, {0x18, 1}};
    return start_thread(args, 0x1c, 0x008244dc, fields, 2);
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_hash_f_i_f_f_2)
uint32_t XB_CDECL Scr_StartThread_ent_hash_f_i_f_f_2(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}, {0x18, 4}, {0x1c, 4}};
    return start_thread(args, 0x20, 0x008244a0, fields, 5);
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_f_i_i_i_f)
uint32_t XB_CDECL Scr_StartThread_ent_ent_f_i_i_i_f(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}, {0x18, 4}, {0x1c, 4}, {0x20, 4}};
    return start_thread(args, 0x24, 0x008244ac, fields, 6);
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_hash_i_i_f_f_f_f_hash_2)
uint32_t XB_CDECL Scr_StartThread_ent_hash_i_i_f_f_f_f_hash_2(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4},  {0x10, 4}, {0x14, 4}, {0x18, 4},
                                          {0x1c, 4}, {0x20, 4}, {0x24, 4}, {0x28, 4}};
    return start_thread(args, 0x2c, 0x00824470, fields, 8);
}

// The functor goes into the a7 slot and the slots shift (a4 <- a7, a5 <- a4, a6 <- a5),
// then a tail jump to Scr_BeginThreadEndOn with the caller's return address.
PLATFORM_ASM_ENTRY(Scr_BeginThreadEndOnFunctor1_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Scr_BeginThreadEndOnFunctor1) "addl $4, %esp\n"
                   "movl 24(%esp), %eax\n"
                   "movl 20(%esp), %edx\n"
                   "movl 16(%esp), %ecx\n"
                   "jmp *0x00922170")

void XB_CDECL Scr_BeginThreadEndOnFunctor1(uint32_t* args) {
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(0xc));
    const uint32_t a7 = args[6];
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = args[5];
        functor[2] = a7;
    }
    args[6] = address_of(functor);
    args[5] = args[4];
    args[4] = args[3];
    args[3] = a7;
}

PLATFORM_ASM_ENTRY(G_Error_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_Error) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL G_Error(const uint32_t* args) {
    // The message is formatted and dropped (the error call is compiled out).
    char text[0x400];
    return title_function<int32_t XB_CDECL(char*, const char*, const void*)>(0x001eea95)(  // _vsprintf
        text, pointer<const char>(args[0]), &args[1]);
}

void XB_CDECL G_EndGame() {
    title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(5, pointer<const char>(0x0083b39c));  // "endgame"
}

void XB_REGS3 XB_CDECL G_KillEntityOutright(uint32_t, uint32_t, uint8_t* ent) {
    const uint32_t die = at<uint32_t>(ent, 0x344);
    if (!die) return;
    const int32_t health = at<int32_t>(ent, 0x350);
    at<int32_t>(ent, 0x350) = health - (health + 1);
    const uint32_t world = at<uint32_t>(title_global<uint8_t*>(0x0095f534), 4);
    float direction[3] = {0.0f, 0.0f, title_global<float>(0x0083eb20)};
    using Die = void XB_CDECL(uint8_t*, uint32_t, uint32_t, int32_t, uint32_t, uint32_t, float*, float*, uint32_t);
    title_function<Die>(title_global<uint32_t>(0x008da88c + die * 4))(ent, world, world, health + 1, 0, 0, &at<float>(ent, 0x140),
                                                                     direction, 0);
}

PLATFORM_ASM_ENTRY(G_Trigger_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_Trigger) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_Trigger(uint8_t* self, uint8_t* other) {
    // This frame's trigger pairs (0x0098606c, 16 bytes each, count 0x0098706c).
    uint32_t* const pairs = pointer<uint32_t>(0x0098606c);
    int32_t& count = title_global<int32_t>(0x0098706c);
    for (int32_t i = 0; i < count; ++i)
        if (pairs[i * 4] == at<uint32_t>(self, 0x224) && pairs[i * 4 + 1] == at<uint32_t>(other, 0x224)) return;
    if (count == 0x100) {
        // Full: notify "trigger" with the other entity at once (G_NotifyWithEntity, self in EDI).
        uint32_t number = at<uint32_t>(other, 0x224);
        const uint32_t regs[6] = {at<uint32_t>(other, 0x224), 0x100, title_global<uint32_t>(0x0097a818), address_of(other), 0x100,
                                  address_of(self)};
        const uint32_t args[2] = {title_global<uint32_t>(0x0097a818), address_of(&number)};
        script_call(0x000265a0, regs, 2, args);
        return;
    }
    uint32_t* pair = pairs + count * 4;
    ++count;
    pair[0] = at<uint32_t>(self, 0x224);
    pair[1] = at<uint32_t>(other, 0x224);
    pair[2] = at<uint32_t>(self, 0xc4);
    pair[3] = at<uint32_t>(other, 0xc4);
}


namespace {

using FreeEntity = void XB_CDECL(uint8_t*, uint32_t);
uint32_t free_entity(uint8_t* ent) {
    title_function<FreeEntity>(0x002be420)(ent, 0);  // G_FreeEntity
    return 0;
}

}  // namespace

void XB_CDECL SP_corona(uint8_t* ent) { free_entity(ent); }
void XB_CDECL SP_misc_model(uint8_t* ent) { free_entity(ent); }
void XB_CDECL SP_light(uint8_t* ent) { free_entity(ent); }
void XB_CDECL SP_info_null(uint8_t* ent) { free_entity(ent); }

PLATFORM_ASM_ENTRY(G_UpdateEntityStringHashes_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_UpdateEntityStringHashes) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL G_UpdateEntityStringHashes(uint8_t* ent) {
    // Each script string (+6 = the text) hashed into the word after it.
    static const uint16_t fields[] = {0x25c, 0x274, 0x264, 0x26c, 0x27c, 0x284};
    uint32_t hash = 0;
    for (const uint16_t field : fields) {
        const uint32_t string = at<uint32_t>(ent, field);
        hash = Com_HashKeyNoCase(string, string ? pointer<const char>(string + 6) : nullptr);
        at<uint32_t>(ent, field + 4) = hash;
    }
    return hash;
}

void XB_REGS3 XB_CDECL G_UpdateEntityLOD(uint32_t, uint32_t, uint8_t* ent) {
    DObj* obj = at<DObj*>(ent, 0x22c);
    if (!obj) return;
    if (at<uint8_t>(obj, 0x9c) & 1) {
        DObj_SetLod(obj, 5);
        return;
    }
    // Squared distance to the player, scaled (0x008da6d8), against the LOD thresholds.
    const uint8_t* player = at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
    const float dy = at<float>(ent, 0x144) - at<float>(player, 0x144);
    const float dx = at<float>(ent, 0x140) - at<float>(player, 0x140);
    const float dz = at<float>(ent, 0x148) - at<float>(player, 0x148);
    const float distance = (((dz * dz) + (dy * dy)) + (dx * dx)) * title_global<float>(0x008da6d8);
    int32_t lod = 0;
    if (distance >= title_global<float>(0x0083ee60))
        lod = 4;
    else if (distance >= title_global<float>(0x0083ee5c))
        lod = 3;
    else if (distance >= title_global<float>(0x0083ee58))
        lod = 2;
    DObj_SetLod(obj, lod);
}

uint32_t XB_REGS2 XB_CDECL G_ClearEntityReferences(uint32_t, uint8_t* ent) {
    const uint32_t self = address_of(ent);
    if (uint8_t* clients = title_global<uint8_t*>(0x00985380)) {
        for (uint32_t offset = 0; offset < 0x2940; offset += 0x84) {
            if (at<uint32_t>(title_global<uint8_t*>(0x00985380), offset + 8) == self) at<uint32_t>(clients, offset + 8) = 0;
            if (at<uint32_t>(title_global<uint8_t*>(0x00985380), offset + 0xc) == self) at<uint32_t>(clients, offset + 0xc) = 0;
            if (at<uint32_t>(title_global<uint8_t*>(0x00985380), offset + 0x4c) == at<uint32_t>(ent, 0x244))
                at<uint32_t>(title_global<uint8_t*>(0x00985380), offset + 0x4c) = 0;
        }
    }
    for (uint32_t slot = 0x00985384; slot < 0x00985404; slot += 4) {
        uint8_t* actor = title_global<uint8_t*>(slot);
        if (!actor || at<int32_t>(actor, 0x1360) < 0 || at<uint32_t>(actor, 0x7cc) != self) continue;
        at<uint32_t>(actor, 0x7c8) = 0;
        at<uint32_t>(actor, 0x7cc) = 0;
    }
    if (uint8_t* client = title_global<uint8_t*>(0x00985370))
        if (at<uint32_t>(client, 0x5f4) == self) at<uint32_t>(client, 0x5f4) = 0;
    Vehicle_ClearEntityRefs(0, ent);
    for (uint32_t i = 0; i < 0x20; ++i)
        if (title_global<uint32_t>(0x00985e30 + i * 4) == at<uint32_t>(ent, 0x224)) title_global<uint32_t>(0x00985e30 + i * 4) = 0;
    return 0x20;
}

PLATFORM_ASM_ENTRY(G_FindLevelScriptModule_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_FindLevelScriptModule) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL G_FindLevelScriptModule(const char* name) {
    // {level name, the level section's module entry}; the last one is dragons_teeth.
    static const uint32_t modules[][2] = {
        {0x008325d0, 0x004588d0}, {0x0082bc60, 0x004af780}, {0x0082bc58, 0x004e8f30}, {0x0082bc48, 0x0052b580},
        {0x0082bc3c, 0x00564950}, {0x0082cc0c, 0x005b3dc0}, {0x0082bc34, 0x00601e40}, {0x0082bc28, 0x0065f680},
        {0x0082bc20, 0x00690340}, {0x0082bcc0, 0x006d04e0}, {0x0082bcb8, 0x00739560}, {0x0082bcac, 0x0075e7c0},
        {0x0082bca0, 0x0078a010}};
    using StrICmp = int32_t XB_CDECL(const char*, const char*);
    for (const auto& module : modules)
        if (!title_function<StrICmp>(0x001f1443)(name, pointer<const char>(module[0]))) return module[1];
    return title_function<StrICmp>(0x001f1443)(name, pointer<const char>(0x0082bc90)) ? 0 : 0x007a8ae0;
}

PLATFORM_ASM_ENTRY(G_SetMovedir_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_SetMovedir) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_SetMovedir(float* angles, float* movedir) {
    // The special up (0x008feefc) and down (0x008feee4) angles map to fixed directions.
    const float* up = pointer<const float>(0x008feefc);
    const float* down = pointer<const float>(0x008feee4);
    const float* direction = nullptr;
    if (angles[0] == up[0] && angles[1] == up[1] && angles[2] == up[2])
        direction = pointer<const float>(0x008feef0);
    else if (angles[0] == down[0] && angles[1] == down[1] && angles[2] == down[2])
        direction = pointer<const float>(0x008feed8);
    if (direction) {
        movedir[0] = direction[0];
        movedir[1] = direction[1];
        movedir[2] = direction[2];
    } else {
        float yaw_sin, yaw_cos, pitch_sin, pitch_cos;
        FastSinCos(&yaw_sin, 0, &yaw_cos, angles[1] * title_global<float>(0x0083edd8));
        FastSinCos(&pitch_sin, 0, &pitch_cos, angles[0] * title_global<float>(0x0083edd8));
        movedir[0] = pitch_cos * yaw_cos;
        movedir[1] = pitch_cos * yaw_sin;
        movedir[2] = 0.0f - pitch_sin;
    }
    angles[2] = 0.0f;
    angles[1] = 0.0f;
    angles[0] = 0.0f;
}

float* XB_STDCALL G_GetEntityMatrix(uint8_t* ent, float* out) {
    // Rows forward, -left, up (the EDI/ESI buffers of AnglesToAxis) and the origin; each
    // row's fourth word repeats its first.
    float forward[3];
    float left[3];
    float up[3];
    uint32_t args[2] = {address_of(ent + 0x150), address_of(forward)};
    AnglesToAxis(args, left, up);
    float* m = &at<float>(ent, 0x160);
    const float rows[16] = {forward[0], forward[1], forward[2], forward[0], 0.0f - left[0], 0.0f - left[1], 0.0f - left[2],
                            0.0f - left[0], up[0], up[1], up[2], up[0], at<float>(ent, 0x140), at<float>(ent, 0x144),
                            at<float>(ent, 0x148), at<float>(ent, 0x140)};
    std::memcpy(m, rows, 64);
    std::memcpy(out, m, 64);
    return out;
}

PLATFORM_ASM_ENTRY(G_RunThink_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_RunThink) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_RunThink(uint8_t* ent, uint32_t arg) {
    if (*reinterpret_cast<uint32_t*>(ent + 0x2a4) & 0x4000000) return;
    int32_t& next = *reinterpret_cast<int32_t*>(ent + 0x328);
    if (next <= 0 || next > title_global<int32_t>(0x0098540c)) return;  // level_time
    next = 0;
    if (!*reinterpret_cast<uint32_t*>(ent + 0x32c)) {
        const uint32_t args[1] = {0x0083b31c};  // "NULL ent->think"
        G_Error(args);
    }
    const uint32_t think = title_global<uint32_t>(0x008da788 + *reinterpret_cast<uint32_t*>(ent + 0x32c) * 4);
    title_function<void XB_CDECL(uint8_t*, uint32_t)>(think)(ent, arg);
}

}  // namespace game
