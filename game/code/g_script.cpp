#include "g_script.h"

#include <cstring>

#include "dobj.h"
#include "g_utils.h"
#include "g_scr_runtime.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "script_string.h"
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
uint32_t* make_functor(const uint32_t* args, uint32_t size, uint32_t vtable, const FunctorField* fields, int count) {
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(size));
    if (functor) {
        functor[1] = args[4];
        functor[0] = vtable;
        functor[2] = args[5];
        for (int i = 0; i < count; ++i)
            std::memcpy(reinterpret_cast<uint8_t*>(functor) + fields[i].offset, pointer<const void>(args[6 + i]), fields[i].size);
    }
    return functor;
}

// Wrappers whose functor class has a constructor (thiscall, callee-cleaned): (a[first], &a[first+1],
// a[first+2], ...) with `count` words in all; the constructor returns the object.
uint32_t* construct_functor(const uint32_t* args, uint32_t size, uint32_t ctor, uint32_t first, uint32_t count) {
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(size));
    if (!functor) return nullptr;
    uint32_t words[12];
    words[0] = args[first];
    words[1] = address_of(&args[first + 1]);
    for (uint32_t i = 2; i < count; ++i) words[i] = args[first + i];
    const uint32_t regs[6] = {0, address_of(functor), 0, 0, 0, 0};
    return pointer<uint32_t>(script_call(ctor, regs, count, words));
}

// Scr_CreateThread (0x0092216c) or Scr_BeginThread (0x00922168) through its pointer:
// (a2, a3, a4, a6, functor, a1).
uint32_t create_thread(const uint32_t* args, uint32_t* functor, uint32_t create = 0x0092216c) {
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    return title_function<CreateThread>(title_global<uint32_t>(create))(args[1], args[2], args[3], args[5], functor, args[0]);
}

// The Scr_CallThread_* wrappers: Scr_BeginThread, then Scr_WaitFrames(0) when called from a
// script thread (Scr_InThread); returns the thread.
uint32_t call_thread(const uint32_t* args, uint32_t* functor) {
    const uint32_t thread = create_thread(args, functor, 0x00922168);
    if (static_cast<uint8_t>(title_function<uint32_t XB_CDECL()>(title_global<uint32_t>(0x0092218c))()))  // Scr_InThread
        title_function<void XB_CDECL(uint32_t)>(0x0011e540)(0);  // Scr_WaitFrames
    return thread;
}

uint32_t start_thread(const uint32_t* args, uint32_t size, uint32_t vtable, const FunctorField* fields, int count) {
    return create_thread(args, make_functor(args, size, vtable, fields, count));
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

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_str_vec)
uint32_t XB_CDECL Scr_StartThread_ent_ent_str_vec(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x20, 0x0017a3c0, 4, 5));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_i_str_b_2)
uint32_t XB_CDECL Scr_StartThread_ent_ent_i_str_b_2(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x1c, 0x0017a490, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_ent_str)
uint32_t XB_CDECL Scr_CallThread_ent_ent_str(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x14, 0x00459b70, 4, 4));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_hash_vec_hash_f_hash_f)
uint32_t XB_CDECL Scr_StartThread_ent_hash_vec_hash_f_hash_f(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x2c, 0x0017a550, 4, 8));
}

SCRIPT_ARGS_ENTRY(Scr_StartThreadFunctorN)
uint32_t XB_CDECL Scr_StartThreadFunctorN(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x1c, 0x0017a650, 4, 4));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_vec_str_vec_f_3)
uint32_t XB_CDECL Scr_StartThread_ent_vec_str_vec_f_3(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x2c, 0x0017a740, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_RespawnerAttributes_i_str)
uint32_t XB_CDECL Scr_StartThread_ent_ent_RespawnerAttributes_i_str(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0xf8, 0x0017a870, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_RespawnerAttributes)
uint32_t XB_CDECL Scr_StartThread_ent_RespawnerAttributes(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0xec, 0x0017a9a0, 4, 3));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_str_i_str_2)
uint32_t XB_CDECL Scr_StartThread_ent_str_i_str_2(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x18, 0x0017aa40, 4, 5));
}

