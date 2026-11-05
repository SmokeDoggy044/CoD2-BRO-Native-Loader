#include "bro_scr_level_info.h"

#include "bro_scr_array.h"
#include "g_scr_runtime.h"
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

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

void call_ent(uint32_t interface_slot, uint32_t ent) { slot<void XB_CDECL(uint32_t)>(interface_slot)(ent); }
uint32_t spawnflags(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(ent); }  // ScrEnt_GetSpawnflags

using Getter = ScriptString* XB_THISCALL(const uint32_t*, ScriptString*);

bool string_is(Getter* get, const uint32_t* ent, uint32_t text) {
    ScriptString value;
    const bool equal = ScriptString_Equals(get(ent, &value), pointer<const char>(text));
    ScriptString_Release(&value);
    return equal;
}

const uint32_t kDelete = 0x00922540, kSetFlag400 = 0x00922568, kClearFlag400 = 0x00922564,
               kNotSolid = 0x009227c0, kSolid = 0x009227bc, kConnectPaths = 0x00922574;
const uint32_t kExploder = 0x008246fc, kChunk = 0x008246e0;  // "exploder", "exploderchunk"

}  // namespace

void XB_CDECL BroScr_SetupExploders(uint32_t ent) {
    const uint32_t number =
        slot<uint32_t XB_CDECL(uint32_t)>(0x009223c0)(*ScrField_script_exploder(ent));  // GScr_GetIntMapValue
    if (number != title_global<uint32_t>(0x00922ec0) && number != 0) {
        if (string_is(&ScrCall_ScrEnt_GetClassname, &ent, 0x00824708)) {  // "item_health"
            call_ent(kDelete, ent);
            return;
        }
        if (string_is(&ScrCall_ScrEnt_GetTargetname, &ent, kExploder)) {
            call_ent(kClearFlag400, ent);
            bool not_solid = false;
            ScriptString noteworthy;
            if (ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep) {
                ScriptString again;
                not_solid = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &again),
                                                pointer<const char>(0x008246f0));  // "notsolid"
                ScriptString_Release(&again);
            }
            ScriptString_Release(&noteworthy);
            call_ent(not_solid ? kNotSolid : kSolid, ent);
            if (spawnflags(ent) & 1) call_ent(kConnectPaths, ent);
            return;
        }
        bool chunk;
        {
            ScriptString first;
            chunk = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &first), pointer<const char>(kChunk));
            if (!chunk) {
                ScriptString second;
                chunk = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &second),
                                            pointer<const char>(0x008246c8));  // "exploderchunk visible"
                ScriptString_Release(&second);
            }
            ScriptString_Release(&first);
        }
        if (chunk) {
            ScriptString target;
            const bool has_target = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
            ScriptString_Release(&target);
            if (has_target) {
                uint32_t found;
                const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&ent, &target);
                const uint32_t piece = *ScrCall_GScr_GetEntArray(
                    &found, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)), 0x0d786b4c, 0);
                found = piece;
                ScriptString_Release(&target);
                if (ScrCall_Scr_EntityHandleIsValid(&found) & 0xff) call_ent(kDelete, piece);
            }
            call_ent(kDelete, ent);
            return;
        }
        bool defined;
        if (*ScrField_script_fxid_IsDefined(&defined, ent)) return;
        if (spawnflags(ent) & 1) call_ent(kConnectPaths, ent);
        call_ent(kDelete, ent);
        return;
    }
    ScriptString targetname;
    ScrCall_ScrEnt_GetTargetname(&ent, &targetname);
    bool fx_model;
    {
        ScriptString model;
        fx_model = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x008246bc)) &&  // "xmodel/fx"
                   (!targetname.rep || !ScriptString_Equals(&targetname, pointer<const char>(kChunk)));
        ScriptString_Release(&model);
    }
    if (fx_model) {
        call_ent(kSetFlag400, ent);
    } else if (ScriptString_Equals(&targetname, pointer<const char>(kExploder)) ||
               ScriptString_Equals(&targetname, pointer<const char>(kChunk))) {
        call_ent(kSetFlag400, ent);
        call_ent(kNotSolid, ent);
        if (spawnflags(ent) & 1) call_ent(kConnectPaths, ent);
    }
    const auto list = []() { return pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x78); };
    bool stored = false;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list()[2]); ++i) {
        if (slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(*BroScr_ArrayElement(list(), i))) continue;  // Scr_EntityHandleIsValid
        *BroScr_ArrayElement(list(), i) = ent;
        stored = true;
        break;
    }
    if (!stored) DynArray_PushBack(reinterpret_cast<uint8_t*>(list()), &ent);
    ScriptString_Release(&targetname);
}

}  // namespace game
