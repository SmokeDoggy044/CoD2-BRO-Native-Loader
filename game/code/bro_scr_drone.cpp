#include "bro_scr_drone.h"

#include "bro_scr_array.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

uint8_t* level() { return pointer<uint8_t>(title_global<uint32_t>(0x00922ee0)); }

// DynArray_Assign12 (thiscall: dst, src).
void assign12(DynArray* dst, const DynArray* src) {
    title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0045c410)(dst, src);
}

void free_data(uint32_t data) {
    if (data) Scr_Free(pointer<void>(data));
}

}  // namespace

uint32_t* XB_CDECL BroScr_DroneSplineMath(uint32_t* out, uint32_t, uint32_t ent) {
    {
        ScriptString name;
        const bool unset = ScriptString_Equals(ScrCall_GScr_GetCvar(&name, 0x00824c30),  // "drones_spline_name"
                                               pointer<const char>(0x0082dfeb));          // ""
        ScriptString_Release(&name);
        if (unset) slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(0x00824c30, 0x0083bd34);  // GScr_SetCvar(.., "all")
    }
    DynArray empty = {};
    ScrStringArray names = {};
    const int32_t create = slot<int32_t XB_CDECL(uint32_t)>(0x009222dc)(0x00802344);  // GScr_GetCvarInt("create_spline_files")
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    bool load = title_function<GetArray>(0x00602a40)(BroScr_ScriptObjectOrEmpty(ent), 0xb89aec99)->size == 0;
    if (load) {
        ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
        uint32_t* path = ScriptKeyTable_Find(table, 0x6bd99abb);
        if (!path) path = ScriptKeyTable_Set(table, 0x6bd99abb, 0);
        load = *path == 0 && create < 1 && level()[0x76c] == 0;
    }
    if (load) {
        ScriptString targetname;
        const ScriptString* const name = ScrCall_ScrEnt_GetTargetname(&ent, &targetname);
        uint32_t* const a = ScrField_h095a5675(ent);
        uint32_t* const b = ScrField_h384ad510(ent);
        uint32_t* const c = ScrField_drone_path_array(ent);
        const uint32_t text = name->rep ? address_of(name->rep->text) : 0;
        slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*, uint32_t*)>(0x009223a8)(text, c, b, a);
        ScriptString_Release(&targetname);
        const uint32_t loaded = *ScrField_drone_path_array(ent);
        if (loaded) {
            *reinterpret_cast<uint8_t*>(ScrField_h19738af8(ent, loaded)) = 1;
            out[0] = out[1] = out[2] = 0;
            title_function<void XB_THISCALL(ScrStringArray*)>(0x0045c9c0)(&names);  // ScrStringArray_Free
            return out;
        }
        // "Drone_Spline_Math():\n Spline File Not Found, " + targetname + ".spl" + the hint, dropped.
        ScriptString head, other, joined, with_suffix, message;
        ScriptString_FromCString(&head, pointer<const char>(0x00824c00));
        const ScriptString* const name2 = ScrCall_ScrEnt_GetTargetname(&ent, &other);
        ScriptString_Concat(&joined, &head, name2);
        ScriptString_ConcatCString(&with_suffix, &joined, pointer<const char>(0x00824bf8));  // ".spl"
        ScriptString_ConcatCString(&message, &with_suffix, pointer<const char>(0x00824bb0));
        ScriptString_Release(&with_suffix);
        ScriptString_Release(&joined);
        ScriptString_Release(&other);
        ScriptString_Release(&head);
        ScriptString_Release(&message);
    }
    out[0] = out[1] = out[2] = 0;
    assign12(reinterpret_cast<DynArray*>(out), &empty);
    return out;
}