// Constructor (a6, &a7, a8), then Scr_BeginThreadEndOn(a1, a2, a3, a7, a4, a5, functor).
SCRIPT_ARGS_ENTRY(Scr_BeginThread_ent_str)
uint32_t XB_CDECL Scr_BeginThread_ent_str(const uint32_t* args) {
    uint32_t* functor = construct_functor(args, 0x10, 0x004591e0, 5, 3);
    using BeginThreadEndOn = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
    return title_function<BeginThreadEndOn>(title_global<uint32_t>(0x00922170))(args[0], args[1], args[2], args[6], args[3],
                                                                               args[4], functor);
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr)
uint32_t XB_CDECL Scr_StartThread_ent_ent_arrent_f_f_hash_hash_i_arr(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x3c, 0x0017c770, 4, 10));
}

SCRIPT_ARGS_ENTRY(Scr_StartThreadFunctor_20_3)
uint32_t XB_CDECL Scr_StartThreadFunctor_20_3(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x20, 0x0017c830, 4, 5));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_ent_str_ent_ent_4)
uint32_t XB_CDECL Scr_StartThread_ent_ent_str_ent_ent_4(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x1c, 0x001e3df0, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_StartThread_ent_i_pathnode_str_3)
uint32_t XB_CDECL Scr_StartThread_ent_i_pathnode_str_3(const uint32_t* args) {
    return create_thread(args, construct_functor(args, 0x18, 0x001e3ec0, 4, 5));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_vt00825be4_2)
uint32_t XB_CDECL Scr_CallThread_vt00825be4_2(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}};
    return call_thread(args, make_functor(args, 0x14, 0x00825be4, fields, 2));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_f_ent_3)
uint32_t XB_CDECL Scr_CallThread_ent_f_ent_3(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}};
    return call_thread(args, make_functor(args, 0x14, 0x007f4bfc, fields, 2));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_hash_i_str_i_ent)
uint32_t XB_CDECL Scr_CallThread_ent_hash_i_str_i_ent(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x20, 0x001e3fb0, 4, 7));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_hash_f_4)
uint32_t XB_CDECL Scr_CallThread_ent_hash_f_4(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}};
    return call_thread(args, make_functor(args, 0x14, 0x007f4a58, fields, 2));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_vec_f_str_i_ent_i)
uint32_t XB_CDECL Scr_CallThread_ent_vec_f_str_i_ent_i(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x2c, 0x00564fa0, 4, 8));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_vec)
uint32_t XB_CDECL Scr_CallThread_ent_vec(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 12}};
    return call_thread(args, make_functor(args, 0x18, 0x007f49e0, fields, 1));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_f_f_5)
uint32_t XB_CDECL Scr_CallThread_ent_f_f_5(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}};
    return call_thread(args, make_functor(args, 0x14, 0x007f4a64, fields, 2));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_hash_AnimDirections_AnimAngles_f_2)
uint32_t XB_CDECL Scr_CallThread_ent_hash_AnimDirections_AnimAngles_f_2(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x2c, 0x001e4080, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_hash_i_f_2)
uint32_t XB_CDECL Scr_CallThread_ent_hash_i_f_2(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}};
    return call_thread(args, make_functor(args, 0x18, 0x00825c08, fields, 3));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_anim_str_f_hash_str_f_b)
uint32_t XB_CDECL Scr_CallThread_ent_anim_str_f_hash_str_f_b(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x28, 0x001e4150, 4, 9));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_str_hash_hash_3)
uint32_t XB_CDECL Scr_CallThread_ent_str_hash_hash_3(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x18, 0x001e4280, 4, 5));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_f_hash_hash_4)
uint32_t XB_CDECL Scr_CallThread_ent_f_hash_hash_4(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}};
    return call_thread(args, make_functor(args, 0x18, 0x00825c2c, fields, 3));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_vec_vec_vec_i)
uint32_t XB_CDECL Scr_CallThread_ent_vec_vec_vec_i(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x34, 0x001e4330, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_f)
uint32_t XB_CDECL Scr_CallThread_ent_f(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}};
    return call_thread(args, make_functor(args, 0x10, 0x007f4a70, fields, 1));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_anim_anim_anim_anim_anim_anim)
