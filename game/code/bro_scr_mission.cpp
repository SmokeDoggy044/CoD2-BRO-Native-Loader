#include "bro_scr_mission.h"

#include "bro_scr_common.h"
#include "scr_access.h"
#include "title_data.h"

namespace game {

namespace {

uint32_t* hash_string(uint32_t* out, const ScriptString* string) {
    return title_function<uint32_t* XB_CDECL(uint32_t*, const ScriptString*)>(0x0011d8b0)(out, string);  // Scr_HashScriptString
}

// The word at +4 of a level object (null object: read through address 0, as the original).
uint32_t object_word(uint32_t object) {
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(field);
}

}  // namespace

void XB_CDECL BroScr_FlagSet(ScriptString name) {
    uint32_t hash;
    const uint32_t* key = hash_string(&hash, &name);
    const uint32_t map = title_global<uint32_t>(0x00922ee0) + 0x32c;
    uint32_t pair[2];  // the found iterator, then the inserted (key, value) pair
    title_function<void XB_THISCALL(uint32_t, uint32_t*, const uint32_t*)>(0x0045e6d0)(map, &pair[0], key);  // HashMap_Find
    uint32_t node = pair[0];
    if (node == *reinterpret_cast<uint32_t*>(map + 8)) {  // end
        pair[0] = *key;
        pair[1] = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool (bytes 1-3 never written: 0)
        uint32_t result[2];
        using Insert = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
        node = *title_function<Insert>(0x004608a0)(map, result, pair);  // HashMap_Insert
    }
    *reinterpret_cast<uint8_t*>(node + 0xc) = 1;
    const uint32_t level = object_word(title_global<uint32_t>(0x00922ee0));
    const uint32_t notify = *hash_string(&hash, &name);
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(level, notify);  // Scr_QueueNotify
    scr_release(&name);
}

uint32_t* XB_CDECL BroScr_GetUseGearCvar(uint32_t* out) {
    *out = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x009222dc)(0x00825ab8);  // GScr_GetCvarInt("g_useGear")
    return out;
}

void XB_CDECL BroScr_FriendlyFireMissionFail() {
    if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x009222dc)(0x00825b50) == 1) return;  // GScr_GetCvarInt("ff_off")
    using EndOn = void XB_CDECL(uint32_t, uint32_t);
    title_function<EndOn>(0x0011d930)(title_global<uint32_t>(0x009236dc), 0x00831f60);  // Scr_EndOnString(player, "death")
    title_function<EndOn>(0x0011d930)(object_word(title_global<uint32_t>(0x00922ef8)), 0x008242e4);  // "mine death"
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(object_word(title_global<uint32_t>(0x00922ef8)),
                                                            0xa264707f);  // Scr_QueueNotify
    uint8_t* const object = reinterpret_cast<uint8_t*>(title_global<uint32_t>(0x00922ef8));
    if (object[0xf0] != 0) return;
    object[0xf0] = 1;
    ScriptString text;
    scr_string_from(&text, 0x00825b38);  // "INGAME_MISSIONFAIL_FF"
    scr_slot<void XB_CDECL(ScriptString*)>(0x00922438)(&text);  // GScr_MissionFailed
    scr_release(&text);
}

void XB_CDECL BroScr_PrecacheModelBySpawn(ScriptString model) {
    uint32_t origin[3] = {0, 0, 0};  // 0.0f
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab98);  // "script_model"
    const uint32_t ent = scr_slot<uint32_t XB_CDECL(ScriptString*, uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
    scr_release(&classname);
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    ScrVec3 player{{lane, lane, lane}};
    scr_slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(title_global<uint32_t>(0x009236dc), &player);  // GScr_GetOrigin
    scr_slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, player);                                // ScrEnt_SetOrigin
    scr_slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, &model, 0);              // GScr_SetModel
    scr_slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);                                                  // GScr_Delete
    scr_release(&model);
}

}  // namespace game