BRO_SCR_ARGS_ENTRY(BroScr_BuildDroneSplines)
uint32_t XB_CDECL BroScr_BuildDroneSplines(uint32_t* args) {
    DynArray* const drones = reinterpret_cast<DynArray*>(&args[1]);
    ScriptString* const name = reinterpret_cast<ScriptString*>(&args[4]);
    int32_t* const counter = reinterpret_cast<int32_t*>(level() + 0x2e4);
    if (static_cast<uint32_t>(*counter) == title_global<uint32_t>(0x00922ec0)) *counter = 0;
    if (*reinterpret_cast<int32_t*>(level() + 0x2e4) > 0) {
        ++*reinterpret_cast<int32_t*>(level() + 0x2e4);
        free_data(address_of(drones->data));
        ScriptString_Release(name);
        return 0;
    }
    ++*reinterpret_cast<int32_t*>(level() + 0x2e4);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(drones->size); ++i) {
        DynArray splines = {};
        const uint8_t* const lvl = level();
        const uint32_t* const level_ent = lvl ? reinterpret_cast<const uint32_t*>(lvl + 4) : nullptr;
        if (i >= drones->size) DynArray_Resize(reinterpret_cast<uint8_t*>(drones), i + 1, i + 4);
        uint32_t* const drone = &drones->data[i];
        DynArray result;
        BroScr_DroneSplineMath(reinterpret_cast<uint32_t*>(&result), *level_ent, *drone);
        assign12(&splines, &result);
        free_data(address_of(result.data));
        ScriptString targetname, underscore, numbered, file;
        const ScriptString* const name_i = ScrCall_ScrEnt_GetTargetname(BroScr_ArrayElement(&args[1], i), &targetname);
        ScriptString_ConcatCString(&underscore, name_i, pointer<const char>(0x0082b1b8));  // "_"
        ScriptString_PlusInt(&numbered, &underscore, static_cast<int32_t>(i));
        ScriptString_ConcatCString(&file, &numbered, pointer<const char>(0x00824bf8));  // ".spl"
        ScriptString_Release(&numbered);
        ScriptString_Release(&underscore);
        ScriptString_Release(&targetname);
        ScriptString_Release(&file);
        free_data(address_of(splines.data));
    }
    if (drones->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(drones), 1, 4);
    ScriptString first, file;
    ScriptString_ConcatCString(&file, ScrCall_ScrEnt_GetTargetname(drones->data, &first), pointer<const char>(0x00824bf8));
    ScriptString_Release(&first);
    *reinterpret_cast<int32_t*>(level() + 0x2e4) = 0;
    ScriptString_Release(&file);
    free_data(address_of(drones->data));
    ScriptString_Release(name);
    return 0;
}

BRO_SCR_ARGS_ENTRY(BroScr_BuildDroneSplinesForEntArrays)
uint32_t XB_CDECL BroScr_BuildDroneSplinesForEntArrays(uint32_t* args) {
    ScrStringArray* const groups = reinterpret_cast<ScrStringArray*>(&args[1]);
    ScriptString* const name = reinterpret_cast<ScriptString*>(&args[4]);
    ScriptString none;
    ScriptString_FromRep(&none, nullptr);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(groups->count); ++i) {
        DynArray drones = {};
        if (i >= groups->count)
            title_function<void XB_THISCALL(ScrStringArray*, uint32_t, uint32_t)>(0x0045c2e0)(groups, i + 1, i + 4);  // DynArray_ReserveString
        Scr_GetEntArray(address_of(&groups->data[i]), 0x0d786b4c, reinterpret_cast<uint8_t*>(&drones), 0);
        if (drones.data && static_cast<int32_t>(drones.size) > 0) {
            const uint8_t* const lvl = level();
            const uint32_t* const level_ent = lvl ? reinterpret_cast<const uint32_t*>(lvl + 4) : nullptr;
            uint32_t block[5];
            ScriptString_Copy(reinterpret_cast<ScriptString*>(&block[4]), name);
            block[1] = block[2] = block[3] = 0;
            title_function<void XB_THISCALL(uint32_t*, const DynArray*)>(0x0045c270)(&block[1], &drones);  // DynArray_Assign4
            block[0] = *level_ent;
            BroScr_BuildDroneSplines(block);
        }
        free_data(address_of(drones.data));
    }
    ScriptString_Release(&none);
    if (groups->data) {
        ScriptString* const end = groups->data + groups->count;
        for (ScriptString* s = groups->data; s != end; ++s) ScriptString_Release(s);
        Scr_Free(groups->data);
    }
    ScriptString_Release(name);
    return 0;
}

}  // namespace game