uint32_t XB_CDECL Scr_CallThread_ent_anim_anim_anim_anim_anim_anim(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}, {0x18, 4}, {0x1c, 4}, {0x20, 4}};
    return call_thread(args, make_functor(args, 0x24, 0x00825c44, fields, 6));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_vt00825c50)
uint32_t XB_CDECL Scr_CallThread_vt00825c50(const uint32_t* args) {
    return call_thread(args, construct_functor(args, 0x1c, 0x001e4430, 4, 6));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_f_hash_hash_hash_i_2)
uint32_t XB_CDECL Scr_CallThread_ent_f_hash_hash_hash_i_2(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}, {0x18, 4}, {0x1c, 4}};
    return call_thread(args, make_functor(args, 0x20, 0x00825c5c, fields, 5));
}

SCRIPT_ARGS_ENTRY(Scr_CallThread_ent_hash_hash_f_5)
uint32_t XB_CDECL Scr_CallThread_ent_hash_hash_f_5(const uint32_t* args) {
    static const FunctorField fields[] = {{0xc, 4}, {0x10, 4}, {0x14, 4}};
    return call_thread(args, make_functor(args, 0x18, 0x00825c68, fields, 3));
}

// Runs a script thread's functor (vtable slot 1, thiscall) inside the original's try/catch; a
// caught exception ends the thread (the catch block resumes after the call). EAX = the slot's result.
uint32_t XB_THISCALL Scr_ThreadEntry(uint32_t* functor) {
    return title_function<uint32_t XB_THISCALL(uint32_t*)>(pointer<uint32_t>(functor[0])[1])(functor);
}

void XB_CDECL Scr_GetEntArray(uint32_t a1, uint32_t a2, uint8_t* array, uint32_t a4) {
    // GScr_GetEntArray fills up to 0x400 entities; the array is emptied and gets them appended.
    uint32_t ents[0x400];
    DynArray_Resize(array, 0, 0);
    using GetEntArray = int32_t XB_CDECL(uint32_t, uint32_t, uint32_t*, uint32_t, uint32_t);
    const int32_t count = title_function<GetEntArray>(title_global<uint32_t>(0x009221c4))(a1, a2, ents, 0x400, a4);
    DynArray_Reserve(array, at<uint32_t>(array, 8) + count);
    for (int32_t i = 0; i < count; ++i) {
        uint32_t ent = ents[i];
        DynArray_PushBack(array, &ent);
    }
}

void XB_CDECL Scr_NotifyAfterDeath(uint32_t object, uint32_t thread, uint32_t name) {
    title_function<void XB_CDECL(uint32_t, uint32_t)>(title_global<uint32_t>(0x00922188))(object, name);  // Scr_EndOn
    Scr_WaitTillString(thread, pointer<const char>(0x00831f60));  // "death"
    Scr_QueueNotify(object, name);
}

SCRIPT_ARGS_ENTRY(Scr_ReleaseStringArrayAndString)
void XB_CDECL Scr_ReleaseStringArrayAndString(uint32_t* args) {
    // a2 = the strings, a4 = their count, a5 = a string passed by value (released in its slot).
    uint32_t* const strings = pointer<uint32_t>(args[1]);
    if (strings) {
        const uint32_t end = args[1] + args[3] * 4;
        for (uint32_t p = args[1]; p != end; p += 4) title_function<void XB_THISCALL(uint32_t)>(0x0011d640)(p);  // ScriptString_Release
        Scr_Free(strings);
    }
    title_function<void XB_THISCALL(uint32_t*)>(0x0011d640)(&args[4]);
}

