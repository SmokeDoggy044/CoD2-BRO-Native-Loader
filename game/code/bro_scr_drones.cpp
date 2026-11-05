#include "bro_scr_drones.h"

#include "bro_scr_common.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

namespace {

// A BroScr_DroneMain thread (Scr_StartThread_ent_b_b with pointers to two bytes: 0 and `flag`) on
// each valid drone of the level's list (+0x770) within sqrt(*radius_sq) of `ent`.
void start_drones_near(uint32_t ent, uint32_t radius_sq, uint8_t flag) {
    const auto drones = []() { return reinterpret_cast<DynArray*>(title_global<uint32_t>(0x00922ee0) + 0x770); };
    if (static_cast<int32_t>(drones()->size) < 1) return;
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(drones()->size); ++n) {
        DynArray* list = drones();
        if (n >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), n + 1, n + 4);
        if (!scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(list->data[n])) continue;  // Scr_EntityHandleIsValid
        using GetOrigin = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
        uint32_t buffer[3];
        const uint32_t* o = scr_slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        uint32_t from[3] = {o[0], o[1], o[2]};
        list = drones();
        if (n >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), n + 1, n + 4);
        uint32_t buffer2[3];
        o = scr_slot<GetOrigin>(0x00922b20)(buffer2, list->data[n]);
        uint32_t to[3] = {o[0], o[1], o[2]};
        const long double d = scr_slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(to, from);  // Scr_DistanceSquared
        if (!(static_cast<long double>(title_global<float>(radius_sq)) >= d)) continue;
        list = drones();
        uint8_t flags[2] = {flag, 0};
        if (n >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), n + 1, n + 4);
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*,
                                              const uint8_t*);
        title_function<StartThread>(0x0045a970)(0, 0, 0, 0, 0x00169e50, list->data[n], &flags[1], &flags[0]);  // Scr_StartThread_ent_b_b(BroScr_DroneMain)
    }
}

}  // namespace

void XB_CDECL BroScr_StartDronesNearEnt(uint32_t ent) { start_drones_near(ent, 0x0083ed4c, 0); }       // 4096 (64 units)
void XB_CDECL BroScr_StukaStartNearbyDrones(uint32_t ent) { start_drones_near(ent, 0x0083f224, 1); }  // 147456 (384 units)

void XB_CDECL BroScr_StartDronesByTargetname(uint32_t self, ScriptString name) {
    (void)self;
    DynArray drones{nullptr, 0, 0};
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c,
                    reinterpret_cast<uint8_t*>(&drones), 0);
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(drones.size); ++n) {
        if (n >= drones.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&drones), n + 1, n + 4);
        const uint32_t drone = drones.data[n];
        uint8_t* functor = static_cast<uint8_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x10));  // ScrIface_Alloc
        if (functor) {
            uint32_t* words = reinterpret_cast<uint32_t*>(functor);
            words[0] = 0x007f4a7c;
            words[1] = 0x00169e50;  // BroScr_DroneMain
            words[2] = drone;
            functor[0xc] = 0;
            functor[0xd] = 0;
        }
        using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*, uint32_t);
        scr_slot<CreateThread>(0x0092216c)(0, 0, 0, drone, functor, 0);  // Scr_CreateThread
    }
    if (drones.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(drones.data);  // Scr_Free
    scr_release(&name);
}

void XB_CDECL BroScr_StartDroneDeathThreads(uint32_t self, ScriptString name) {
    (void)self;
    DynArray drones{nullptr, 0, 0};
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c,
                    reinterpret_cast<uint8_t*>(&drones), 0);
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(drones.size); ++n) {
        const uint32_t object = title_global<uint32_t>(0x00922ee0);
        const uint32_t* level = reinterpret_cast<const uint32_t*>(object ? object + 4 : 0);
        if (n >= drones.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&drones), n + 1, n + 4);
        ScriptString noteworthy;
        ScriptString* note = ScrCall_ScrEnt_GetScriptNoteworthy(&drones.data[n], &noteworthy);
        const uint32_t level_word = *level;
        if (n >= drones.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&drones), n + 1, n + 4);
        using Begin = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                        ScriptString*);
        title_function<Begin>(0x0017b530)(0, 0, 0, drones.data[n], 0xe2a210cf, 0x00171970, level_word,
                                          note);  // Scr_BeginThread_ent_str(BroScr_DroneDeathThink)
        scr_release(&noteworthy);
    }
    if (drones.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(drones.data);  // Scr_Free
    scr_release(&name);
}

}  // namespace game