void XB_CDECL BroScr_PanzerIVSetDifficultyHealth(uint32_t ent) {
    // Health by difficulty (default 5000), then the vehicle health thread (0x0014d0a0) on the entity.
    ScriptString difficulty;
    ScriptString_FromRep(&difficulty, nullptr);
    title_function<void XB_CDECL(ScriptString*)>(title_global<uint32_t>(0x009223cc))(&difficulty);  // GScr_GetDifficulty
    uint32_t hash;
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&difficulty));
    uint32_t health = 0x1388;
    if (hash == 0x00330a19)
        health = 0x1964;
    else if (hash == 0xdc63aacd)
        health = 0x157c;
    else if (hash == 0x00358a24)
        health = 0x1388;
    else if (hash == 0x00000cb5)
        health = 0xdac;
    else if (hash == 0x003229d0)
        health = 0x1d4c;
    title_function<void XB_CDECL(uint32_t, uint32_t)>(title_global<uint32_t>(0x00922b94))(ent, health);  // ScrEnt_SetHealth
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(0xc));
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = 0x0014d0a0;
        functor[2] = ent;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    title_function<CreateThread>(title_global<uint32_t>(0x0092216c))(0, 0, 0, ent, functor, 0);
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&difficulty);  // ScriptString_Release
}

uint32_t XB_CDECL BroScr_PanzerIVUseAnimTree(uint32_t ent) {
    // The "panzerIV" anim tree, then the thread 0x001679b0 on the entity.
    ScriptString tree;
    title_function<void XB_THISCALL(ScriptString*, const char*)>(0x0011eb50)(&tree, pointer<const char>(0x007f499c));
    title_function<void XB_CDECL(uint32_t, ScriptString*)>(title_global<uint32_t>(0x009225f0))(ent, &tree);  // GScr_UseAnimTree
    title_function<void XB_THISCALL(ScriptString*)>(0x0011d640)(&tree);
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(0xc));
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = 0x001679b0;
        functor[2] = ent;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    return title_function<CreateThread>(title_global<uint32_t>(0x0092216c))(0, 0, 0, ent, functor, 0);
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

// 0x0011b4d0: like the Scr_StartThread_* wrappers but through Scr_BeginThread (0x00922168); when
// called from a script thread, that thread then waits a frame (Scr_WaitFrames(0)).
SCRIPT_ARGS_ENTRY(Scr_BeginThreadFunctor1)
uint32_t XB_CDECL Scr_BeginThreadFunctor1(const uint32_t* args) {
    uint32_t* functor = static_cast<uint32_t*>(scr_alloc(0xc));
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = args[4];
        functor[2] = args[5];
    }
    using BeginThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    const uint32_t thread = title_function<BeginThread>(title_global<uint32_t>(0x00922168))(args[1], args[2], args[3],
                                                                                           args[5], functor, args[0]);
    if (static_cast<uint8_t>(title_function<uint32_t XB_CDECL()>(title_global<uint32_t>(0x0092218c))()))  // Scr_InThread
        title_function<void XB_CDECL(uint32_t)>(0x0011e540)(0);  // Scr_WaitFrames
    return thread;
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

// The think callback runs at the original's ESP (entry - 8 with its two arguments): callbacks store stack
// addresses in .data (0x0098a4fc).
PLATFORM_ASM_ENTRY(G_RunThink_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_RunThink) "addl $20, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_RunThink(uint32_t entry_esp, uint8_t* ent, uint32_t arg, uint32_t ebx, uint32_t edi) {
    if (*reinterpret_cast<uint32_t*>(ent + 0x2a4) & 0x4000000) return;
    int32_t& next = *reinterpret_cast<int32_t*>(ent + 0x328);
    if (next <= 0 || next > title_global<int32_t>(0x0098540c)) return;  // level_time
    next = 0;
    if (!*reinterpret_cast<uint32_t*>(ent + 0x32c)) {
        const uint32_t args[1] = {0x0083b31c};  // "NULL ent->think"
        G_Error(args);
    }
    const uint32_t index = *reinterpret_cast<uint32_t*>(ent + 0x32c);
    const uint32_t ent_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    const uint32_t args[2] = {ent_word, arg};
    const TitleRegs regs = {index, ebx, arg, 0, ent_word, edi};
    title_call_at_regs(entry_esp, title_global<uint32_t>(0x008da788 + index * 4), &regs, 2, args);
}

uint32_t XB_THISCALL Scr_GetEntObjectOrEmpty(const uint32_t* entity) {
    const uint32_t object = title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(entity[0]);  // GScr_GetEntScriptObject
    return object ? object : 0x00922e94;
}

}  // namespace game
